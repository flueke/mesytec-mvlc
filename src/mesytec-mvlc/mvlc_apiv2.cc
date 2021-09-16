#include "mvlc_apiv2.h"

#include <future>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "logging.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_error.h"
#include "mvlc_eth_interface.h"
#include "mvlc_usb_interface.h"
#include "util/storage_sizes.h"
#include "vme_constants.h"

namespace mesytec
{
namespace mvlc
{
namespace apiv2
{

namespace
{
struct PendingResponse
{
    std::promise<std::error_code> promise;
    std::vector<u32> *dest = nullptr;
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
        , pendingSuper({})
        , pendingStack({})
        , stackErrors({})
        , counters({})
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
    };

    auto is_good_header = [] (const u32 header) -> bool
    {
        return is_super_buffer(header)
            || is_stack_buffer(header)
            || is_stackerror_notification(header);
    };

    auto contains_complete_frame = [=] (const u32 *begin, const u32 *end) -> bool
    {
        if (end - begin == 0)
            return false;

        assert(end - begin > 0);
        assert(is_good_header(*begin));

        auto frameInfo = extract_frame_info(*begin);
        auto avail = end - begin;

        if (frameInfo.len + 1 > avail)
            return false;

        while (frameInfo.flags & frame_flags::Continue)
        {
            begin += frameInfo.len + 1;
            avail = end - begin;
            frameInfo = extract_frame_info(*begin);

            if (frameInfo.len + 1 > avail)
                return false;
        }

        return true;
    };

#ifdef __linux__
    prctl(PR_SET_NAME,"cmd_pipe_reader",0,0,0);
#endif
    auto logger = spdlog::get("cmd_pipe_reader");

    logger->info("cmd_pipe_reader starting");

    auto mvlcUsb = dynamic_cast<usb::MVLC_USB_Interface *>(context.mvlc);
    auto mvlcEth = dynamic_cast<eth::MVLC_ETH_Interface *>(context.mvlc);

    assert(mvlcUsb || mvlcEth);

    std::error_code ec;
    Buffer buffer;
    buffer.ensureFreeSpace(util::Megabytes(1)/sizeof(u32));

