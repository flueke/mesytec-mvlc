/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mvlc.h"
#include "mvlc_constants.h"

#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include "mvlc_dialog.h"
#include "mvlc_error.h"
#include "util/storage_sizes.h"

#include <iostream>
using std::cout;
using std::endl;

namespace mesytec
{
namespace mvlc
{

namespace
{

void stack_error_poller(
    MVLCDialog &mvlc,
    Mutex &cmdMutex,
    Protected<StackErrorCounters> &stackErrorCounters,
    Mutex &suspendMutex,
    std::atomic<bool> &quit)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"error_poller",0,0,0);
#endif

    static constexpr auto Default_PollInterval = std::chrono::milliseconds(1000);

    std::vector<u32> buffer;
    buffer.reserve(util::Megabytes(1));
    std::error_code ec = {};

#define POLLER_DEBUG 0

#if POLLER_DEBUG
    auto threadId = std::this_thread::get_id();
    std::cout << "stack_error_notification_poller " << threadId << " entering loop" << std::endl;
#endif

    while (!quit)
    {
#if POLLER_DEBUG
        std::cout << "stack_error_notification_poller " << threadId << " waiting for suspendMutex" << std::endl;
#endif
        std::unique_lock<Mutex> suspendGuard(suspendMutex);

#if POLLER_DEBUG
        std::cout << "stack_error_notification_poller " << threadId << " begin read" << std::endl;
        auto tReadStart = std::chrono::steady_clock::now();
#endif
        {
            std::unique_lock<Mutex> cmdGuard(cmdMutex);
            ec = mvlc.readKnownBuffer(buffer);
        }

        suspendGuard.unlock();

#if POLLER_DEBUG
        auto tReadEnd = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> readElapsed = tReadEnd - tReadStart;
        std::cout << "stack_error_notification_poller " << threadId << " read done "
            << ", dt=" << readElapsed.count()
            << ", ec=" << ec.message()
            << ", buffer.size()=" << buffer.size()
            << std::endl;
#endif

        if (!buffer.empty())
        {
            //std::cout << "mvlc_readout::stack_error_notification_poller updating counters" << std::endl;
            update_stack_error_counters(stackErrorCounters.access().ref(), buffer);
        }

        if (ec == ErrorType::ConnectionError || buffer.empty())
        {
#if POLLER_DEBUG
            std::cout << "stack_error_notification_poller " << threadId << " sleeping" << std::endl;
#endif
            std::this_thread::sleep_for(Default_PollInterval);
#if POLLER_DEBUG
            std::cout << "stack_error_notification_poller " << threadId << " waking" << std::endl;
#endif
        }
    }

#if POLLER_DEBUG
    std::cout << "stack_error_notification_poller " << threadId << " exiting" << std::endl;
#endif

#undef POLLER_DEBUG
}

}

struct MVLC::Private
{
    explicit Private(std::unique_ptr<MVLCBasicInterface> &&impl_)
        : impl(std::move(impl_))
        , dialog(impl.get())
        , isConnected(false)
        , hardwareId(0)
        , firmwareRevision(0)
        , errorPollerQuit(false)
    {
        errorPollerThread = std::thread(
            stack_error_poller,
            std::ref(dialog),
            std::ref(locks.cmdMutex()),
            std::ref(dialog.getProtectedStackErrorCounters()),
            std::ref(errorPollerSuspendMutex),
            std::ref(errorPollerQuit));
    }

    ~Private()
    {
        errorPollerQuit = true;

        if (errorPollerThread.joinable())
            errorPollerThread.join();
    }

    std::unique_ptr<MVLCBasicInterface> impl;
    MVLCDialog dialog;
    mutable Locks locks;
    std::atomic<bool> isConnected;
    std::atomic<u32> hardwareId;
    std::atomic<u32> firmwareRevision;

    Mutex errorPollerSuspendMutex;
    std::atomic<bool> errorPollerQuit;
    std::thread errorPollerThread;

    std::error_code resultCheck(const std::error_code &ec)
    {
        // Update the local connection state without calling
        // impl->isConnected() which would require us to take both pipe locks.
        if (ec == ErrorType::ConnectionError)
            this->isConnected = false;
        return ec;
    }
};

MVLC::MVLC()
    : d(nullptr)
{
}

MVLC::MVLC(std::unique_ptr<MVLCBasicInterface> &&impl)
    : d(std::make_shared<Private>(std::move(impl)))
{
#if 0
    std::cout << __PRETTY_FUNCTION__
        << " this=" << this << ", d=" << d.get() << std::endl;
#endif
}

MVLC::~MVLC()
{
#if 0
    std::cout << __PRETTY_FUNCTION__
        << " this=" << this << ", d=" << d.get() << std::endl;
#endif
}

MVLCBasicInterface *MVLC::getImpl()
{
    return d->impl.get();
}

Locks &MVLC::getLocks()
{
    return d->locks;
}

std::error_code MVLC::connect()
{
    auto guards = d->locks.lockBoth();
    auto ec = d->impl->connect();
    d->isConnected = d->impl->isConnected();

    if (isConnected())
    {
        // Read hardware id and firmware revision.
        u32 hardwareId = 0;
        u32 firmwareRevision = 0;

        if (auto ec = d->dialog.readRegister(registers::hardware_id, hardwareId))
        {
            d->isConnected = false;
            return ec;
        }

        if (auto ec = d->dialog.readRegister(registers::firmware_revision, firmwareRevision))
        {
            d->isConnected = false;
            return ec;
        }

        d->hardwareId = hardwareId;
        d->firmwareRevision = firmwareRevision;
    }

    return ec;
}

