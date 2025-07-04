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
namespace
{

static const size_t LogBuffersMaxWords = 0; // set to 0 to output the full buffer contents

struct PendingResponse
{
    std::promise<std::error_code> promise;
    std::function<void (const u32 *, size_t)> consume;
    u32 reference = 0;
    bool pending = false;
};

struct ReaderContext
{
    MVLCBasicInterface *mvlc;
    std::atomic<bool> quit;
    std::atomic<u16> nextSuperReference;
    std::atomic<u32> nextStackReference;
    WaitableProtected<PendingResponse> pendingSuper;
    WaitableProtected<PendingResponse> pendingStack;

    Protected<StackErrorCounters> stackErrors;
    Protected<CmdPipeCounters> counters;

    explicit ReaderContext(MVLCBasicInterface *mvlc_)
        : mvlc(mvlc_)
        , quit(false)
        , nextSuperReference(1)
        , nextStackReference(1)
        , stackErrors()
        , counters()
        {}
};

std::error_code fullfill_pending_response(
    PendingResponse &pr,
    const std::error_code &ec,
    const u32 *contents = nullptr, size_t len = 0)
{
    if (pr.pending)
    {
        pr.pending = false;
        pr.consume(contents, len);
        pr.promise.set_value(ec);
    }

    return ec;
}

std::error_code fullfill_pending_response(
    WaitableProtected<PendingResponse> &pr,
    const std::error_code &ec,
    const u32 *contents = nullptr, size_t len = 0)
{
    return fullfill_pending_response(pr.access().ref(), ec, contents, len);
}

std::future<std::error_code> set_pending_response(
    WaitableProtected<PendingResponse> &pending,
    std::function<void (const u32 *, size_t)> consume,
    u32 reference)
{
    auto pendingResponse = pending.wait(
        [] (const PendingResponse &pr) { return !pr.pending; });

    assert(pendingResponse.ref().pending == false);

    std::promise<std::error_code> promise;
    auto result = promise.get_future();

    pendingResponse.ref() = { std::move(promise), consume, reference, true };

    return result;
}

std::future<std::error_code> set_pending_response(
    WaitableProtected<PendingResponse> &pending,
    std::vector<u32> &dest,
    u32 reference)
{
    auto consume = [d=&dest] (const u32 *data, size_t len)
    {
        std::copy(data, data+len, std::back_inserter(*d));
    };

    return set_pending_response(pending, consume, reference);
}

std::future<std::error_code> set_pending_response(
    WaitableProtected<PendingResponse> &pending,
    util::span<u32> &dest,
    u32 reference)
{
    auto consume = [dest] (const u32 *data, size_t len)
    {
        std::copy(data, data+std::min(len, dest.size()), dest.begin());
    };

    return set_pending_response(pending, consume, reference);
}

void cmd_pipe_reader(ReaderContext &context)
{
    struct Buffer
    {
        std::vector<u32> mem;
        size_t start = 0;
        size_t used = 0;

        const u32 *begin() const { return mem.data() + start; }
        u32 *begin() { return mem.data() + start; }

        const u32 *end() const { return begin() + used; }
        u32 *end() { return begin() + used; }

        bool empty() const { return used == 0; }
        size_t size() const { return used; }
        size_t capacity() const { return mem.size(); }
        size_t free() const
        {
            return (mem.data() + mem.size()) - (mem.data() + start + used);
        }

        u32 *writeBegin() { return end(); }
        u32 *writeEnd() { return mem.data() + mem.size(); }


        void consume(size_t nelements)
        {
            assert(used >= nelements);
            start += nelements;
            used -= nelements;
        }

        void use(size_t nelements)
        {
            assert(free() >= nelements);
            used += nelements;
        }

        void pack()
        {
            if (start > 0)
            {
                size_t oldFree = free(); (void) oldFree;
                std::copy(begin(), end(), mem.data());
                start = 0;
                assert(free() > oldFree);
                assert(begin() == mem.data());
            }
        }

        void resize(size_t size)
        {
            if (size > mem.size())
            {
                mem.resize(size);
                pack();
            }
        };

        void ensureFreeSpace(size_t size)
        {
            if (free() < size)
            {
                pack();

                if (free() < size)
                {
                    mem.resize(mem.size() + size);
                }
            }

            assert(free() >= size);
        }

        const u32 &operator[](size_t index) const
        {
            return mem[index + start];
        };

        nonstd::basic_string_view<const u32> viewU32() const
        {
            return nonstd::basic_string_view<const u32>(begin(), size());
        }
    };

    auto logger = get_logger("cmd_pipe_reader");

    auto is_good_header = [] (const u32 header) -> bool
    {
        return is_super_buffer(header)
            || is_super_buffer_continuation(header)
            || is_stack_buffer(header)
            || is_stack_buffer_continuation(header)
            || is_stackerror_notification(header);
    };

    auto contains_complete_frame = [&] (const u32 *begin, const u32 *const end) -> bool
    {
        assert(end - begin >= 0);

        if (end - begin == 0)
        {
            logger->warn("contains_complete_frame: empty data given -> returning false");
            assert(!"do not call me with empty data!");
            return false;
        }

        assert(is_good_header(*begin));

        auto frameInfo = extract_frame_info(*begin);
        auto avail = end - begin;

        if (frameInfo.len + 1 > avail)
            return false;

        while (frameInfo.flags & frame_flags::Continue)
        {
            begin += frameInfo.len + 1;
            avail = end - begin;

            if (avail <= 0)
                return false;

            u32 header = *begin;

            if (!is_good_header(header))
            {
                logger->warn("cmd_pipe_reader: contains_complete_frame: landed on bad header word: 0x{:08x}, avail={}", header, avail);
                auto buffer = basic_string_view<u32>(begin, std::distance(begin, end));
                //log_buffer(logger, spdlog::level::warn, buffer, "cmd_pipe_reader read buffer", LogBuffersMaxWords);
                logger->trace("cmd_pipe_reader read buffer: {:#010x}", fmt::join(buffer, ", "));
                assert(!"bad header word");
                return false; // FIXME: have to empty the internal buffer as we otherwise will remain in this state and continously hit the bad header word
            }

            frameInfo = extract_frame_info(header);

            if (frameInfo.len + 1 > avail)
                return false;
        }

        return true;
    };

#define CMD_PIPE_RECORD_DATA 0
#define CMD_PIPE_RECORD_FILE "cmd_pipe_stream.dat"

#ifdef __linux__
    prctl(PR_SET_NAME,"cmd_pipe_reader",0,0,0);
#endif

    logger->debug("cmd_pipe_reader starting");

    auto mvlcUsb = dynamic_cast<usb::MVLC_USB_Interface *>(context.mvlc);
    auto mvlcEth = dynamic_cast<eth::MVLC_ETH_Interface *>(context.mvlc);

    assert(mvlcUsb || mvlcEth);

    std::error_code ec;
    Buffer buffer;
    buffer.ensureFreeSpace(util::Megabytes(1)/sizeof(u32));

#if CMD_PIPE_RECORD_DATA
    std::ofstream recordOut(CMD_PIPE_RECORD_FILE, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    if (recordOut.fail())
        throw std::runtime_error("Error opening cmd_pipe_reader debug file");
#endif
    bool logNextIncomplete = false;

    while (!context.quit.load(std::memory_order_relaxed))
    {
        while (buffer.used && !context.quit.load(std::memory_order_relaxed))
        {
            auto countersAccess = context.counters.access();
            auto &counters = countersAccess.ref();

            size_t skippedWords = 0u;

            while (!buffer.empty() && !is_good_header(buffer[0]))
            {
                auto word = buffer[0];
                buffer.consume(1);
                ++counters.invalidHeaders;
                ++counters.wordsSkipped;
                ++skippedWords;
                logger->warn("cmd_pipe_reader: skipped over non-good header word 0x{:08x}", word);
            }

            if (skippedWords)
            {
                logger->warn("cmd_pipe_reader: skipped {} non-good header words, words left in buffer: {}",
                             skippedWords, buffer.used);
            }

            if (buffer.empty())
                continue;

            if (contains_complete_frame(buffer.begin(), buffer.end()))
            {
                logger->trace("cmd_pipe_reader: received complete frame: {:#010x}", fmt::join(buffer.viewU32(), ", "));


                logNextIncomplete = true;

                if (is_stackerror_notification(buffer[0]))
                {
                    ++counters.errorBuffers;

                    const auto frameLength = get_frame_length(buffer[0]);
                    auto frameBegin = buffer.begin();
                    auto frameEnd = buffer.begin() + frameLength + 1;

                    update_stack_error_counters(
                        context.stackErrors.access().ref(),
                        basic_string_view<u32>(frameBegin, frameEnd-frameBegin));

                    buffer.consume(frameLength + 1);
                    logger->trace("handled stack error notification frame of length {}", frameLength);
                }
                // super buffers
                else if (is_super_buffer(buffer[0]))
                {
                    const auto frameLength = get_frame_length(buffer[0]);

                    ++counters.superBuffers;

                    auto pendingResponse = context.pendingSuper.access();
                    size_t toConsume = 0;
                    std::error_code ec;

                    if (frameLength == 0)
                    {
                        logger->warn("cmd_pipe_reader: short super frame, consuming frame header");
                        ec = make_error_code(MVLCErrorCode::ShortSuperFrame);
                        ++counters.shortSuperBuffers;
                        toConsume = 1;
                    }
                    else
                    {
                        using namespace super_commands;

                        toConsume += frameLength + 1;

                        u32 refCmd = buffer[1];

                        if (((refCmd >> SuperCmdShift) & SuperCmdMask) != static_cast<u32>(SuperCommandType::ReferenceWord))
                        {
                            logger->warn("cmd_pipe_reader: super buffer does not start with ref command");
                            ec = make_error_code(MVLCErrorCode::SuperFormatError);
                            ++counters.superFormatErrors;
                        }
                        else
                        {
                            u32 superRef = buffer[1] & SuperCmdArgMask;

                            if (superRef != pendingResponse->reference)
                            {
                                logger->warn("cmd_pipe_reader: super ref mismatch, wanted=0x{:04x}, got=0x{:04x}",
                                             pendingResponse->reference, superRef);
                                logger->warn("cmd_pipe_reader: input buffer before super ref mismatch: {#:010x}", fmt::join(buffer.viewU32(), ", "));
                                ec = make_error_code(MVLCErrorCode::SuperReferenceMismatch);
                                ++counters.superRefMismatches;
                            }
                        }
                    }

                    const u32 *header = &buffer[0];
                    auto frameInfo = extract_frame_info(*header);

                    while (frameInfo.flags & frame_flags::Continue)
                    {
                        header += frameInfo.len + 1;
                        frameInfo = extract_frame_info(*header);
                        toConsume += frameInfo.len + 1;
                    }

                    if (ec)
                        fullfill_pending_response(
                            pendingResponse.ref(), ec);
                    else
                        fullfill_pending_response(
                            pendingResponse.ref(), ec, &buffer[0], toConsume);

                    buffer.consume(toConsume);
                }
                // stack buffers
                else if (is_stack_buffer(buffer[0]))
                {
                    ++counters.stackBuffers;

                    auto pendingResponse = context.pendingStack.access();

                    if (!pendingResponse->pending)
                    {
                        logger->error("cmd_pipe_reader: received stack buffer without an active stack transaction! {:#010x}", fmt::join(buffer.viewU32(), ", "));
                    }

                    size_t toConsume = 0;
                    std::error_code ec;

                    if (get_frame_length(buffer[0]) == 0)
                    {
                        ec = make_error_code(MVLCErrorCode::StackFormatError);
                        toConsume = 1;
                    }
                    else
                    {
                        toConsume += get_frame_length(buffer[0]) + 1;

                        u32 stackRef = buffer[1];

                        if (stackRef != pendingResponse->reference)
                        {
                            logger->warn("cmd_pipe_reader: stack ref mismatch, wanted=0x{:08x}, got=0x{:08x}",
                                         pendingResponse->reference, stackRef);
                            logger->warn("cmd_pipe_reader: input buffer before stack ref mismatch: 0x{:08x}", fmt::join(buffer.viewU32(), ", "));
                            ec = make_error_code(MVLCErrorCode::StackReferenceMismatch);
                            ++counters.stackRefMismatches;
                        }
                    }

                    const u32 *header = &buffer[0];
                    auto frameInfo = extract_frame_info(*header);

                    while (frameInfo.flags & frame_flags::Continue)
                    {
                        header += frameInfo.len + 1;
                        frameInfo = extract_frame_info(*header);
                        toConsume += frameInfo.len + 1;
                    }

                    if (ec)
                        fullfill_pending_response(
                            pendingResponse.ref(), ec);
                    else
                        fullfill_pending_response(
                            pendingResponse.ref(), ec, &buffer[0], toConsume);

                    buffer.consume(toConsume);
                }
                else
                    // Should not happen because of the is_good_header() check above.
                    assert(!"cmd_pipe_reader: unknown frame in buffer");
            }
            else
            {
                if (logNextIncomplete && logger->should_log(spdlog::level::trace))
                {
                    // No complete frame in the buffer
                    auto pendingSuper = context.pendingSuper.access();
                    auto pendingStack = context.pendingStack.access();
                    logger->trace("cmd_pipe_reader: incomplete frame in buffer, trying to read more data "
                        "(pendingSuper: pending={}, ref=0x{:04x}, pendingStack: pending={}, ref=0x{:08x})",
                        pendingSuper->pending, pendingSuper->reference,
                        pendingStack->pending, pendingStack->reference);
                    logNextIncomplete = false;
                    //log_buffer(logger, spdlog::level::trace, buffer, "cmd_pipe_reader incomplete frame", LogBuffersMaxWords);
                    logger->trace("cmd_pipe_reader: incomplete frame: {:#010x}", fmt::join(buffer.viewU32(), ", "));
                }
                break; // break out of the loop to read more data below
            }
        }

        if (context.quit.load(std::memory_order_relaxed))
            break;

        size_t bytesTransferred = 0;

        if (mvlcUsb)
        {
            if (buffer.free() < usb::USBSingleTransferMaxWords)
                buffer.ensureFreeSpace(usb::USBSingleTransferMaxWords);

            ec = context.mvlc->read(
                Pipe::Command,
                reinterpret_cast<u8 *>(buffer.writeBegin()),
                std::min(buffer.free() * sizeof(u32), usb::USBSingleTransferMaxBytes),
                bytesTransferred);

            buffer.used += bytesTransferred / sizeof(u32);

#if CMD_PIPE_RECORD_DATA
            auto partBegin = reinterpret_cast<const char *>(buffer.end()) - bytesTransferred;
            recordOut.write(partBegin, bytesTransferred);
#endif
        }
        else if (mvlcEth)
        {
            if (buffer.free() < eth::JumboFrameMaxSize / sizeof(u32))
                buffer.ensureFreeSpace(eth::JumboFrameMaxSize / sizeof(u32));

            std::array<u8, eth::JumboFrameMaxSize> packetBuffer;

            auto packet = mvlcEth->read_packet(
                Pipe::Command,
                packetBuffer.data(),
                packetBuffer.size());

            ec = packet.ec;
            bytesTransferred += packet.bytesTransferred; // This includes all eth overhead.

            if (packet.bytesTransferred > 0 && packet.hasNextHeaderPointer() && !packet.isNextHeaderPointerValid())
            {
                logger->warn("cmd_pipe_reader: invalid nextHeaderPointer ({}) in packet containing {} data words ({} payload words, ec={}, packetTotalBytes={})",
                    packet.nextHeaderPointer(), packet.dataWordCount(), packet.availablePayloadWords(), ec.message(),
                    packet.bytesTransferred);
            }

            const u32 *payloadBegin = packet.payloadBegin();
            const u32 *payloadEnd = packet.payloadEnd();

            // Actual payload goes to the buffer.
            std::copy(payloadBegin, payloadEnd, buffer.writeBegin());
            buffer.used += payloadEnd - payloadBegin;

#if CMD_PIPE_RECORD_DATA
            recordOut.write(reinterpret_cast<const char *>(packet.buffer), packet.bytesTransferred);
#endif
            if (packet.lostPackets)
                logger->warn("cmd_pipe_reader: lost {} packets", packet.lostPackets);
        }

        if (ec && ec != ErrorType::Timeout)
            logger->trace("cmd_pipe_reader: error from read(): {}", ec.message());

        /*
        if (ec == ErrorType::Timeout)
            logger->trace("cmd_pipe_reader: read() timed out");
        */

        if (bytesTransferred > 0)
        {
            logger->trace("cmd_pipe_reader: received {} bytes, {} words", bytesTransferred, bytesTransferred / sizeof(u32));
            //log_buffer(logger, spdlog::level::trace, buffer, "cmd_pipe_reader read buffer", LogBuffersMaxWords);
            logger->trace("cmd_pipe_reader: read buffer: {:#010x}", fmt::join(buffer, ", "));
        }

        {
            auto countersAccess = context.counters.access();
            auto &counters = countersAccess.ref();

            ++counters.reads;
            counters.bytesRead += bytesTransferred;
            if (ec == ErrorType::Timeout)
                ++counters.timeouts;
        }

        if (ec == ErrorType::ConnectionError)
            context.quit = true;
    }

    fullfill_pending_response(
        context.pendingSuper.access().ref(),
        ec ? ec : make_error_code(MVLCErrorCode::IsDisconnected));

    fullfill_pending_response(
        context.pendingStack.access().ref(),
        ec ? ec : make_error_code(MVLCErrorCode::IsDisconnected));

    logger->debug("cmd_pipe_reader exiting");
}

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
        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo = true);
        std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);

        std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true);
        std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo = true);
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

        std::error_code superTransactionImpl(
            u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer, unsigned attempt);

        template<typename Dest>
        std::error_code stackTransaction(
            u32 stackRef, const StackCommandBuilder &stackBuilder, Dest &stackResponse);

        template<typename Dest>
        std::error_code stackTransactionImpl(
            u32 stackRef, const StackCommandBuilder &stackBuilder, Dest &stackResponse, unsigned attempt);

        // If set to true the two stack_exec_status registers are checked after
        // every stack transaction, not just on error. For diagnostics and
        // debugging purposes only.
        void setAlwaysCheckStackStatusRegisters(bool alwaysCheck)
        {
            alwaysCheckStackStatusRegisters_ = alwaysCheck;
        }

        bool doAlwaysCheckStackStatusRegisters() const
        {
            return alwaysCheckStackStatusRegisters_;
        }

    private:
        static constexpr std::chrono::milliseconds ResultWaitTimeout = std::chrono::milliseconds(2000);

        ReaderContext &readerContext_;
        bool alwaysCheckStackStatusRegisters_ = false;
};