    while (!context.quit)
    {
        auto countersAccess = context.counters.access();
        auto &counters = countersAccess.ref();

        while (buffer.used)
        {
            log_buffer(logger, spdlog::level::trace, buffer, "cmd_pipe_reader buffer");

            while (!buffer.empty() && !is_good_header(buffer[0]))
            {
                buffer.consume(1);
                // FIXME: these variables are only incremented here
                ++counters.invalidHeaders;
                ++counters.wordsSkipped;
            }

            if (buffer.empty())
                continue;

            if (contains_complete_frame(buffer.begin(), buffer.end()))
            {
                if (is_stackerror_notification(buffer[0]))
                {
                    ++counters.errorBuffers;

                    auto frameBegin = buffer.begin();
                    auto frameEnd = buffer.begin() + get_frame_length(buffer[0]) + 1;

                    update_stack_error_counters(
                        context.stackErrors.access().ref(),
                        basic_string_view<u32>(frameBegin, frameEnd-frameBegin));

                    buffer.consume(get_frame_length(buffer[0]) + 1);
                }
                // super buffers
                else if (is_super_buffer(buffer[0]))
                {
                    ++counters.superBuffers;
                    auto pendingResponse = context.pendingSuper.access();
                    std::error_code ec;

                    if (get_frame_length(buffer[0]) == 0)
                    {
                        ec = make_error_code(MVLCErrorCode::ShortSuperFrame);
                        ++counters.shortSuperBuffers;
                        buffer.consume(1);
                    }
                    else
                    {
                        using namespace super_commands;

                        u32 refCmd = buffer[1];
                        if (((refCmd >> SuperCmdShift) & SuperCmdMask) != static_cast<u32>(SuperCommandType::ReferenceWord))
                        {
                            logger->warn("cmd_pipe_reader: super buffer does not start with ref command");
                            ec = make_error_code(MVLCErrorCode::SuperFormatError);
                            ++counters.superFormatErrors;
                            buffer.consume(get_frame_length(buffer[0]) + 1);
                        }
                        else
                        {
                            u32 ref = buffer[1] & SuperCmdArgMask;

                            if (ref != pendingResponse->reference)
                            {
                                logger->warn("cmd_pipe_reader: super ref mismatch, wanted={}, got={}",
                                             pendingResponse->reference, ref);
                                ec = make_error_code(MVLCErrorCode::SuperReferenceMismatch);
                                ++counters.superRefMismatches;
                            }
                            else
                            {
                                fullfill_pending_response(
                                    pendingResponse.ref(), {}, &buffer[0], get_frame_length(buffer[0] + 1));
                            }

                            buffer.consume(get_frame_length(buffer[0]) + 1);
                        }
                    }
                }
                // stack buffers
                else if (is_stack_buffer(buffer[0]))
                {
                    ++counters.stackBuffers;

                    auto pendingResponse = context.pendingStack.access();
                    size_t toConsume = 1;
                    std::error_code ec;

                    if (get_frame_length(buffer[0]) < 1)
                    {
                        ec = make_error_code(MVLCErrorCode::MirrorShortResponse);
                    }
                    else
                    {
                        toConsume += get_frame_length(buffer[0]);

                        u32 stackRef = buffer[1];

                        if (stackRef != pendingResponse->reference)
                        {
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
            }
            else
            {
                // No complete frame in the buffer
                break;
            }
        }

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

            // Actual payload goes to the buffer.
            std::copy(packet.payloadBegin(), packet.payloadEnd(), buffer.writeBegin());
            buffer.used += packet.payloadEnd() - packet.payloadBegin();
        }

        if (bytesTransferred > 0)
            logger->trace("received {} bytes", bytesTransferred);

        ++counters.reads;
        counters.bytesRead += bytesTransferred;
        if (ec == ErrorType::Timeout)
            ++counters.timeouts;


        if (ec == ErrorType::ConnectionError)
            context.quit = true;
    }

    fullfill_pending_response(
        context.pendingSuper.access().ref(),
        ec ? ec :make_error_code(MVLCErrorCode::IsDisconnected));

    fullfill_pending_response(
        context.pendingStack.access().ref(),
        ec ? ec : make_error_code(MVLCErrorCode::IsDisconnected));

    logger->info("cmd_pipe_reader exiting");
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
        std::error_code vmeSignallingRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);
        std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest);
        std::error_code vmeMBLTSwapped(u32 address, u16 maxTransfers, std::vector<u32> &dest);

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

        std::error_code superTransaction(
            u16 ref, std::vector<u32> superBuffer, std::vector<u32> &responseBuffer);

        std::error_code stackTransaction(
            u32 stackRef, const StackCommandBuilder &stackBuilder, std::vector<u32> &stackResponse);

    private:
        static constexpr std::chrono::milliseconds ResultWaitTimeout = std::chrono::milliseconds(250);

        ReaderContext &readerContext_;
};

constexpr std::chrono::milliseconds CmdApi::ResultWaitTimeout;

std::error_code CmdApi::superTransaction(
    u16 ref,
    std::vector<u32> cmdBuffer,
    std::vector<u32> &responseBuffer)
{
    auto rf = set_pending_response(readerContext_.pendingSuper, responseBuffer, ref);

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);

    if (ec)
        return fullfill_pending_response(readerContext_.pendingSuper, ec);

    if (rf.wait_for(ResultWaitTimeout) != std::future_status::ready)
        return fullfill_pending_response(readerContext_.pendingSuper, make_error_code(MVLCErrorCode::CommandTimeout));

