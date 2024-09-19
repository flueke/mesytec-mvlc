/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020-2023 mesytec GmbH & Co. KG
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

#include <atomic>
#include <fstream>
#include <future>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "firmware_checks.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_error.h"
#include "mvlc_eth_interface.h"
#include "mvlc_usb_interface.h"
#include "util/logging.h"
#include "util/storage_sizes.h"
#include "vme_constants.h"

namespace mesytec::mvlc
{

static const size_t LogBuffersMaxWords = 0; // set to 0 to output the full buffer contents

#if 0
namespace
{


// ============================================
// CmdApi
// ============================================
class CmdApi
{
    public:
        CmdApi(ReaderContext &context)
            : readerContext_(context)
        {}

        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

        std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);
        std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);
        std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);

        std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);
        std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);

        std::error_code uploadStack(u8 stackOutputPipe, u16 stackMemoryOffset,
                                    const std::vector<StackCommand> &commands);

        std::error_code uploadStack(u8 stackOutputPipe, u16 stackMemoryOffset,
                                    const std::vector<u32> &stackContents);

        StackErrorCounters getStackErrorCounters() const
        {
            return readerContext_.stackErrors.copy();
        }

        void resetStackErrorCounters()
        {
            readerContext_.stackErrors.access().ref() = {};
        }

        std::error_code readStackExecStatusRegisters(u32 &status0, u32 &status1);

        std::error_code superTransaction(
            u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer);

        std::error_code stackTransaction(
            u32 stackRef, const StackCommandBuilder &stackBuilder, std::vector<u32> &stackResponse);

        std::error_code superTransactionImpl(
            u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer, unsigned attempt);

        std::error_code stackTransactionImpl(
            u32 stackRef, const StackCommandBuilder &stackBuilder, std::vector<u32> &stackResponse, unsigned attempt);

    private:
        static constexpr std::chrono::milliseconds ResultWaitTimeout = std::chrono::milliseconds(2000);

        ReaderContext &readerContext_;
};

static const unsigned TransactionMaxAttempts = 10;

std::error_code CmdApi::superTransaction(
    u16 ref,
    std::vector<u32> cmdBuffer,
    std::vector<u32> &responseBuffer)
{
    unsigned attempt = 0;
    std::error_code ec;

    do
    {
        if ((ec = superTransactionImpl(ref, cmdBuffer, responseBuffer, attempt++)))
            spdlog::warn("superTransaction failed on attempt {} with error: {}", attempt, ec.message());
    } while (ec && attempt < TransactionMaxAttempts);

    return ec;
}

std::error_code CmdApi::superTransactionImpl(
    u16 ref,
    std::vector<u32> cmdBuffer,
    std::vector<u32> &responseBuffer,
    unsigned attempt)
{
    if (cmdBuffer.size() > MirrorTransactionMaxWords)
        return make_error_code(MVLCErrorCode::MirrorTransactionMaxWordsExceeded);

    auto rf = set_pending_response(readerContext_.pendingSuper, responseBuffer, ref);
    auto tSet = std::chrono::steady_clock::now();

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);

    if (ec)
        return fullfill_pending_response(readerContext_.pendingSuper, ec);

    if (rf.wait_for(ResultWaitTimeout) != std::future_status::ready)
    {
        auto elapsed = std::chrono::steady_clock::now() - tSet;
        get_logger("mvlc_apiv2")->warn(
            "superTransaction super future not ready -> SuperCommandTimeout"
            " (ref=0x{:04x}, timed_out after {}ms (attempt {}/{})",
            readerContext_.pendingSuper.access()->reference,
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
            attempt, TransactionMaxAttempts
            );
        return fullfill_pending_response(readerContext_.pendingSuper, make_error_code(MVLCErrorCode::SuperCommandTimeout));
    }

    return rf.get();
}

std::error_code CmdApi::readStackExecStatusRegisters(u32 &status0, u32 &status1)
{
    auto logger = get_logger("mvlc_apiv2");

    u16 superRef = readerContext_.nextSuperReference++;
    SuperCommandBuilder sb;
    sb.addReferenceWord(superRef);
    sb.addReadLocal(registers::stack_exec_status0);
    sb.addReadLocal(registers::stack_exec_status1);

    std::vector<u32> response;

    if (auto ec = superTransaction(superRef, make_command_buffer(sb), response))
    {
        logger->warn("readStackExecStatusRegisters: superRef={:#06x}, response={:#010x}", superRef, fmt::join(response, ", "));
        return ec;
    }

    logger->info("readStackExecStatusRegisters: superRef={:#06x}, response={:#010x}", superRef, fmt::join(response, ", "));

    assert(response.size() == 6);

    if (response.size() != 6)
        return MVLCErrorCode::ShortSuperFrame; // cannot really happen, would be a firmware bug

    // Response structure:
    //   super header, superRef,   read_status0, stack_exec_status0, read_status1, stack_exec_status1
    //   0xf1000005,   0x010135f0, 0x01021400,   0xf3001ac8,         0x01021404,   0x00001ac8
    status0 = response[3];
    status1 = response[5];

    return {};
}