std::error_code MVLC::disconnect()
{
    auto guards = d->locks.lockBoth();
    auto ec = d->impl->disconnect();
    d->isConnected = d->impl->isConnected();
    return ec;
}

bool MVLC::isConnected() const
{
#if 0
    auto guards = d->locks.lockBoth();
    cout << __PRETTY_FUNCTION__ << this << "both locks taken" << endl;
    return d->impl->isConnected();
#else
    // No locking needed as we just return local atomic state.
    return d->isConnected;
#endif
}

ConnectionType MVLC::connectionType() const
{
    // No need to lock. Impl must guarantee that this access is thread-safe.
    return d->impl->connectionType();
}

std::string MVLC::connectionInfo() const
{
    // No need to lock. Impl must guarantee that this access is thread-safe.
    return d->impl->connectionInfo();
}

u32 MVLC::hardwareId() const
{
    return d->hardwareId;
}

u32 MVLC::firmwareRevision() const
{
    return d->firmwareRevision;
}

std::error_code MVLC::write(Pipe pipe, const u8 *buffer, size_t size,
                      size_t &bytesTransferred)
{
    auto guard = d->locks.lock(pipe);
    return d->resultCheck(d->impl->write(pipe, buffer, size, bytesTransferred));
}

std::error_code MVLC::read(Pipe pipe, u8 *buffer, size_t size,
                     size_t &bytesTransferred)
{
    auto guard = d->locks.lock(pipe);
    return d->resultCheck(d->impl->read(pipe, buffer, size, bytesTransferred));
}

std::error_code MVLC::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto guard = d->locks.lock(pipe);
    return d->resultCheck(d->impl->setWriteTimeout(pipe, ms));
}

std::error_code MVLC::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto guard = d->locks.lock(pipe);
    return d->resultCheck(d->impl->setReadTimeout(pipe, ms));
}

unsigned MVLC::writeTimeout(Pipe pipe) const
{
    auto guard = d->locks.lock(pipe);
    return d->impl->writeTimeout(pipe);
}

unsigned MVLC::readTimeout(Pipe pipe) const
{
    auto guard = d->locks.lock(pipe);
    return d->impl->readTimeout(pipe);
}

void MVLC::setDisableTriggersOnConnect(bool b)
{
    auto guards = d->locks.lockBoth();
    d->impl->setDisableTriggersOnConnect(b);
}

bool MVLC::disableTriggersOnConnect() const
{
    auto guards = d->locks.lockBoth();
    return d->impl->disableTriggersOnConnect();
}

std::error_code MVLC::readRegister(u16 address, u32 &value)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.readRegister(address, value));
}

std::error_code MVLC::writeRegister(u16 address, u32 value)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.writeRegister(address, value));
}

std::error_code MVLC::vmeRead(u32 address, u32 &value, u8 amod,
                              VMEDataWidth dataWidth)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.vmeRead(address, value, amod, dataWidth));
}

std::error_code MVLC::vmeWrite(u32 address, u32 value, u8 amod,
                               VMEDataWidth dataWidth)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.vmeWrite(address, value, amod, dataWidth));
}

std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                             std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.vmeBlockRead(address, amod, maxTransfers, dest));
}

std::error_code MVLC::vmeMBLTSwapped(u32 address, u16 maxTransfers, std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.vmeMBLTSwapped(address, maxTransfers, dest));
}

std::error_code MVLC::uploadStack(
    u8 stackOutputPipe,
    u16 stackMemoryOffset,
    const std::vector<StackCommand> &commands,
    std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.uploadStack(
            stackOutputPipe, stackMemoryOffset, commands, responseDest));
}

std::error_code MVLC::execImmediateStack(
    u16 stackMemoryOffset, std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.execImmediateStack(stackMemoryOffset, responseDest));
}

std::error_code MVLC::readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.readResponse(bhv, dest));
}

std::error_code MVLC::mirrorTransaction(
    const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.mirrorTransaction(cmdBuffer, responseDest));
}

std::error_code MVLC::stackTransaction(const std::vector<u32> &stackUploadData,
                                 std::vector<u32> &responseDest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.stackTransaction(stackUploadData, responseDest));
}

std::error_code MVLC::readKnownBuffer(std::vector<u32> &dest)
{
    auto guard = d->locks.lockCmd();
    return d->resultCheck(d->dialog.readKnownBuffer(dest));
}

std::vector<u32> MVLC::getResponseBuffer() const
{
    auto guard = d->locks.lockCmd();
    return d->dialog.getResponseBuffer();
}

StackErrorCounters MVLC::getStackErrorCounters() const
{
    // Note: this is thread-safe in MVLCDialog
    return d->dialog.getStackErrorCounters();
}

Protected<StackErrorCounters> &MVLC::getProtectedStackErrorCounters()
{
    return d->dialog.getProtectedStackErrorCounters();
}

void MVLC::clearStackErrorCounters()
{
    // Note: this is thread-safe in MVLCDialog
    d->dialog.clearStackErrorCounters();
}

std::unique_lock<Mutex> MVLC::suspendStackErrorPolling()
{
    // Take the mutex so that the poller is forced to block on its next
    // iteration.
    return std::unique_lock<Mutex>(d->errorPollerSuspendMutex);
}

}
}