constexpr std::chrono::milliseconds CmdApi::ResultWaitTimeout;
static const unsigned TransactionMaxAttempts = 3;

std::error_code CmdApi::superTransaction(
    u16 ref,
    std::vector<u32> cmdBuffer,
    std::vector<u32> &responseBuffer)
{
    unsigned attempt = 0;
    std::error_code ec;

    ++readerContext_.counters.access()->superTransactionCount;

    do
    {
        if ((ec = superTransactionImpl(ref, cmdBuffer, responseBuffer, attempt++)))
            spdlog::warn("superTransaction failed on attempt {} with error: {}", attempt, ec.message());
        else if (attempt > 1)
        {
            spdlog::warn("superTransaction succeeded on attempt {}", attempt);
            ++readerContext_.counters.access()->superTransactionRetries;
        }
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
        get_logger("mvlc")->warn(
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
        logger->warn("readStackExecStatusRegisters: superRef={:#06x}, response={:#010x}, ec={}", superRef, fmt::join(response, ", "), ec.message());
        return ec;
    }

    assert(response.size() == 6);

    if (response.size() != 6)
        return MVLCErrorCode::ShortSuperFrame; // cannot really happen, would be a firmware bug

    // Response structure:
    //   super header, superRef,   read_status0, stack_exec_status0, read_status1, stack_exec_status1
    //   0xf1000005,   0x010135f0, 0x01021400,   0xf3001ac8,         0x01021404,   0x00001ac8
    status0 = response[3];
    status1 = response[5];

    logger->debug("readStackExecStatusRegisters: superRef={:#06x}, response={{{:#010x}}}, status0={:#010x}, status1={:#010x}",
         superRef, fmt::join(response, ", "), status0, status1);

    return {};
}

template<typename Dest>
std::error_code CmdApi::stackTransaction(
    u32 stackRef, const StackCommandBuilder &stackBuilder,
    Dest &stackResponse)
{
    auto logger = get_logger("mvlc_apiv2");
    unsigned attempt = 0;
    std::error_code ec;

    ++readerContext_.counters.access()->stackTransactionCount;

    do
    {
        if (attempt > 0)
        {
            logger->warn("stackTransaction: begin transaction, stackRef={:#010x}, attempt={} (zero-based)", stackRef, attempt);
            ++readerContext_.counters.access()->stackTransactionRetries;
        }

        ec = stackTransactionImpl(stackRef, stackBuilder, stackResponse, attempt);

        if (ec && ec != ErrorType::VMEError && attempt < TransactionMaxAttempts)
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

                // Count all of the above as StackExecResponseLost, despite the
                // flags providing more detail for the error code.
                ++readerContext_.counters.access()->stackExecResponsesLost;

                logger->warn("stackTransaction: stackRef={:#010x}, attempt={}: stack_exec_status1 matches stackRef, frame flags from stack_exec_status0: {}, returning {}",
                    stackRef, attempt, format_frame_flags(frameFlags), ec.message());

            }
            else
            {
                // status1 does not contain our stack reference number => the
                // MVLC did not receive the request or somehow did not execute
                // the stack => retry
                ec = MVLCErrorCode::StackExecRequestLost;
                logger->warn("stackTransaction: stackRef={:#010x}, attempt={}: stack_exec_status1 ({:#010x}) does NOT match stackRef => {}, retrying",
                    stackRef, attempt, status1, ec.message());
                ++readerContext_.counters.access()->stackExecRequestsLost;
            }

            // Also read the new parse_error_counter register and output it.
            u32 parseErrorCounter = 0;
            if ((ec = readRegister(registers::parse_error_counter, parseErrorCounter)))
            {
                logger->warn("stackTransaction: stackRef={:#010x}, attempt={} -> {}, failed reading parse_error_counter register", stackRef, attempt, ec.message());
            }
            else if (parseErrorCounter > 0 || true)
            {
                logger->warn("stackTransaction: stackRef={:#010x}, attempt={} -> parse_error_counter register value = {}", stackRef, attempt, parseErrorCounter);
            }
        }
    } while (ec && ++attempt < TransactionMaxAttempts);

    if (!ec && doAlwaysCheckStackStatusRegisters())
    {
        u32 status0 = 0, status1 = 0;

        if (auto ec = readStackExecStatusRegisters(status0, status1))
        {
            logger->warn("stackTransaction: stackRef={:#010x}, attempt={} -> {}, failed reading stack_exec_status registers", stackRef, attempt, ec.message());
        }
        else if (status1 != stackRef)
        {
            logger->warn("stackTransaction: consistency check failed: stackRef={:#010x}, attempt={}: stack_exec_status1 ({:#010x}) does NOT match stackRef ({:#010x})",
                 stackRef, attempt, status1, stackRef);
        }
    }

    logger->log(ec ? spdlog::level::warn : spdlog::level::debug, "stackTransaction: stackRef={:#010x}, attempt={}: returning '{}'",
        stackRef, attempt, ec.message());

    return ec;
}

