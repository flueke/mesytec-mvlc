#include <cassert>
#include <spdlog/spdlog.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);
    // TODO: move this into a setUp routine
    usb::Impl mvlc;
    auto ec = mvlc.connect();
    assert(!ec);
    assert(mvlc.isConnected());

for (size_t i=0; i<1'000'000; ++i)
{
    SuperCommandBuilder cmdList;
    cmdList.addReferenceWord(i); // XXX: Makes the response one word larger. 15 bytes in total now!
    cmdList.addReadLocal(registers::hardware_id);
    auto request = make_command_buffer(cmdList);

    spdlog::info("request={:#010x}", fmt::join(request, ", "));
    size_t bytesWritten = 0u;
    const size_t bytesToWrite = request.size() * sizeof(u32);
    ec = mvlc.write(Pipe::Command, reinterpret_cast<const u8 *>(request.data()), bytesToWrite, bytesWritten);

    assert(!ec);
    assert(bytesToWrite == bytesWritten);

    //std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Linux: At this point the read timeout has been set to 0 at the end of
    // connect(). Reading small amounts of data immediately returns FT_TIMEOUT
    // and 0 bytes read. Starting from 1024 * 128, most times the expected
    // result of 12 bytes is returned, but not always.
    // The current APIv2 implementation doesn't run into problems because it
    // just reads in a loop.

    static const size_t responseCapacityInBytes = 4 * sizeof(u32);
    std::vector<u32> response(responseCapacityInBytes / sizeof(u32));
    const size_t responseCapacity = response.size() * sizeof(u32);
    size_t bytesRead = 0u;
    size_t retryCount = 0u;
    static const size_t ReadRetryMaxCount = 20;
    auto tReadTotalStart = std::chrono::steady_clock::now();

    while (retryCount < ReadRetryMaxCount)
    {
        auto tReadStart = std::chrono::steady_clock::now();
        ec = mvlc.read(Pipe::Command, reinterpret_cast<u8 *>(response.data()), responseCapacity, bytesRead);
        auto tReadEnd = std::chrono::steady_clock::now();
        auto readElapsed = std::chrono::duration_cast<std::chrono::microseconds>(tReadEnd - tReadStart);
        spdlog::info("read(): ec={}, bytesRequested={}, bytesRead={}, read took {} µs", ec.message(), responseCapacity, bytesRead, readElapsed.count());

        if (ec != ErrorType::Timeout)
            break;

        spdlog::warn("read() timed out, retrying!");
        ++retryCount;
    }

    assert(!ec);
    assert(bytesRead % sizeof(u32) == 0);
    const size_t wordsRead = bytesRead / sizeof(u32);
    response.resize(wordsRead);
    spdlog::info("response={:#010x}", fmt::join(response, ", "));
    assert(wordsRead == 4);
    assert((response[1] & 0xffffu) == (i & 0xffffu));
    assert(response[3] == 0x5008u); // mvlc hardware id

    if (!ec && retryCount > 1)
    {
        auto tReadTotalEnd = std::chrono::steady_clock::now();
        auto readTotalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tReadTotalEnd - tReadTotalStart);
        spdlog::warn("read() succeeded after {} retries, total read time {} ms, cycle #{}", retryCount, readTotalElapsed.count(), i);
        return 1;
    }
}
    return 0;
}