// TODO: split this up. It's too big with the stack_exec_status checks
std::error_code CmdApi::stackTransaction(
    u32 stackRef, const StackCommandBuilder &stackBuilder,
    std::vector<u32> &stackResponse)
{
    auto logger = get_logger("mvlc_apiv2");
    unsigned attempt = 0;
    std::error_code ec;

    do
    {
        if (attempt > 0)
            logger->info("stackTransaction: begin transaction, stackRef={:#010x}, attempt={}", stackRef, attempt);

        ec = stackTransactionImpl(stackRef, stackBuilder, stackResponse, attempt);

        if ((ec == MVLCErrorCode::SuperCommandTimeout || ec == MVLCErrorCode::StackCommandTimeout)
            && attempt < TransactionMaxAttempts)
        {
            // We did not get a response matching our request. Now read the
            // stack_exec_status registers to figure out if our transaction was
            // executed by the MVLC.
            logger->warn("stackTransaction: stackRef={:#010x}, attempt={} -> {}, checking stack_exec_status registers", stackRef, attempt, ec.message());

            u32 status0 = 0, status1 = 0;

            if ((ec = readStackExecStatusRegisters(status0, status1)))
            {
                logger->warn("stackTransaction: stackRef={:#010x}, attempt={} -> {}, failed reading stack_exec_status registers", stackRef, attempt, ec.message());
                break;
            }

            if (status1 == stackRef)
            {
                // status1 contains our stack reference number which means the
                // MVLC has executed the transaction but the response never
                // arrived. Extract the status flags from the status0 and
                // return an appropriate response code.
                auto frameFlags = extract_frame_flags(status0);

                if (frameFlags & frame_flags::Timeout)
                    ec = MVLCErrorCode::NoVMEResponse;
                else if (frameFlags & frame_flags::BusError)
                    ec = MVLCErrorCode::VMEBusError;
                else if (frameFlags & frame_flags::SyntaxError)
                    ec = MVLCErrorCode::StackSyntaxError;
                else
                    ec = MVLCErrorCode::StackExecResponseLost;

                logger->warn("stackTransaction: stackRef={:#010x}, attempt={}: stack_exec_status1 matches stackRef, frame flags from stack_exec_status0: {}, returning {}",
                    stackRef, attempt, format_frame_flags(frameFlags), ec.message());
            }
            else
            {
                // status1 does not contain our stack reference number => the
                // MVLC did receive the request or somehow did not execute the
                // stack => retry
                ec = MVLCErrorCode::StackExecRequestLost;
                logger->warn("stackTransaction: stackRef={:#010x}, attempt={}: stack_exec_status1 ({:#010x}) does NOT match stackRef, retrying",
                    stackRef, attempt, status1);
            }
        }
    } while (ec && ++attempt < TransactionMaxAttempts);

    logger->log(ec ? spdlog::level::warn : spdlog::level::info, "stackTransaction: stackRef={:#010x}, attempt={}: returning '{}'",
        stackRef, attempt, ec.message());

    return ec;
}