    return rf.get();
}

std::error_code CmdApi::stackTransaction(
    u32 stackRef, const StackCommandBuilder &stackBuilder,
    std::vector<u32> &stackResponse)
{
    u16 superRef = readerContext_.nextSuperReference++;

    SuperCommandBuilder superBuilder;
    superBuilder.addReferenceWord(superRef);
    superBuilder.addStackUpload(stackBuilder, CommandPipe, stacks::ImmediateStackStartOffsetBytes);
    superBuilder.addWriteLocal(stacks::Stack0OffsetRegister, stacks::ImmediateStackStartOffsetBytes);
    superBuilder.addWriteLocal(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);
    auto cmdBuffer = make_command_buffer(superBuilder);

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
        fullfill_pending_response(readerContext_.pendingSuper, ec);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    if (superFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
    {
        ec = make_error_code(MVLCErrorCode::CommandTimeout);
        fullfill_pending_response(readerContext_.pendingSuper, ec);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    if (auto ec = superFuture.get())
        return fullfill_pending_response(readerContext_.pendingStack, ec);

    // stack response
    if (stackFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
    {
        ec = make_error_code(MVLCErrorCode::CommandTimeout);
        return fullfill_pending_response(readerContext_.pendingStack, ec);
    }

    return stackFuture.get();
}

std::error_code CmdApi::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents)
{
    u16 superRef = readerContext_.nextSuperReference++;
    SuperCommandBuilder superBuilder;
    superBuilder.addReferenceWord(superRef);
    superBuilder.addStackUpload(stackContents, stackOutputPipe, stackMemoryOffset);
    auto cmdBuffer = make_command_buffer(superBuilder);
    std::vector<u32> superResponse;

    auto superFuture = set_pending_response(readerContext_.pendingSuper, superResponse, superRef);

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);

    if (ec)
        return fullfill_pending_response(readerContext_.pendingSuper, ec);

    if (superFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
        return fullfill_pending_response(readerContext_.pendingSuper, make_error_code(MVLCErrorCode::CommandTimeout));

    return superFuture.get();
}

std::error_code CmdApi::uploadStack(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands)
{
#if 0
    u16 superRef = readerContext_.nextSuperReference++;

    SuperCommandBuilder superBuilder;
    superBuilder.addReferenceWord(superRef);
    superBuilder.addStackUpload(make_stack_buffer(commands), stackOutputPipe, stackMemoryOffset);
    auto cmdBuffer = make_command_buffer(superBuilder);
    std::vector<u32> superResponse;

    auto superFuture = set_pending_response(readerContext_.pendingSuper, superResponse, superRef);

    size_t bytesWritten = 0;

    auto ec = readerContext_.mvlc->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);

    if (ec)
        return fullfill_pending_response(readerContext_.pendingSuper, ec);

    if (superFuture.wait_for(ResultWaitTimeout) != std::future_status::ready)
        return fullfill_pending_response(readerContext_.pendingSuper, make_error_code(MVLCErrorCode::CommandTimeout));

    return superFuture.get();
#else
    return uploadStack(stackOutputPipe, stackMemoryOffset, make_stack_buffer(commands));
#endif
}

std::error_code CmdApi::readRegister(u16 address, u32 &value)
{
    u16 ref = readerContext_.nextSuperReference++;

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
    u16 ref = readerContext_.nextSuperReference++;
    SuperCommandBuilder scb;
    scb.addReferenceWord(ref);
    scb.addWriteLocal(address, value);
    auto cmdBuffer = make_command_buffer(scb);
    std::vector<u32> responseBuffer;

    if (auto ec = superTransaction(ref, cmdBuffer, responseBuffer))
        return ec;

    if (responseBuffer.size() != 4)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    return {};
}

std::error_code CmdApi::vmeRead(
    u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMERead(address, amod, dataWidth);

    std::vector<u32> stackResponse;

    if (auto ec = stackTransaction(stackRef, stackBuilder, stackResponse))
        return ec;

    log_buffer(spdlog::get("mvlc"), spdlog::level::trace, stackResponse, "vmeRead(): stackResponse");

    if (stackResponse.size() != 3)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    if (extract_frame_info(stackResponse[0]).flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    const u32 Mask = (dataWidth == VMEDataWidth::D16 ? 0x0000FFFF : 0xFFFFFFFF);

    value = stackResponse[2] & Mask;

    return {};
}

std::error_code CmdApi::vmeWrite(
    u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEWrite(address, value, amod, dataWidth);

    std::vector<u32> stackResponse;

    if (auto ec = stackTransaction(stackRef, stackBuilder, stackResponse))
        return ec;

    log_buffer(spdlog::get("mvlc"), spdlog::level::trace, stackResponse, "vmeWrite(): stackResponse");

    if (stackResponse.size() != 2)
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

    if (extract_frame_info(stackResponse[0]).flags & frame_flags::Timeout)
        return MVLCErrorCode::NoVMEResponse;

    return {};
}

std::error_code CmdApi::vmeBlockRead(
    u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest)
{
    if (!vme_amods::is_block_mode(amod))
        return make_error_code(MVLCErrorCode::NonBlockAddressMode);

    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEBlockRead(address, amod, maxTransfers);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(spdlog::get("mvlc"), spdlog::level::trace, dest, "vmeBlockRead(): stackResponse");

    return {};
}

std::error_code CmdApi::vmeMBLTSwapped(
    u32 address, u16 maxTransfers, std::vector<u32> &dest)
{
    u32 stackRef = readerContext_.nextStackReference++;

    StackCommandBuilder stackBuilder;
    stackBuilder.addWriteMarker(stackRef);
    stackBuilder.addVMEMBLTSwapped(address, maxTransfers);

    if (auto ec = stackTransaction(stackRef, stackBuilder, dest))
        return ec;

    log_buffer(spdlog::get("mvlc"), spdlog::level::trace, dest, "vmeMBLTSwapped(): stackResponse");

    return stackTransaction(stackRef, stackBuilder, dest);
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
    }

    ~Private()
    {
        readerContext_.quit = true;
        if (readerThread_.joinable())
            readerThread_.join();
    }

    std::error_code resultCheck(const std::error_code &ec)
    {
        // Update the local connection state without calling
        // impl->isConnected() which would require us to take both pipe locks.
        if (ec == ErrorType::ConnectionError)
            isConnected_ = false;
        return ec;
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
}

MVLC::~MVLC()
{
}

std::error_code MVLC::connect()
{
    auto logger = spdlog::get("mvlc");
    auto guards = d->locks_.lockBoth();
    d->isConnected_ = d->impl_->isConnected();
    std::error_code ec;

    if (!isConnected())
    {
        assert(!d->readerThread_.joinable());

        if (d->readerThread_.joinable())
        {
            d->readerContext_.quit = true;
            d->readerThread_.join();
        }

        assert(!d->readerThread_.joinable());

        ec = d->impl_->connect();
        d->isConnected_ = d->impl_->isConnected();

        if (ec)
        {
            logger->error("MVLC::connect(): {}", ec.message());
            return ec;
        }

        if (!d->readerThread_.joinable())
        {
            d->readerContext_.quit = false;
            d->readerContext_.stackErrors.access().ref() = {};
            d->readerContext_.counters.access().ref() = {};
            d->readerThread_ = std::thread(cmd_pipe_reader, std::ref(d->readerContext_));
        }

        // Read hardware id and firmware revision.
        u32 hardwareId = 0;
        u32 firmwareRevision = 0;

        logger->debug("reading hardware_id register");
        if (auto ec = d->cmdApi_.readRegister(registers::hardware_id, hardwareId))
        {
            d->isConnected_ = false;
            return ec;
        }

        logger->debug("reading firmware_revision register");
        if (auto ec = d->cmdApi_.readRegister(registers::firmware_revision, firmwareRevision))
        {
            d->isConnected_ = false;
            return ec;
        }

        d->hardwareId_ = hardwareId;
        d->firmwareRevision_ = firmwareRevision;

        logger->info("connected to MVLC ({})", connectionInfo());
    }
    else
    {
        return make_error_code(MVLCErrorCode::IsConnected);
    }

    return ec;
}

std::error_code MVLC::disconnect()
{
    auto logger = spdlog::get("mvlc");
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


std::error_code MVLC::vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeBlockRead(address, amod, maxTransfers, dest));
}

std::error_code MVLC::vmeMBLTSwapped(u32 address, u16 maxTransfers, std::vector<u32> &dest)
{
    auto guard = d->locks_.lockCmd();
    return d->resultCheck(d->cmdApi_.vmeMBLTSwapped(address, maxTransfers, dest));
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

} // end namespace apiv2
} // end namespace mvlc
} // end namespace mesytec
