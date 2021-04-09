#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <lyra/lyra.hpp>
#include <future>
#include <list>

using std::cout;
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
        std::atomic<size_t> bytesRead;
        std::atomic<size_t> timeouts;
        std::atomic<size_t> invalidHeaders;
        std::atomic<size_t> bytesSkipped;

        Counters()
            : bytesRead(0)
            , timeouts(0)
            , invalidHeaders(0)
            , bytesSkipped(0)
        {}
    };

    std::atomic<bool> quit;
    MVLCBasicInterface *mvlc;
    Protected<PendingResponse> pendingSuper;
    Protected<PendingResponse> pendingStack;
    Counters counters;
};

bool fullfill_pending_response(
    PendingResponse &pr,
    const std::error_code &ec,
    const std::vector<u32> &contents = {})
{
    if (pr.pending)
    {
        pr.pending = false;

        if (pr.dest)
            *pr.dest = contents;

        pr.promise.set_value(ec);
        return true;
    }

    return false;
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
        }

        u32 operator[](size_t index) const
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
            return std::find_if(begin, end, 0xC0000000u) != end;


    };

    Buffer buffer;
    buffer.mem.resize(util::Megabytes(1));

    while (!context.quit)
    {
        while (buffer.used)
        {
            if (!is_good_header(buffer[0]))
            {
                // TODO: count error, seek to next known buffer start, count skipped bytes
                throw std::runtime_error("bad header in buffer");
            }

            if (contains_complete_frame(buffer.begin(), buffer.end()))
            {
                // TODO: handle frame based on type
            }
            else
                break;
        }


        // TODO: move remaining data to buffer front and ensure there's free
        // space in the buffer

        // TODO; read fixed size chunks instead of current buffer size which may grow.
        // FIXME: start at an offset. parts of the buffer may be used
        size_t bytesTransferred = 0;
        auto ec = context.mvlc->read(
            Pipe::Command,
            reinterpret_cast<u8 *>(context.readBuffer.data()),
            context.readBuffer.size() * sizeof(u32),
            bytesTransferred);

        // TODO: buffer bookkeeping

        if (ec == ErrorType::ConnectionError)
            context.quit = true;
    }
#endif
}

int main(int argc, char *argv[])
{
    std::string host;
    bool showHelp = false;
    unsigned secondsToRun = 2;

    auto cli
        = lyra::help(showHelp)
        | lyra::opt(host, "hostname")["--eth"]("mvlc hostname")
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
    }
    catch (const std::error_code &ec)
    {
        cout << "Error: " << ec.message() << "\n";
        return 1;
    }
}
