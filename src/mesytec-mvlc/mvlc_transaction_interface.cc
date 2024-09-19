#include "mvlc_transaction_interface.h"

#include "mvlc_core_interface.h"
#include "mvlc_command_builders.h"
#include "mvlc_error.h"
#include "mvlc_usb_interface.h"
#include "util/logging.h"

namespace mesytec::mvlc
{

std::error_code upload_stack(MvlcTransactionInterface *trxImpl,
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

    const size_t PartMaxSize = (trxImpl->getImpl()->connectionType() == ConnectionType::USB
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

        u16 superRef = trxImpl->nextSuperReference();
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

        if (auto ec = trxImpl->superTransaction(super, superResponse))
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

class MvlcCoreInterface;

std::error_code upload_stack(MvlcCoreInterface *mvlc,
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents)
{
    return upload_stack(mvlc->getTransactionImpl(), stackOutputPipe, stackMemoryOffset, stackContents);
}

}
