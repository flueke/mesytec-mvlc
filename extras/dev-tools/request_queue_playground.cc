#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <lyra/lyra.hpp>
#include <future>
#include <list>

using std::cout;
using namespace mesytec::mvlc;

struct Buffer
{
    std::unique_ptr<u32> mem;
    size_t capacity;
    size_t used;

    u32 *begin() { return mem.get(); }
    u32 *end() { return begin() + used; }
};

using Buffers = std::vector<Buffer>;

struct PendingRequest
{
    enum Type { Super, Stack };
    Type type;
    u16 id;
    std::promise<std::error_code> promise;
    std::vector<u32> &dest;
};

struct RequestQueue
{
    std::list<PendingRequest> queue;
    std::mutex mutex;
    std::atomic<u16> nextId;
};

struct Context
{
    usb::Impl *mvlc;
    std::atomic<bool> quit;

    RequestQueue requestQueue;
};

std::future<std::error_code> make_super_request(
    MVLCBasicInterface &mvlc,
    RequestQueue &requestQueue,
    const std::vector<u32> &superBuffer,
    std::vector<u32> &responseDest)
{
    assert(((superBuffer[1] >> super_commands::SuperCmdShift) & super_commands::SuperCmdMask)
           == static_cast<u16>(super_commands::SuperCommandType::ReferenceWord));

    std::future<std::error_code> result;

    u16 id = requestQueue.nextId++;




    return result;
};


void requester(Context &context)
{
    std::list<std::vector<u32 *>> responses;

    while (!context.quit)
    {
        u16 id = context.requestQueue.nextId++;
        SuperCommandBuilder scb;
        scb.addReferenceWord(id);
        scb.addWriteLocal(stacks::StackMemoryBegin, 0x87654321u);
        scb.addReadLocal(stacks::StackMemoryBegin);
        auto cmdBuffer = make_command_buffer(scb);

        PendingRequest pr { PendingRequest::Stack, id, 
    }
};

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