// Perform a stack transaction.
//
// stackRef is the reference word that is produced as part of the
std::error_code CmdApi::stackTransactionImpl(
    u32 stackRef, const StackCommandBuilder &stackBuilder,
    std::vector<u32> &stackResponse,
    unsigned attempt)
{
    if (auto ec = uploadStack(CommandPipe, stacks::ImmediateStackStartOffsetBytes, make_stack_buffer(stackBuilder)))
        return ec;

    u16 superRef = readerContext_.nextSuperReference++;

    SuperCommandBuilder superBuilder;
    superBuilder.addReferenceWord(superRef);
    // New in FW0039: clear stack status registers before executing the stack
    superBuilder.addWriteLocal(registers::stack_exec_status0, 0);
    superBuilder.addWriteLocal(registers::stack_exec_status1, 0);
    // Write the stack offset and trigger registers. The latter triggers the
    // immediate execution of the stack.
    superBuilder.addWriteLocal(stacks::Stack0OffsetRegister, stacks::ImmediateStackStartOffsetBytes);
    superBuilder.addWriteLocal(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);
    auto cmdBuffer = make_command_buffer(superBuilder);

    log_buffer(get_logger("mvlc_apiv2"), spdlog::level::trace,
        cmdBuffer, "stackTransactionImpl: 'exec immediate stack' command buffer", LogBuffersMaxWords);

    std::vector<u32> superResponse;

    auto superFuture = set_pending_response(readerContext_.pendingSuper, superResponse, superRef);
    auto stackFuture = set_pending_response(readerContext_.pendingStack, stackResponse, stackRef);

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);

    // super response
    if (ec)
    {
        // On write error use the same error_code to fullfill both responses.
        fullfill_pending_response(readerContext_.pendingSuper, ec);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    if (superFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
    {
        get_logger("mvlc_apiv2")->warn("stackTransactionImpl: super future still not ready -> SuperCommandTimeout (ref=0x{:04x}, attempt={}/{})",
            readerContext_.pendingSuper.access()->reference,
            attempt, TransactionMaxAttempts);
        ec = make_error_code(MVLCErrorCode::SuperCommandTimeout);
        fullfill_pending_response(readerContext_.pendingSuper, ec);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    if (auto ec = superFuture.get())
        return fullfill_pending_response(readerContext_.pendingStack, ec);

    // stack response
    if (stackFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
    {
        get_logger("mvlc_apiv2")->warn("stackTransactionImpl: stack future still not ready -> StackCommandTimeout (ref=0x{:08x})",
            readerContext_.pendingStack.access()->reference,
            attempt, TransactionMaxAttempts);
        ec = make_error_code(MVLCErrorCode::StackCommandTimeout);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    return stackFuture.get();
}

std::error_code CmdApi::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents)
{
    // Uploading a command stack requires writing the following to the stack memory area:
    // - StackStart with the correct output pipe set
    // - each word of StackContents
    // - StackEnd
    //
    // Limits for the number of stack words that can be uploaded in a single
    // super transaction:
    // ETH is limited by the non-jumbo UDP max payload size. Using 181 stack
    // words per part results in 1+181*2=362 super words (reference word +
    // (write command + payload) for each stack word. If the part is the first
    // and/or last part, StackStart and/or StackEnd also have to be written.
    // Extreme case without ref word: StackStart + 181 words + StackEnd = 183 words.
    // With WriteLocal commands: 183 * 2 + 1 ref word: 367 words * 4 bytes = 1468 bytes.
    static const size_t EthPartMaxSize = 181;

    // USB is theoretically unlimited but there are issues with large buffers
    // (FW0036_11 and earlier): the super response from the MVLC is missing
    // data, e.g. 1619 words are uploaded but the response is missing 1020 words
    // in the middle. The current part size of 768 was determined through trial
    // and error.
    static const size_t UsbPartMaxSize = 768;

    // Update (230928): when continuously writing, the MVLC firmware can handle
    // 256 incoming words at a time, so large buffers would have to be split
    // into max 256 word sized pieces (with buffer start and end words). These
    // pieces could be written out one after the other, without having to wait
    // for each individual response. The incoming responses could be read in
    // parallel while still writing data. The current system with one pending
    // super and one pending stack response can't handle this (I think). As this
    // most likely only affects things like MVP firmware updates over VME the
    // code is not going to be rewritten now.

    const size_t PartMaxSize = (dynamic_cast<usb::MVLC_USB_Interface *>(readerContext_.mvlc)
                                    ? UsbPartMaxSize : EthPartMaxSize);

    auto logger = get_logger("mvlc_uploadStack");
    size_t stackWordsWritten = 0u;
    const auto stackBegin = std::begin(stackContents);
    const auto stackEnd = std::end(stackContents);
    auto partIter = stackBegin;
    u16 writeAddress = stacks::StackMemoryBegin + stackMemoryOffset;
    std::vector<u32> superResponse;
    size_t partCount = 0;

    while (partIter != stackEnd)
    {
        auto partEnd = std::min(partIter + PartMaxSize, stackEnd);

        //basic_string_view<u32> partView(&(*partIter), partEnd - partIter);
        //log_buffer(logger, spdlog::level::trace, partView, "stack part to upload");

        //for (auto tmp=partIter; tmp!=partEnd; ++tmp)
        //    logger->trace("part: 0x{:08x}", *tmp);

        u16 superRef = readerContext_.nextSuperReference++;
        SuperCommandBuilder super;
        super.addReferenceWord(superRef);

        if (partIter == stackBegin)
        {
            if (writeAddress >= stacks::StackMemoryEnd)
                return make_error_code(MVLCErrorCode::StackMemoryExceeded);

            // This is the first part being uploaded -> add the StackStart command.
            super.addWriteLocal(
                writeAddress,
                (static_cast<u32>(StackCommandType::StackStart) << stack_commands::CmdShift
                 | (stackOutputPipe << stack_commands::CmdArg0Shift)));

            writeAddress += AddressIncrement;
        }

        // Add a write for each data word of current part.
        while (partIter != partEnd)
        {
            if (writeAddress >= stacks::StackMemoryEnd)
                return make_error_code(MVLCErrorCode::StackMemoryExceeded);

            super.addWriteLocal(writeAddress, *partIter++);
            writeAddress += AddressIncrement;
            ++stackWordsWritten;
        }

        if (partIter == stackEnd)
        {
            if (writeAddress >= stacks::StackMemoryEnd)
                return make_error_code(MVLCErrorCode::StackMemoryExceeded);

            // This is the final part being uploaded -> add the StackEnd word.
            super.addWriteLocal(
                writeAddress,
                static_cast<u32>(StackCommandType::StackEnd) << stack_commands::CmdShift);
            writeAddress += AddressIncrement;
        }

        auto superBuffer = make_command_buffer(super);
        logger->trace("stack part #{}: superBuffer.size()={}", partCount+1, superBuffer.size());
        //log_buffer(logger, spdlog::level::trace, superBuffer, fmt::format("partial stack upload part {}", partCount+1));
        assert(superBuffer.size() <= MirrorTransactionMaxWords);

        if (auto ec = superTransaction(superRef, superBuffer, superResponse))
        {
            logger->warn("upload superTransaction for part #{} failed: {}", partCount+1, ec.message());
            return ec;
        }
        else
            logger->trace("successful superTransaction for stack part #{}", partCount+1);

        ++partCount;
    }

    assert(partIter == stackEnd);
    logger->trace("stackWordsWritten={}, stackContents.size()={}, partCount={}",
                 stackWordsWritten, stackContents.size(), partCount);
    assert(stackWordsWritten == stackContents.size());

    return {};
}

std::error_code CmdApi::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands)
{
    return uploadStack(stackOutputPipe, stackMemoryOffset, make_stack_buffer(commands));
}

std::error_code CmdApi::readRegister(u16 address, u32 &value)
{
    auto logger = get_logger("mvlc");

    u16 ref = readerContext_.nextSuperReference++;
    logger->info("readRegister(addr=0x{:04x}): superRef=0x{:04x}", address, value, ref);

    SuperCommandBuilder scb;
    scb.addReferenceWord(ref);
    scb.addReadLocal(address);
    auto cmdBuffer = make_command_buffer(scb);
    std::vector<u32> responseBuffer;

    if (auto ec = superTransaction(ref, cmdBuffer, responseBuffer))
        return ec;

    if (responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    value = responseBuffer[3];

    return {};
}

std::error_code CmdApi::writeRegister(u16 address, u32 value)
{
    auto logger = get_logger("mvlc");

    u16 ref = readerContext_.nextSuperReference++;
    logger->info("writeRegister(addr=0x{:04x}, value={}): superRef=0x{:04x}", address, value, ref);

    SuperCommandBuilder scb;
    scb.addReferenceWord(ref);
    scb.addWriteLocal(address, value);
    auto cmdBuffer = make_command_buffer(scb);
    std::vector<u32> responseBuffer;

    if (auto ec = superTransaction(ref, cmdBuffer, responseBuffer))
        return ec;

    logger->info("writeRegister(a=0x{:04x}, value={}, superRef=0x{:04x}): response buffer: {:#010x} ", address, value, ref, fmt::join(responseBuffer, ", "));

    if (responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code CmdApi::vmeRead(
    u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
{
    auto logger = get_logger("mvlc");

    u32 stackRef = readerContext_.nextStackReference++;

    logger->info("vmeRead(addr=0x{:04x}, amod=0x{:02x}): stackRef=0x{:08x}", address, std::byte(amod), stackRef);

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMERead(address, amod, dataWidth);

    std::vector<u32> stackResponse;

    if (auto ec = stackTransaction(stackRef, stackBuilder, stackResponse))
    {
        value = 0;
        return ec;
    }

    log_buffer(get_logger("mvlc"), spdlog::level::trace, stackResponse, "vmeRead(): stackResponse", LogBuffersMaxWords);

    if (stackResponse.size() != 3)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    auto frameFlags = extract_frame_flags(stackResponse[0]);

    if (frameFlags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    if (frameFlags & frame_flags::BusError)
        return MVLCErrorCode::VMEBusError;

    if (frameFlags & frame_flags::SyntaxError)
        return MVLCErrorCode::StackSyntaxError;

    const u32 Mask = (dataWidth == VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

    value = stackResponse[2] & Mask;

    return {};
}

std::error_code CmdApi::vmeWrite(
    u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto logger = get_logger("mvlc");

    u32 stackRef = readerContext_.nextStackReference++;

    logger->info("vmeWrite(addr=0x{:04x}, value=0x{:08x}, amod=0x{:02x}): stackRef=0x{:08x}", address, value, std::byte(amod), stackRef);

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEWrite(address, value, amod, dataWidth);

    std::vector<u32> stackResponse;

    if (auto ec = stackTransaction(stackRef, stackBuilder, stackResponse))
        return ec;

    log_buffer(get_logger("mvlc"), spdlog::level::trace, stackResponse, "vmeWrite(): stackResponse", LogBuffersMaxWords);

    if (stackResponse.size() != 2)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    auto frameFlags = extract_frame_flags(stackResponse[0]);

    if (frameFlags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    if (frameFlags & frame_flags::BusError)
        return MVLCErrorCode::VMEBusError;

    if (frameFlags & frame_flags::SyntaxError)
        return MVLCErrorCode::StackSyntaxError;

    return {};
}

std::error_code CmdApi::vmeBlockRead(
    u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    if (!vme_amods::is_block_mode(amod))
        return make_error_code(MVLCErrorCode::NonBlockAddressMode);

    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEBlockRead(address, amod, maxTransfers, fifo);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockRead(): stackResponse", LogBuffersMaxWords);

    if (!dest.empty())
    {
        auto frameFlags = extract_frame_flags(dest[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;
    }

    return {};
}

std::error_code CmdApi::vmeBlockRead(
    u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEBlockRead(address, rate, maxTransfers, fifo);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockRead(): stackResponse", LogBuffersMaxWords);

    if (!dest.empty())
    {
        auto frameFlags = extract_frame_flags(dest[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;
    }

    return {};
}

std::error_code CmdApi::vmeBlockReadSwapped(
    u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEBlockReadSwapped(address, amod, maxTransfers, fifo);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockReadSwapped(): stackResponse", LogBuffersMaxWords);

    if (!dest.empty())
    {
        auto frameFlags = extract_frame_flags(dest[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;
    }

    return {};
}

std::error_code CmdApi::vmeBlockReadSwapped(
    u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEBlockReadSwapped(address, rate, maxTransfers, fifo);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockReadSwapped(): stackResponse", LogBuffersMaxWords);

    if (!dest.empty())
    {
        auto frameFlags = extract_frame_flags(dest[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;
    }

    return {};
}

} // end anon namespace
#endif

// ============================================
// MVLC
// ============================================
// TODO: isConnected initial state, handling of hardware and firmware revision shadow registers.
struct MVLC::Private
{
    mutable Locks locks_;
    std::unique_ptr<MvlcTransactionInterface> trxImpl_;
    MvlcBasicInterface *impl_ = nullptr;

    // Cached data
    std::atomic<bool> isConnected_;
    std::atomic<u32> hardwareId_;
    std::atomic<u32> firmwareRevision_;

    explicit Private(std::unique_ptr<MvlcTransactionInterface> &&trxImpl)
        : trxImpl_(std::move(trxImpl))
        , impl_(trxImpl_->getImpl())
        , isConnected_(false)
        , hardwareId_(0)
        , firmwareRevision_(0)
    { }

    ~Private()
    { }

    std::error_code resultCheck(const std::error_code &ec)
    {
        // Update the local connection state without calling
        // impl->isConnected() which would require us to take both pipe locks.
        if (ec == ErrorType::ConnectionError)
            isConnected_ = false;
        return ec;
    }

    std::error_code readRegister(u16 address, u32 &value)
    {
        auto logger = get_logger("mvlc");

        u16 ref = trxImpl_->nextSuperReference();
        logger->info("readRegister(addr=0x{:04x}): superRef=0x{:04x}", address, value, ref);

        SuperCommandBuilder scb;
        scb.addReferenceWord(ref);
        scb.addReadLocal(address);
        auto cmdBuffer = make_command_buffer(scb);
        std::vector<u32> responseBuffer;

        if (auto ec = trxImpl_->superTransaction(scb, responseBuffer))
            return ec;

        if (responseBuffer.size() != 4)
            return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

        value = responseBuffer[3];

        return {};
    }

    std::error_code writeRegister(u16 address, u32 value)
    {
        auto logger = get_logger("mvlc");

        u16 ref = trxImpl_->nextSuperReference();
        logger->info("writeRegister(addr=0x{:04x}, value={}): superRef=0x{:04x}", address, value, ref);

        SuperCommandBuilder scb;
        scb.addReferenceWord(ref);
        scb.addWriteLocal(address, value);
        auto cmdBuffer = make_command_buffer(scb);
        std::vector<u32> responseBuffer;

        if (auto ec = trxImpl_->superTransaction(scb, responseBuffer))
            return ec;

        logger->info("writeRegister(a=0x{:04x}, value={}, superRef=0x{:04x}): response buffer: {:#010x} ", address, value, ref, fmt::join(responseBuffer, ", "));

        if (responseBuffer.size() != 4)
            return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

        return {};
    }

    std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
    {
        auto logger = get_logger("mvlc");

        u32 stackRef = trxImpl_->nextStackReference();

        logger->info("vmeRead(addr=0x{:04x}, amod=0x{:02x}): stackRef=0x{:08x}", address, std::byte(amod), stackRef);

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMERead(address, amod, dataWidth);

        std::vector<u32> stackResponse;

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, stackResponse))
        {
            value = 0;
            return ec;
        }

        log_buffer(get_logger("mvlc"), spdlog::level::trace, stackResponse, "vmeRead(): stackResponse", LogBuffersMaxWords);

        if (stackResponse.size() != 3)
            return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

        auto frameFlags = extract_frame_flags(stackResponse[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;

        const u32 Mask = (dataWidth == VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

        value = stackResponse[2] & Mask;

        return {};
    }

    std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
    {
        auto logger = get_logger("mvlc");

        u32 stackRef = trxImpl_->nextStackReference();

        logger->info("vmeWrite(addr=0x{:04x}, value=0x{:08x}, amod=0x{:02x}): stackRef=0x{:08x}", address, value, std::byte(amod), stackRef);

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMEWrite(address, value, amod, dataWidth);

        std::vector<u32> stackResponse;

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, stackResponse))
            return ec;

        log_buffer(get_logger("mvlc"), spdlog::level::trace, stackResponse, "vmeWrite(): stackResponse", LogBuffersMaxWords);

        if (stackResponse.size() != 2)
            return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

        auto frameFlags = extract_frame_flags(stackResponse[0]);

        if (frameFlags & frame_flags::Timeout)
            return MVLCErrorCode::NoVMEResponse;

        if (frameFlags & frame_flags::BusError)
            return MVLCErrorCode::VMEBusError;

        if (frameFlags & frame_flags::SyntaxError)
            return MVLCErrorCode::StackSyntaxError;

        return {};
    }

    std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
    {
        if (!vme_amods::is_block_mode(amod))
            return make_error_code(MVLCErrorCode::NonBlockAddressMode);

        u32 stackRef = trxImpl_->nextStackReference();

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMEBlockRead(address, amod, maxTransfers, fifo);

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, dest))
            return ec;

        log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockRead(): stackResponse", LogBuffersMaxWords);

        if (!dest.empty())
        {
            auto frameFlags = extract_frame_flags(dest[0]);

            if (frameFlags & frame_flags::Timeout)
                return MVLCErrorCode::NoVMEResponse;

            if (frameFlags & frame_flags::BusError)
                return MVLCErrorCode::VMEBusError;

            if (frameFlags & frame_flags::SyntaxError)
                return MVLCErrorCode::StackSyntaxError;
        }

        return {};
    }

    std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
    {
        u32 stackRef = trxImpl_->nextStackReference();

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMEBlockRead(address, rate, maxTransfers, fifo);

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, dest))
            return ec;

        log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockRead(): stackResponse", LogBuffersMaxWords);

        if (!dest.empty())
        {
            auto frameFlags = extract_frame_flags(dest[0]);

            if (frameFlags & frame_flags::Timeout)
                return MVLCErrorCode::NoVMEResponse;

            if (frameFlags & frame_flags::BusError)
                return MVLCErrorCode::VMEBusError;

            if (frameFlags & frame_flags::SyntaxError)
                return MVLCErrorCode::StackSyntaxError;
        }

        return {};
    }

    std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
    {
        u32 stackRef = trxImpl_->nextStackReference();

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMEBlockReadSwapped(address, amod, maxTransfers, fifo);

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, dest))
            return ec;

        log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockReadSwapped(): stackResponse", LogBuffersMaxWords);

        if (!dest.empty())
        {
            auto frameFlags = extract_frame_flags(dest[0]);

            if (frameFlags & frame_flags::Timeout)
                return MVLCErrorCode::NoVMEResponse;

            if (frameFlags & frame_flags::BusError)
                return MVLCErrorCode::VMEBusError;

            if (frameFlags & frame_flags::SyntaxError)
                return MVLCErrorCode::StackSyntaxError;
        }

        return {};
    }

    std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
    {
        u32 stackRef = trxImpl_->nextStackReference();

        StackCommandBuilder stackBuilder;
        stackBuilder.addWriteMarker(stackRef);
        stackBuilder.addVMEBlockReadSwapped(address, rate, maxTransfers, fifo);

        if (auto ec = trxImpl_->stackTransaction(stackBuilder, dest))
            return ec;

        log_buffer(get_logger("mvlc"), spdlog::level::trace, dest, "vmeBlockReadSwapped(): stackResponse", LogBuffersMaxWords);

        if (!dest.empty())
        {
            auto frameFlags = extract_frame_flags(dest[0]);

            if (frameFlags & frame_flags::Timeout)
                return MVLCErrorCode::NoVMEResponse;

            if (frameFlags & frame_flags::BusError)
                return MVLCErrorCode::VMEBusError;

            if (frameFlags & frame_flags::SyntaxError)
                return MVLCErrorCode::StackSyntaxError;
        }

        return {};
    }
};

MVLC::MVLC()
    : d(nullptr)
{
}

MVLC::MVLC(std::unique_ptr<MvlcTransactionInterface> &&impl)
    : d(std::make_shared<Private>(std::move(impl)))
{
}

MVLC::~MVLC()
{
}

MvlcTransactionInterface *MVLC::getTransactionImpl()
{
    return d->trxImpl_.get();
}

u16 MVLC::nextSuperReference()
{
    return d->trxImpl_->nextSuperReference();
}

u32 MVLC::nextStackReference()
{
    return d->trxImpl_->nextStackReference();
}

CmdPipeCounters MVLC::getCmdPipeCounters() const
{
    return d->trxImpl_->getCmdPipeCounters();
}

StackErrorCounters MVLC::getStackErrorCounters() const
{
    return d->trxImpl_->getStackErrorCounters();
}

void MVLC::resetStackErrorCounters()
{
    d->trxImpl_->resetStackErrorCounters();
}

std::error_code MVLC::connect()
{
    auto logger = get_logger("mvlc");
    auto guards = d->locks_.lockBoth();
    d->isConnected_ = d->impl_->isConnected();
    std::error_code ec;

    if (!isConnected())
    {
        if (!d->impl_->isConnected())
            ec = d->impl_->connect();

        d->isConnected_ = d->impl_->isConnected();

        if (ec)
        {
            logger->error("MVLC::connect(): {}", ec.message());
            return ec;
        }

        // At this point the cmd_pipe_reader is running and thus the CmdApi can
        // be used for communication. Direct methods like MVLC::readRegister()
        // cannot be used as those take the cmd lock which we are still holding.

        // Read hardware id and firmware revision.
        u32 hwId = 0;
        u32 fwRev = 0;

        logger->debug("reading hardware_id register");
        if (auto ec = d->readRegister(registers::hardware_id, hwId))
        {
            logger->error("error reading hardware_id register: {}", ec.message());
            d->isConnected_ = false;
            return ec;
        }

        logger->debug("reading firmware_revision register");
        if (auto ec = d->readRegister(registers::firmware_revision, fwRev))
        {
            logger->error("error reading firmware_revision register: {}", ec.message());
            d->isConnected_ = false;
            return ec;
        }

        d->hardwareId_ = hwId;
        d->firmwareRevision_ = fwRev;

        if (!firmware_checks::is_recommended_firmware(hwId, fwRev))
        {
            logger->warn("Firmware FW{:04x} is recommended to be used with this module, found FW{:04x}",
                firmware_checks::minimum_recommended_firmware(hwId), fwRev);
        }

        logger->info("Connected to MVLC ({}, firmware=FW{:04x})", connectionInfo(), firmwareRevision());
    }
    else
    {
        return make_error_code(MVLCErrorCode::IsConnected);
    }

    return ec;
}

std::error_code MVLC::disconnect()
{
    auto logger = get_logger("mvlc");
    auto guards = d->locks_.lockBoth();

    std::error_code ec;

    if (d->impl_->isConnected())
    {
        auto conInfo = connectionInfo();

        ec = d->impl_->disconnect();

        if (ec)
            logger->error("Error disconnecting from MVLC ({}): {}", conInfo, ec.message());
        else
            logger->info("Disconnected from MVLC ({})", conInfo);
    }

    d->isConnected_ = d->impl_->isConnected();

    return ec;
}

bool MVLC::isConnected() const
{
    return d->isConnected_;
}

ConnectionType MVLC::connectionType() const
{
    // No need to lock. Impl must guarantee that this access is thread-safe.
    return d->impl_->connectionType();
}

std::string MVLC::connectionInfo() const
{
    // No need to lock. Impl must guarantee that this access is thread-safe.
    return d->impl_->connectionInfo();
}

u32 MVLC::hardwareId() const
{
    return d->hardwareId_;
}

u32 MVLC::firmwareRevision() const
{
    return d->firmwareRevision_;
}

unsigned MVLC::getStackCount() const
{
    if (firmwareRevision() < 0x0037u)
        return stacks::StackCountPreFW0037;

    return stacks::StackCount;
}

void MVLC::setDisableTriggersOnConnect(bool b)
{
    auto guards = d->locks_.lockBoth();
    d->impl_->setDisableTriggersOnConnect(b);
}

bool MVLC::disableTriggersOnConnect() const
{
    auto guards = d->locks_.lockBoth();
    return d->impl_->disableTriggersOnConnect();
}

// internal register and vme api
std::error_code MVLC::readRegister(u16 address, u32 &value)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->readRegister(address, value));
}

std::error_code MVLC::writeRegister(u16 address, u32 value)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->writeRegister(address, value));
}


std::error_code MVLC::vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeRead(address, value, amod, dataWidth));
}

std::error_code MVLC::vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeWrite(address, value, amod, dataWidth));
}


std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeBlockRead(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeBlockRead(address, rate, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeBlockReadSwapped(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->vmeBlockReadSwapped(address, rate, maxTransfers, dest, fifo));
}

std::error_code MVLC::uploadStack(
    u8 stackOutputPipe,
    u16 stackMemoryOffset,
    const std::vector<u32> &stackContents)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(upload_stack(this, stackOutputPipe, stackMemoryOffset, stackContents));
}

//MvlcBasicInterface *MVLC::getImpl()
//{
//    return d->impl_.get();
//}

Locks &MVLC::getLocks()
{
    return d->locks_;
}

std::error_code MVLC::superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest)
{
    assert(!superBuilder.empty() && superBuilder[0].type == SuperCommandType::ReferenceWord);

    if (superBuilder.empty())
        return make_error_code(MVLCErrorCode::SuperFormatError);

    if (superBuilder[0].type != SuperCommandType::ReferenceWord)
        return make_error_code(MVLCErrorCode::SuperFormatError);

    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->trxImpl_->superTransaction(superBuilder, dest));
}

