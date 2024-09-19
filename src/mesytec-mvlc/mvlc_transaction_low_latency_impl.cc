#include "mvlc_transaction_low_latency_impl.h"

#include <atomic>
#include <future>
#include <string_view>

#include "mvlc_buffer_validators.h"
#include "mvlc_command_builders.h"
#include "mvlc_error.h"
#include "mvlc_eth_interface.h"
#include "mvlc_stack_errors.h"
#include "mvlc_usb_interface.h"
#include "util/logging.h"
#include "util/protected.h"
#include "util/storage_sizes.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace mesytec::mvlc
{

static const size_t LogBuffersMaxWords = 0; // set to 0 to output the full buffer contents

struct PendingResponse
{
    std::promise<std::error_code> promise;
    std::vector<u32> *dest = nullptr;
    u32 reference = 0;
    bool pending = false;
};

struct ReaderContext
{
    MvlcBasicInterface *mvlc;
    std::atomic<bool> quit;
    std::atomic<u16> nextSuperReference;
    std::atomic<u32> nextStackReference;
    WaitableProtected<PendingResponse> pendingSuper;
    WaitableProtected<PendingResponse> pendingStack;

    Protected<StackErrorCounters> stackErrors;
    Protected<CmdPipeCounters> counters;

    explicit ReaderContext(MvlcBasicInterface *mvlc_)
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

        if (pr.dest && contents && len)
            std::copy(contents, contents+len, std::back_inserter(*pr.dest));

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
    std::vector<u32> &dest,
    u32 reference)
{
    auto pendingResponse = pending.wait(
        [] (const PendingResponse &pr) { return !pr.pending; });

    assert(pendingResponse.ref().pending == false);

    std::promise<std::error_code> promise;
    auto result = promise.get_future();

    pendingResponse.ref() = { std::move(promise), &dest, reference, true };

    return result;
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

        std::basic_string_view<const u32> viewU32() const
        {
            return std::basic_string_view<const u32>(begin(), size());
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
                logger->warn("contains_complete_frame: landed on bad header word: 0x{:08x}, avail={}", header, avail);
                assert(!"bad header word");
                return false;
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
        auto countersAccess = context.counters.access();
        auto &counters = countersAccess.ref();

        while (buffer.used && !context.quit.load(std::memory_order_relaxed))
        {
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
                logger->debug("cmd_pipe_reader: received complete frame: {:#010x}", fmt::join(buffer.viewU32(), ", "));


                logNextIncomplete = true;

                if (is_stackerror_notification(buffer[0]))
                {
                    ++counters.errorBuffers;

                    const auto frameLength = get_frame_length(buffer[0]);
                    auto frameBegin = buffer.begin();
                    auto frameEnd = buffer.begin() + frameLength + 1;

                    update_stack_error_counters(
                        context.stackErrors.access().ref(),
                        std::basic_string_view<u32>(frameBegin, frameEnd-frameBegin));

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

            static std::array<u8, eth::JumboFrameMaxSize> packetBuffer;

            auto packet = mvlcEth->read_packet(
                Pipe::Command,
                packetBuffer.data(),
                packetBuffer.size());

            ec = packet.ec;
            bytesTransferred += packet.bytesTransferred; // This includes all eth overhead.

            size_t headerOffsetWords = 0;

            // If a header pointer is present use it as the start of the payload
            // data. Otherwise use the full payload contained in the packet.
            if (packet.hasNextHeaderPointer())
            {
                if (packet.isNextHeaderPointerValid())
                {
                    headerOffsetWords = packet.nextHeaderPointer();
                }
                else if (!ec)
                {
                    logger->warn("cmd_pipe_reader: invalid nextHeaderPointer ({}) in packet containing {} data words ({} payload words, ec={})",
                        packet.nextHeaderPointer(), packet.dataWordCount(), packet.availablePayloadWords(), ec.message());
                }
            }

            if (headerOffsetWords > 0)
            {
                logger->warn("skipped {} words of packet data to start payload data from the given nextHeaderPointer ({})",
                                headerOffsetWords, packet.nextHeaderPointer());
            }

            const u32 *payloadBegin = packet.payloadBegin() + headerOffsetWords;
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
            logger->trace("received {} bytes, {} words", bytesTransferred, bytesTransferred / sizeof(u32));
            log_buffer(logger, spdlog::level::trace, buffer, "cmd_pipe_reader read buffer", LogBuffersMaxWords);
        }

        ++counters.reads;
        counters.bytesRead += bytesTransferred;
        if (ec == ErrorType::Timeout)
            ++counters.timeouts;


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

struct MvlcTransactionLowLatencyImpl::Private
{
    static constexpr std::chrono::milliseconds ResultWaitTimeout = std::chrono::milliseconds(2000);

    MvlcTransactionLowLatencyImpl *q = nullptr;
    std::unique_ptr<MvlcBasicInterface> impl_;
    ReaderContext readerContext_;
    std::thread readerThread_;

    // Cached data
    std::atomic<bool> isConnected_;
    std::atomic<u32> hardwareId_;
    std::atomic<u32> firmwareRevision_;

    Private(MvlcTransactionLowLatencyImpl *trxImpl,  std::unique_ptr<MvlcBasicInterface> &&mvlcImpl)
        : q(trxImpl)
        , impl_(std::move(mvlcImpl))
        , readerContext_(impl_.get())
    { }

    ~Private()
    {
        stopCmdReader();
    }

    #if 0
    std::error_code resultCheck(const std::error_code &ec)
    {
        // Update the local connection state without calling
        // impl->isConnected() which would require us to take both pipe locks.
        if (ec == ErrorType::ConnectionError)
            isConnected_ = false;
        return ec;
    }
    #endif

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

    std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest);

    std::error_code superTransactionImpl(
        u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer, unsigned attempt);

    std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest);

    std::error_code stackTransactionImpl(
        u32 stackRef, const StackCommandBuilder &stackBuilder, std::vector<u32> &stackResponse, unsigned attempt);

    std::error_code readStackExecStatusRegisters(u32 &status0, u32 &status1);
};

constexpr std::chrono::milliseconds MvlcTransactionLowLatencyImpl::Private::ResultWaitTimeout;
static const unsigned TransactionMaxAttempts = 10;

MvlcTransactionLowLatencyImpl::MvlcTransactionLowLatencyImpl(std::unique_ptr<MvlcBasicInterface> &&mvlcImpl)
    : d(std::make_unique<Private>(this, std::move(mvlcImpl)))
{
}

MvlcTransactionLowLatencyImpl::~MvlcTransactionLowLatencyImpl() { }

MvlcBasicInterface *MvlcTransactionLowLatencyImpl::getImpl()
{
    return d->impl_.get();
}

std::error_code MvlcTransactionLowLatencyImpl::superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest)
{
    // TODO: locking here, resultCheck here?
    return d->superTransaction(superBuilder, dest);
}

std::error_code MvlcTransactionLowLatencyImpl::Private::superTransaction(
    const SuperCommandBuilder &superBuilder, std::vector<u32> &dest)
{
    assert(!superBuilder.empty() && superBuilder[0].type == SuperCommandType::ReferenceWord);

    if (superBuilder.empty())
        return make_error_code(MVLCErrorCode::SuperFormatError);

    if (superBuilder[0].type != SuperCommandType::ReferenceWord)
        return make_error_code(MVLCErrorCode::SuperFormatError);

    u16 superRef = superBuilder[0].value;
    auto cmdBuffer = make_command_buffer(superBuilder);
    unsigned attempt = 0;
    std::error_code ec;

    do
    {
        if ((ec = superTransactionImpl(superRef, cmdBuffer, dest, attempt++)))
            spdlog::warn("superTransaction failed on attempt {} with error: {}", attempt, ec.message());
    } while (ec && attempt < TransactionMaxAttempts);

    return ec;
}

std::error_code MvlcTransactionLowLatencyImpl::Private::superTransactionImpl(
    u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer, unsigned attempt)
{
    if (superBuffer.size() > MirrorTransactionMaxWords)
        return make_error_code(MVLCErrorCode::MirrorTransactionMaxWordsExceeded);

    auto rf = set_pending_response(readerContext_.pendingSuper, responseBuffer, ref);
    auto tSet = std::chrono::steady_clock::now();

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(superBuffer.data()),
        superBuffer.size() * sizeof(u32),
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

std::error_code MvlcTransactionLowLatencyImpl::stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest)
{
    return d->stackTransaction(stackBuilder, dest);
}

std::error_code MvlcTransactionLowLatencyImpl::Private::stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest)
{
    using CommandType = StackCommand::CommandType;

    assert(!stackBuilder.empty() && stackBuilder[0].type == CommandType::WriteMarker);

    if (stackBuilder.empty())
        return make_error_code(MVLCErrorCode::StackFormatError);

    if (stackBuilder[0].type != CommandType::WriteMarker)
        return make_error_code(MVLCErrorCode::StackFormatError);

    u32 stackRef = stackBuilder[0].value;
    auto logger = get_logger("mvlc_apiv2");
    unsigned attempt = 0;
    std::error_code ec;

    do
    {
        if (attempt > 0)
            logger->info("stackTransaction: begin transaction, stackRef={:#010x}, attempt={}", stackRef, attempt);

        ec = stackTransactionImpl(stackRef, stackBuilder, dest, attempt);

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

std::error_code MvlcTransactionLowLatencyImpl::Private::stackTransactionImpl(
    u32 stackRef, const StackCommandBuilder &stackBuilder, std::vector<u32> &stackResponse, unsigned attempt)
{
    if (auto ec = upload_stack(q, static_cast<u8>(CommandPipe), stacks::ImmediateStackStartOffsetBytes, make_stack_buffer(stackBuilder)))
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

std::error_code MvlcTransactionLowLatencyImpl::Private::readStackExecStatusRegisters(u32 &status0, u32 &status1)
{
    auto logger = get_logger("mvlc_apiv2");

    u16 superRef = readerContext_.nextSuperReference++;
    SuperCommandBuilder sb;
    sb.addReferenceWord(superRef);
    sb.addReadLocal(registers::stack_exec_status0);
    sb.addReadLocal(registers::stack_exec_status1);

    std::vector<u32> response;

    if (auto ec = superTransaction(sb, response))
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

u16 MvlcTransactionLowLatencyImpl::nextSuperReference()
{
    return d->readerContext_.nextSuperReference++;
}

u32 MvlcTransactionLowLatencyImpl::nextStackReference()
{
    return d->readerContext_.nextStackReference++;
}

CmdPipeCounters MvlcTransactionLowLatencyImpl::getCmdPipeCounters() const
{
    return d->readerContext_.counters.copy();
}

StackErrorCounters MvlcTransactionLowLatencyImpl::getStackErrorCounters() const
{
    return d->readerContext_.stackErrors.copy();
}

void MvlcTransactionLowLatencyImpl::resetStackErrorCounters()
{
    d->readerContext_.stackErrors.access().ref() = {};
}

}
