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
#ifndef __MESYTEC_MVLC_MVLC_H__
#define __MESYTEC_MVLC_MVLC_H__

#include <memory>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_constants.h"
#include "mvlc_threading.h"
#include "mvlc_command_builders.h"
#include "mvlc_stack_errors.h"
#include "util/protected.h"

namespace mesytec
{
namespace mvlc
{

class MESYTEC_MVLC_EXPORT MVLC: public MVLCBasicInterface
{
    public:
        // Warning: the default constructor creates a MVLC object which is in
        // an invalid state. Calling most methods will result in a crash
        // because the internal Private pointer is set to nullptr.
        // The constructor was added to allow creating an uninitialized MVLC
        // object and later on copy/move a properly constructed MVLC object
        // into it.
        explicit MVLC();

        MVLC(std::unique_ptr<MVLCBasicInterface> &&impl);
        ~MVLC() override;

        MVLC(const MVLC &) = default;
        MVLC &operator=(const MVLC &) = default;

        MVLC(MVLC &&) = default;
        MVLC &operator=(MVLC &&) = default;

        explicit operator bool() const { return d != nullptr; }
        bool isValid() const { return static_cast<bool>(*this); }

        // Contents of the hardware_id register (0x6008)
        u32 hardwareId() const;
        // Contents of the firmware_revision register (0x600e)
        u32 firmwareRevision() const;

        //
        // MVLCBasicInterface
        //
        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;
        ConnectionType connectionType() const override;
        std::string connectionInfo() const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned writeTimeout(Pipe pipe) const override;
        unsigned readTimeout(Pipe pipe) const override;

        void setDisableTriggersOnConnect(bool b) override;
        bool disableTriggersOnConnect() const override;

        //
        // Dialog layer
        //
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

        std::error_code vmeRead(
            u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeWrite(
            u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(
            u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest);

        std::error_code vmeMBLTSwapped(
            u32 address, u16 maxTransfers, std::vector<u32> &dest);

        std::error_code uploadStack(
            u8 stackOutputPipe,
            u16 stackMemoryOffset,
            const std::vector<StackCommand> &commands,
            std::vector<u32> &responseDest);

        inline std::error_code uploadStack(
            u8 stackOutputPipe,
            u16 stackMemoryOffset,
            const StackCommandBuilder &stack,
            std::vector<u32> &responseDest)
        {
            return uploadStack(stackOutputPipe, stackMemoryOffset, stack.getCommands(), responseDest);
        }

        std::error_code execImmediateStack(
            u16 stackMemoryOffset, std::vector<u32> &responseDest);

        inline std::error_code execImmediateStack(std::vector<u32> &responseDest)
        {
            return execImmediateStack(0, responseDest);
        }

        // Read a full response buffer into dest. The buffer header is passed
        // to the validator before attempting to read the rest of the response.
        // If validation fails no more data is read.
        // Note: if stack error notification buffers are received they are made
        // available via getStackErrorNotifications().
        std::error_code readResponse(BufferHeaderValidator bhv, std::vector<u32> &dest);

        // Send the given cmdBuffer to the MVLC, reads and verifies the mirror
        // response. The buffer must start with CmdBufferStart and end with
        // CmdBufferEnd, otherwise the MVLC cannot interpret it.
        std::error_code mirrorTransaction(
            const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest);

        // Sends the given stack data (which must include upload commands),
        // reads and verifies the mirror response, and executes the stack.
        // Note: Stack0 is used and offset 0 into stack memory is assumed.
        std::error_code stackTransaction(const std::vector<u32> &stackUploadData,
                                         std::vector<u32> &responseDest);

        // Low level read accepting any of the known buffer types (see
        // is_known_buffer_header()). Does not do any special handling for
        // stack error notification buffers as is done in readResponse().
        std::error_code readKnownBuffer(std::vector<u32> &dest);

        // Returns the response buffer containing the contents of the last read
        // operation from the MVLC.
        // After mirrorTransaction() the buffer will contain the mirror
        // response. After stackTransaction() the buffer will contain the
        // response from executing the stack.
        std::vector<u32> getResponseBuffer() const;

        //
        // Stack Error Notifications (Command Pipe)
        //


        StackErrorCounters getStackErrorCounters() const;
        Protected<StackErrorCounters> &getProtectedStackErrorCounters();
        void clearStackErrorCounters();

        // Temporarily suspend polling for stack errors. Take the mutex before
        // running a batch of commands (e.g. VME writes from an init sequence)
        // and hold it until all commands are done. This way the stack error
        // poller won't slow down the command execution with reads that run
        // into timeouts.
        std::unique_lock<Mutex> suspendStackErrorPolling();

        void stopStackErrorPolling();
        void startStackErrorPolling();

        //
        // Access to the low-level implementation and the mutexes.
        //

        MVLCBasicInterface *getImpl();

        // Direct access to the per-pipe locks.
        Locks &getLocks();

    private:
        struct Private;
        std::shared_ptr<Private> d;
};

}
}

#endif /* __MESYTEC_MVLC_MVLC_H__ */