std::error_code MVLC::stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest)
{
    using CommandType = StackCommand::CommandType;

    assert(!stackBuilder.empty() && stackBuilder[0].type == CommandType::WriteMarker);

    if (stackBuilder.empty())
        return make_error_code(MVLCErrorCode::StackFormatError);

    if (stackBuilder[0].type != CommandType::WriteMarker)
        return make_error_code(MVLCErrorCode::StackFormatError);

    u32 stackRef = stackBuilder[0].value;

    auto guard = d->locks_.lockCmd();
    auto logger = get_logger("mvlc");
    logger->info("stackTransaction(name={}): stackRef=0x{:08x}", stackBuilder.getName(), stackRef);
    return d->resultCheck(d->trxImpl_->stackTransaction(stackBuilder, dest));
}

std::error_code MVLC::enableJumboFrames(bool b)
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    return writeRegister(registers::jumbo_frame_enable, static_cast<u32>(b));
}

std::pair<bool, std::error_code> MVLC::jumboFramesEnabled()
{
    if (!isConnected())
        return std::make_pair(false, make_error_code(MVLCErrorCode::IsDisconnected));

    u32 value = 0u;
    auto ec = readRegister(registers::jumbo_frame_enable, value);

    return std::make_pair(static_cast<bool>(value), ec);
}

// Experimental: interact with the low level future/promise/cmd_pipe_reader system.
//std::future<std::error_code> MVLC::setPendingStackResponse(std::vector<u32> &dest, u32 stackRef)
//{
//    auto guard = d->locks_.lockCmd();
//    return set_pending_response(d->readerContext_.pendingStack, dest, stackRef);
//}

std::error_code MESYTEC_MVLC_EXPORT redirect_eth_data_stream(MVLC &mvlc)
{
    if (mvlc.connectionType() != ConnectionType::ETH)
        return {};

    static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
    size_t bytesTransferred = 0;

    auto dataGuard = mvlc.getLocks().lockData();

    return mvlc.getImpl()->write(
            Pipe::Data,
            reinterpret_cast<const u8 *>(EmptyRequest.data()),
            EmptyRequest.size() * sizeof(u32),
            bytesTransferred);
}

}