// Perform a stack transaction.
//
// stackRef is the reference word that is produced as part of the
template<typename Dest>
std::error_code CmdApi::stackTransactionImpl(
    u32 stackRef, const StackCommandBuilder &stackBuilder,
    Dest &stackResponse,
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

    get_logger("mvlc_apiv2")->trace("stackTransactionImpl 'exec immediate stack' command buffer : {:#010x}", fmt::join(cmdBuffer, " "));

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
    // (write command + payload) for each stack part. If the part is the first
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
    logger->debug("readRegister(addr=0x{:04x}): superRef=0x{:04x}", address, value, ref);

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
    logger->debug("writeRegister(addr=0x{:04x}, value={}): superRef=0x{:04x}", address, value, ref);

    SuperCommandBuilder scb;
    scb.addReferenceWord(ref);
    scb.addWriteLocal(address, value);
    auto cmdBuffer = make_command_buffer(scb);
    std::vector<u32> responseBuffer;

    if (auto ec = superTransaction(ref, cmdBuffer, responseBuffer))
        return ec;

    logger->debug("writeRegister(a=0x{:04x}, value={}, superRef=0x{:04x}): response buffer: {:#010x} ", address, value, ref, fmt::join(responseBuffer, ", "));

    if (responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code CmdApi::vmeRead(
    u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
{
    auto logger = get_logger("mvlc");

    u32 stackRef = readerContext_.nextStackReference++;

    logger->debug("vmeRead(addr=0x{:04x}, amod=0x{:02x}): stackRef=0x{:08x}", address, std::byte(amod), stackRef);

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

    logger->debug("vmeWrite(addr=0x{:04x}, value=0x{:08x}, amod=0x{:02x}): stackRef=0x{:08x}", address, value, std::byte(amod), stackRef);

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

    get_logger("mvlc")->debug("vmeBlockRead(): address=0x{:08X}, amod=0x{:02X}, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, amod, maxTransfers, fifo, stackRef);

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
    u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo)
{
    if (!vme_amods::is_block_mode(amod))
        return make_error_code(MVLCErrorCode::NonBlockAddressMode);

    u32 stackRef = readerContext_.nextStackReference++;

    get_logger("mvlc")->debug("vmeBlockRead(): address=0x{:08X}, amod=0x{:02X}, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, amod, maxTransfers, fifo, stackRef);

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

    get_logger("mvlc")->debug("vmeBlockRead(): address=0x{:08X}, amod=2eSST, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, maxTransfers, fifo, stackRef);

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

    get_logger("mvlc")->debug("vmeBlockReadSwapped(): address=0x{:08X}, amod=0x{:02X}, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, maxTransfers, amod, fifo, stackRef);

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
    u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo)
{
    u32 stackRef = readerContext_.nextStackReference++;

    get_logger("mvlc")->debug("vmeBlockReadSwapped(): address=0x{:08X}, amod=0x{:02X}, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, maxTransfers, amod, fifo, stackRef);

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

    get_logger("mvlc")->debug("vmeBlockReadSwapped(): address=0x{:08X}, amod=2eSST, maxTransfers={}, fifo={}, stackRef=0x{:08X}",
        address, maxTransfers, fifo, stackRef);

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

// ============================================
// MVLC
// ============================================
struct MVLC::Private
{
    explicit Private(std::unique_ptr<MVLCBasicInterface> &&impl)
        : impl_(std::move(impl))
        , readerContext_(impl_.get())
        , cmdApi_(readerContext_)
        , isConnected_(false)
        , hardwareId_(0)
        , firmwareRevision_(0)
    {
#if 0 // #ifndef NDEBUG // TODO: enable this once the firmware is fixed
        cmdApi_.setAlwaysCheckStackStatusRegisters(true);
#endif
    }

    ~Private()
    {
        stopCmdReader();
    }

    std::error_code resultCheck(const std::error_code &ec)
    {
        // Update the local connection state without calling
        // impl->isConnected() which would require us to take both pipe locks.
        if (ec == ErrorType::ConnectionError)
            isConnected_ = false;
        return ec;
    }

    void startCmdReader()
    {
        if (!readerThread_.joinable())
        {
            readerContext_.quit = false;
            readerContext_.stackErrors.access().ref() = {};
            readerContext_.counters.access().ref() = {};
            readerThread_ = std::thread(cmd_pipe_reader, std::ref(readerContext_));
        }
    }

    void stopCmdReader()
    {
        if (readerThread_.joinable())
        {
            readerContext_.quit = true;
            readerThread_.join();
        }
    }

    bool cmdReaderIsRunning()
    {
        return readerThread_.joinable();
    }

    mutable Locks locks_;
    std::unique_ptr<MVLCBasicInterface> impl_;
    ReaderContext readerContext_;
    CmdApi cmdApi_;
    std::thread readerThread_;

    // Cached data
    std::atomic<bool> isConnected_;
    std::atomic<u32> hardwareId_;
    std::atomic<u32> firmwareRevision_;
};

MVLC::MVLC()
    : d(nullptr)
{
}

MVLC::MVLC(std::unique_ptr<MVLCBasicInterface> &&impl)
    : d(std::make_shared<Private>(std::move(impl)))
{
    get_logger("mvlc")->trace(__PRETTY_FUNCTION__);
}

MVLC::~MVLC()
{
}

std::error_code MVLC::connect()
{
    auto logger = get_logger("mvlc");
    auto guards = d->locks_.lockBoth();
    d->isConnected_ = d->impl_->isConnected();
    std::error_code ec;

    if (!isConnected())
    {
        assert(!d->cmdReaderIsRunning()); // The cmd reader should be stopped as this point.
        d->stopCmdReader();

        ec = d->impl_->connect();
        d->isConnected_ = d->impl_->isConnected();

        if (ec)
        {
            logger->error("MVLC::connect(): {}", ec.message());
            return ec;
        }

        d->startCmdReader();

        // At this point the cmd_pipe_reader is running and thus the CmdApi can
        // be used for communication. Direct methods like MVLC::readRegister()
        // cannot be used as those take the cmd lock which we are still holding.

        // Read hardware id and firmware revision.
        u32 hwId = 0;
        u32 fwRev = 0;

        logger->debug("reading hardware_id register");
        if (auto ec = d->cmdApi_.readRegister(registers::hardware_id, hwId))
        {
            logger->error("error reading hardware_id register: {}", ec.message());
            d->isConnected_ = false;
            d->stopCmdReader();
            return ec;
        }

        logger->debug("reading firmware_revision register");
        if (auto ec = d->cmdApi_.readRegister(registers::firmware_revision, fwRev))
        {
            logger->error("error reading firmware_revision register: {}", ec.message());
            d->isConnected_ = false;
            d->stopCmdReader();
            return ec;
        }

        d->hardwareId_ = hwId;
        d->firmwareRevision_ = fwRev;

        if (!firmware_checks::is_recommended_firmware(hwId, fwRev))
        {
            logger->warn("WARNING: Firmware FW{:04x} is recommended for this module (hardwareId={:04x}), but found FW{:04x}!",
                firmware_checks::minimum_recommended_firmware(hwId), hwId, fwRev);
            logger->warn("WARNING: Please update the firmware to the recommended version. See https://mesytec.com/ for details.");
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

        if (d->readerThread_.joinable())
        {
            d->readerContext_.quit = true;
            d->readerThread_.join();
        }

        ec = d->impl_->disconnect();
        d->isConnected_ = d->impl_->isConnected();

        if (ec)
            logger->error("Error disconnecting from MVLC ({}): {}", conInfo, ec.message());
        else
            logger->info("Disconnected from MVLC ({})", conInfo);
    }

    assert(d->impl_->isConnected() == d->isConnected_);

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
    return d->resultCheck(d->cmdApi_.readRegister(address, value));
}

std::error_code MVLC::writeRegister(u16 address, u32 value)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.writeRegister(address, value));
}


std::error_code MVLC::vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeRead(address, value, amod, dataWidth));
}

std::error_code MVLC::vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeWrite(address, value, amod, dataWidth));
}


std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockRead(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockRead(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockRead(address, rate, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockReadSwapped(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, util::span<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockReadSwapped(address, amod, maxTransfers, dest, fifo));
}

std::error_code MVLC::vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockReadSwapped(address, rate, maxTransfers, dest, fifo));
}

std::error_code MVLC::uploadStack(
    u8 stackOutputPipe,
    u16 stackMemoryOffset,
    const std::vector<StackCommand> &commands)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.uploadStack(stackOutputPipe, stackMemoryOffset, commands));
}

std::error_code MVLC::uploadStack(
    u8 stackOutputPipe,
    u16 stackMemoryOffset,
    const std::vector<u32> &stackContents)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.uploadStack(stackOutputPipe, stackMemoryOffset, stackContents));
}

CmdPipeCounters MVLC::getCmdPipeCounters() const
{
    return d->readerContext_.counters.copy();
}

StackErrorCounters MVLC::getStackErrorCounters() const
{
    return d->cmdApi_.getStackErrorCounters();
}

void MVLC::resetStackErrorCounters()
{
    d->cmdApi_.resetStackErrorCounters();
}

MVLCBasicInterface *MVLC::getImpl()
{
    return d->impl_.get();
}

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

    u16 superRef = superBuilder[0].value;

    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.superTransaction(superRef, make_command_buffer(superBuilder), dest));
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
    return d->resultCheck(d->cmdApi_.stackTransaction(stackRef, stackBuilder, dest));
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
