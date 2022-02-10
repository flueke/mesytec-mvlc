#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <lyra/lyra.hpp>

using std::cout;
using namespace mesytec::mvlc;

struct BaseContext
{
    MVLCBasicInterface *mvlc;
    std::atomic<bool> quit;
    std::atomic<size_t> transferCount;
    std::atomic<size_t> bytesTransferred;
    std::atomic<size_t> timeouts;
    std::error_code ec;
};

struct WriterContext: public BaseContext
{
    std::vector<u32> cmdBuffer;
};

struct ReaderContext: public BaseContext
{
    std::vector<u32> readBuffer;
};


void writer(WriterContext &context)
{
    SuperCommandBuilder scb;
    scb.addReferenceWord(0x1337);
    scb.addWriteLocal(stacks::StackMemoryBegin, 0x87654321u);
    scb.addReadLocal(stacks::StackMemoryBegin);
    auto cmdBuffer = make_command_buffer(scb);
    context.cmdBuffer = cmdBuffer;

    while (!context.quit)
    {
        size_t bytesTransferred = 0u;

        if (auto ec = context.mvlc->write(
                Pipe::Command,
                reinterpret_cast<const u8 *>(cmdBuffer.data()),
                cmdBuffer.size() * sizeof(decltype(cmdBuffer)::value_type),
                bytesTransferred
                ))
        {
            context.ec = ec;

            if (ec == ErrorType::Timeout)
                ++context.timeouts;
            else
                break;
        }

        context.transferCount += 1;
        context.bytesTransferred += bytesTransferred;
    }
}

void reader(ReaderContext &context)
{
    auto &readBuffer = context.readBuffer;
    readBuffer.resize(1024);

    size_t bytesTransferred = 0u;
    while (true)
    {

        if (auto ec = context.mvlc->read(
                Pipe::Command,
                reinterpret_cast<u8 *>(readBuffer.data()),
                readBuffer.size() * sizeof(decltype(context.readBuffer)::value_type),
                bytesTransferred))
        {
            context.ec = ec;

            if (ec == ErrorType::Timeout)
            {
                ++context.timeouts;
                if (context.quit)
                    break;
            }
            else
                break;
        }

        context.transferCount += 1;
        context.bytesTransferred += bytesTransferred;
    }
};

int main(int argc, char *argv[])
{
    std::string host;
    bool showHelp = false;
    unsigned secondsToRun = 10;

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

        WriterContext writerContext = {};
        writerContext.mvlc = mvlc.get();
        writerContext.quit = false;

        //WriterContext writerContext1 = {};
        //writerContext1.mvlc = mvlc.get();
        //writerContext1.quit = false;


        ReaderContext readerContext = {};
        readerContext.mvlc = mvlc.get();
        readerContext.quit = false;

        std::thread writerThread(writer, std::ref(writerContext));

        std::thread readerThread(reader, std::ref(readerContext));

        auto tStart = std::chrono::steady_clock::now();

        while (true)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - tStart);

            if (elapsed.count() > secondsToRun)
                break;
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        writerContext.quit = true;
        readerContext.quit = true;
        writerThread.join();
        readerThread.join();

        cout << "writes=" << writerContext.transferCount
            << ", bytesWritten=" << writerContext.bytesTransferred
            << ", timeouts=" << writerContext.timeouts
            << ", ec=" << writerContext.ec.message()
            << ", bytesWritten/writes="
            << (writerContext.bytesTransferred*1.0/writerContext.transferCount)
            << "\n"
            << "reads=" << readerContext.transferCount
            << ", bytesRead=" << readerContext.bytesTransferred
            << ", timeouts=" << readerContext.timeouts
            << ", ec=" << readerContext.ec.message()
            << ", bytesRead/reads="
            << (readerContext.bytesTransferred*1.0/readerContext.transferCount)
            << "\n";

        util::log_buffer(cout, writerContext.cmdBuffer, "writer cmd buffer",
                         16, 16);

        util::log_buffer(cout, readerContext.readBuffer, "last read buffer",
                         16, 16);
    }
    catch (const std::error_code &ec)
    {
        cout << "Error: " << ec.message() << "\n";
        return 1;
    }

    return 0;
}
