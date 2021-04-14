#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <lyra/lyra.hpp>
#include <future>
#include <list>
#include <spdlog/spdlog.h>
#ifndef __WIN32
#include <sys/prctl.h>
#endif

using std::cout;
using std::endl;
using namespace mesytec::mvlc;


struct PendingResponse
{
    std::promise<std::error_code> promise;
    std::vector<u32> *dest = nullptr;
    bool pending = false;
};

struct ReaderContext
{
    struct Counters
    {
        std::atomic<size_t> reads;
        std::atomic<size_t> bytesRead;
        std::atomic<size_t> timeouts;
        std::atomic<size_t> invalidHeaders;
        std::atomic<size_t> bytesSkipped;
        std::atomic<size_t> errorBuffers;
        std::atomic<size_t> dsoBuffers;

        Counters()
            : reads(0)
            , bytesRead(0)
            , timeouts(0)
            , invalidHeaders(0)
            , bytesSkipped(0)
        {}
    };

    std::atomic<bool> quit;
    MVLCBasicInterface *mvlc;
    TicketMutex cmdLock;
    WaitableProtected<PendingResponse> pendingSuper;
    WaitableProtected<PendingResponse> pendingStack;
    Counters counters;

    ReaderContext()
        : mvlc(nullptr)
        , pendingSuper({})
        , pendingStack({})
        {}

    //WaitableProtected<std::vector<u32>> stackErrorBuffer;
    //WaitableProtected<std::vector<u32>> dsoBuffer;
};

bool fullfill_pending_response(
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
        return true;
    }

    return false;
}

std::future<std::error_code> set_pending_super_response(
    ReaderContext &readerContext,
    std::vector<u32> &dest)
{
    auto pendingResponse = readerContext.pendingSuper.wait(
        [] (const PendingResponse &pr) { return !pr.pending; });

    assert(pendingResponse.ref().pending == false);

    std::promise<std::error_code> promise;
    auto result = promise.get_future();

    pendingResponse.ref() = { std::move(promise), &dest, true };

    return result;
}

std::future<std::error_code> set_pending_stack_response(
    ReaderContext &readerContext,
    std::vector<u32> &dest)
{
    auto pendingResponse = readerContext.pendingStack.wait(
        [] (const PendingResponse &pr) { return !pr.pending; });

    assert(pendingResponse.ref().pending == false);

    std::promise<std::error_code> promise;
    auto result = promise.get_future();

    pendingResponse.ref() = { std::move(promise), &dest, true };

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
                size_t oldFree = free();
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
            || is_stackerror_notification(header)
            || is_dso_buffer(header);
    };

    auto contains_complete_frame = [=] (const u32 *begin, const u32 *end) -> bool
    {
        assert(end - begin > 0);
        assert(is_good_header(*begin));
        // TODO: handle continuations

        if (is_dso_buffer(*begin))
            return std::find_if(begin, end,
                                [] (u32 word) { return word == 0xC0000000u; }) != end;

        auto len = get_frame_length(*begin);
        auto avail = end - begin;

        return avail >= len + 1;
    };

#ifndef __WIN32
    prctl(PR_SET_NAME,"cmd_pipe_reader",0,0,0);
#endif

    spdlog::info("cmd_pipe_reader starting");

    Buffer buffer;
    // A bit misleading: this is 1M elements, not 1MB space.
    buffer.ensureFreeSpace(util::Megabytes(1));

    while (!context.quit)
    {
        while (buffer.used)
        {

            //util::log_buffer(cout, buffer, "cmd_pipe_reader buffer");


            if (!is_good_header(buffer[0]))
            {
                // TODO: count error, seek to next known buffer start, count skipped bytes
                throw std::runtime_error("bad header in buffer");
            }

            if (contains_complete_frame(buffer.begin(), buffer.end()))
            {
                if (is_stackerror_notification(buffer[0]))
                {
                    ++context.counters.errorBuffers;
                    buffer.consume(get_frame_length(buffer[0]) + 1);
                }
                else if (is_dso_buffer(buffer[0]))
                {
                    ++context.counters.dsoBuffers;
                    auto dsoEnd = std::find_if(buffer.begin(), buffer.end(),
                                               [] (u32 word) { return word == 0xC0000000u; }) + 1;
                    buffer.consume(dsoEnd - buffer.begin());
                }
                else if (is_super_buffer(buffer[0]))
                {
                    auto pendingResponse = context.pendingSuper.access();
                    fullfill_pending_response(
                        pendingResponse.ref(), {}, &buffer[0], get_frame_length(buffer[0] + 1));
                    buffer.consume(get_frame_length(buffer[0]) + 1);
                }
                else if (is_stack_buffer(buffer[0]))
                {
                    // TODO: handle stack continuations: copy parts together,
                    // consuming parts from the buffer in the process.
                    auto pendingResponse = context.pendingStack.access();
                    fullfill_pending_response(
                        pendingResponse.ref(), {}, &buffer[0], get_frame_length(buffer[0] + 1));
                    buffer.consume(get_frame_length(buffer[0]) + 1);
                }
            }
            else
            {
                // Move remaining data to buffer front and ensure there's free space in
                // the buffer.
                buffer.pack();
                // A bit misleading: this is 1M elements, not 1MB space.
                buffer.ensureFreeSpace(util::Megabytes(1));
                break;
            }
        }

        size_t bytesTransferred = 0;
        auto ec = context.mvlc->read(
            Pipe::Command,
            reinterpret_cast<u8 *>(buffer.writeBegin()),
            std::min(buffer.free() * sizeof(u32), usb::USBSingleTransferMaxBytes),
            bytesTransferred);

        buffer.used += bytesTransferred / sizeof(u32);

        spdlog::trace("received {} bytes", bytesTransferred);

        ++context.counters.reads;
        context.counters.bytesRead += bytesTransferred;
        if (ec == ErrorType::Timeout)
            ++context.counters.timeouts;


        if (ec == ErrorType::ConnectionError)
            context.quit = true;
    }

    fullfill_pending_response(
        context.pendingSuper.access().ref(),
        make_error_code(MVLCErrorCode::IsDisconnected));

    fullfill_pending_response(
        context.pendingStack.access().ref(),
        make_error_code(MVLCErrorCode::IsDisconnected));

    SPDLOG_TRACE("cmd_pipe_reader exiting");
    spdlog::info("cmd_pipe_reader exiting");
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);

    std::string host;
    bool showHelp = false;
    bool logTrace = false;
    unsigned secondsToRun = 2;

    auto cli
        = lyra::help(showHelp)
        | lyra::opt(host, "hostname")["--eth"]("mvlc hostname")
        | lyra::opt(logTrace)["--trace"]("enable trace logging")
        | lyra::arg(secondsToRun, "secondsToRun")
        ;
    auto parseResult = cli.parse({ argc, argv});

    if (!parseResult)
        return 1;

    if (showHelp)
    {
        cout << cli << "\n";
        return 0;
    }

    if (logTrace)
        spdlog::set_level(spdlog::level::trace);

    std::unique_ptr<MVLCBasicInterface> mvlc;

    if (host.empty())
        mvlc = std::make_unique<usb::Impl>();
    else
        mvlc = std::make_unique<eth::Impl>(host);

    assert(mvlc);

    try
    {
        if (auto ec = mvlc->connect())
            throw ec;

        ReaderContext readerContext;
        readerContext.mvlc = mvlc.get();
        readerContext.quit = false;

        size_t superTransactions = 0;
        size_t stackTransactions = 0;

        std::thread readerThread(cmd_pipe_reader, std::ref(readerContext));

        // for testing delay a bit so the reader does run into a few timeouts right away
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto tStart = std::chrono::steady_clock::now();

        while (true)
        {
#define RUN_SUPER_TEST 1
#if RUN_SUPER_TEST
            // test some super commands
            {
                spdlog::trace("sending writeLocal+readLocal super cmds");
                SuperCommandBuilder scb;
                scb.addReferenceWord(0x1337);
                scb.addWriteLocal(stacks::StackMemoryBegin, 0x87654321u);
                scb.addReadLocal(stacks::StackMemoryBegin);
                auto cmdBuffer = make_command_buffer(scb);
                std::vector<u32> responseBuffer;

                auto cmdGuard = std::unique_lock<TicketMutex>(readerContext.cmdLock);
                auto rf = set_pending_super_response(readerContext, responseBuffer);

                size_t bytesWritten = 0;

                auto ec = mvlc->write(
                    Pipe::Command,
                    reinterpret_cast<const u8 *>(cmdBuffer.data()),
                    cmdBuffer.size() * sizeof(u32),
                    bytesWritten);

                if (ec)
                {
                    cout << "write failed: ec=" << ec.message() << endl;
                    throw ec;
                }

                // TODO: use wait_for with a timeout and use fullfill_pending_response()
                // passing a timeout error code then return rf.get()
                ec = rf.get();

                //if (responseBuffer.size())
                //    util::log_buffer(std::cout, responseBuffer,
                //                     "writeLocal+readLocal responseBuffer");

                if (ec)
                {
                    cout << "super test response error: ec=" << ec.message() << endl;
                    throw ec;
                }

                ++superTransactions;
            }
#endif // RUN_SUPER_TEST

#define RUN_STACK_TEST 1
#if RUN_STACK_TEST
            // Test a vme read
            {
                spdlog::trace("performing vmeRead");

                StackCommandBuilder stackBuilder;
                stackBuilder.addWriteMarker(0xdeadbeef);
                stackBuilder.addVMERead(0xffff6008, vme_amods::A32, VMEDataWidth::D16);


                SuperCommandBuilder superBuilder;
                superBuilder.addReferenceWord(0x1338);
                u16 cmdStackOffset = 1;
                superBuilder.addStackUpload(stackBuilder, CommandPipe, cmdStackOffset);
                superBuilder.addWriteLocal(stacks::Stack0OffsetRegister, cmdStackOffset);
                superBuilder.addWriteLocal(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);
                auto cmdBuffer = make_command_buffer(superBuilder);
                std::vector<u32> superResponse, stackResponse;

                auto cmdGuard = std::unique_lock<TicketMutex>(readerContext.cmdLock);
                auto superFuture = set_pending_super_response(readerContext, superResponse);
                auto stackFuture = set_pending_stack_response(readerContext, stackResponse);

                size_t bytesWritten = 0;

                auto ec = mvlc->write(
                    Pipe::Command,
                    reinterpret_cast<const u8 *>(cmdBuffer.data()),
                    cmdBuffer.size() * sizeof(u32),
                    bytesWritten);

                if (ec)
                {
                    cout << "write failed: ec=" << ec.message() << endl;
                    throw ec;
                }

                // TODO: use wait_for with a timeout and use fullfill_pending_response()
                // passing a timeout error code then return rf.get()
                auto superError = superFuture.get();

                if (superError)
                {
                    spdlog::warn("vmeRead super error: {}", superError.message());
                    throw superError;
                }

                if (superResponse.size())
                {
                    std::ostringstream oss;
                    util::log_buffer(oss, superResponse, "vmeRead super response");
                    spdlog::trace(oss.str());
                }

                auto stackError = stackFuture.get();

                if (stackError)
                {
                    spdlog::warn("vmeRead stack error: {}", stackError.message());
                    throw ec;
                }

                if (stackResponse.size())
                {
                    std::ostringstream oss;
                    util::log_buffer(oss, stackResponse, "vmeRead stack response");
                    spdlog::trace(oss.str());
                }

                ++stackTransactions;
            }
#endif // RUN_STACK_TEST

            auto tElapsed = std::chrono::steady_clock::now() - tStart;

            if (tElapsed >= std::chrono::seconds(10))
                break;
        }

        auto tElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tStart);

        // quit --------------------------
        readerContext.quit = true;
        readerThread.join();

        // --------------------------------

        auto superRate = 1000.0 * superTransactions / tElapsed.count();
        auto stackRate = 1000.0 * stackTransactions / tElapsed.count();

        spdlog::info("loop done, elapsed={}ms, superTransactions={}, superRate={}",
                     tElapsed.count(), superTransactions, superRate);

        spdlog::info("loop done, elapsed={}ms, stackTransactions={}, stackRate={}",
                     tElapsed.count(), stackTransactions, stackRate);

        spdlog::info("total reads={}, read timeouts={}, timeouts/reads={}",
                     readerContext.counters.reads,
                     readerContext.counters.timeouts,
                     (float)readerContext.counters.timeouts / readerContext.counters.reads
                    );

    }
    catch (const std::error_code &ec)
    {
        cout << "Error: " << ec.message() << "\n";
        return 1;
    }
}
