#include "gtest/gtest.h"
#include <ftd3xx.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>

using namespace mesytec::mvlc;

TEST(MvlcUsb, ConnectToFirstDevice)
{
    usb::Impl mvlc;

    ASSERT_EQ(mvlc.connectionType(), ConnectionType::USB);

    auto ec = mvlc.connect();
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    ec = mvlc.disconnect();
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_FALSE(mvlc.isConnected());
}

struct FtdiVersion
{
    u8 major;
    u8 minor;
    u16 build;
};

FtdiVersion ftdi_version_from_dword(DWORD value)
{
    FtdiVersion result = {};

    result.major = (value >> 24) & 0xffu;
    result.minor = (value >> 16) & 0xffu;
    result.build = value & 0xffffu;

    return result;
}

TEST(MvlcUsb, GetFtdiDriverVersions)
{
    spdlog::set_level(spdlog::level::trace);
    // TODO: move this into a setUp routine
    usb::Impl mvlc;
    auto ec = mvlc.connect();
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    auto ftHandle = mvlc.getHandle();
    ASSERT_NE(ftHandle, nullptr);

    DWORD driverVersionValue = {};

    if (auto ftSt = FT_GetDriverVersion(ftHandle, &driverVersionValue))
    {
        spdlog::error("FT_GetDriverVersion() returned {}", ftSt);
        ASSERT_EQ(ftSt, FT_OK);
    }

    auto driverVersion = ftdi_version_from_dword(driverVersionValue);

    spdlog::info("Ftdi Driver Version: {}.{}.{}", driverVersion.major, driverVersion.minor, driverVersion.build);

    DWORD libraryVersionValue;

    if (auto ftSt = FT_GetLibraryVersion(&libraryVersionValue))
    {
        spdlog::error("FT_GetLibraryVersion() returned {}", ftSt);
        ASSERT_EQ(ftSt, FT_OK);
    }

    auto libraryVersion = ftdi_version_from_dword(libraryVersionValue);

    spdlog::info("Ftdi Library Version: {}.{}.{}", libraryVersion.major, libraryVersion.minor, libraryVersion.build);
}

TEST(MvlcUsb, ReadRegister)
{
    spdlog::set_level(spdlog::level::trace);
    // TODO: move this into a setUp routine
    usb::Impl mvlc;
    auto ec = mvlc.connect();
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    for (size_t i = 0; i < 1000; ++i)
    {
        SuperCommandBuilder cmdList;
        cmdList.addReferenceWord(i); // XXX: Makes the response one word larger. 15 bytes in total now!
        cmdList.addReadLocal(registers::hardware_id);
        auto request = make_command_buffer(cmdList);

        spdlog::info("request={:#010x}", fmt::join(request, ", "));
        size_t bytesWritten = 0u;
        const size_t bytesToWrite = request.size() * sizeof(u32);
        ec = mvlc.write(Pipe::Command, reinterpret_cast<const u8 *>(request.data()), bytesToWrite, bytesWritten);

        ASSERT_FALSE(ec) << ec.message();
        ASSERT_EQ(bytesToWrite, bytesWritten);

        // std::this_thread::sleep_for(std::chrono::microseconds(100));

        // Linux: At this point the read timeout has been set to 0 at the end of
        // connect(). Reading small amounts of data immediately returns FT_TIMEOUT
        // and 0 bytes read. Starting from 1024 * 128, most times the expected
        // result of 12 bytes is returned, but not always.
        // The current APIv2 implementation doesn't run into problems because it
        // just reads in a loop.

#ifdef __linux__
        auto ec = usb::set_endpoint_timeout(mvlc.getHandle(), usb::get_endpoint(Pipe::Command, usb::EndpointDirection::In), 1000);
        ASSERT_FALSE(ec) << ec.message();
#endif

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

        ASSERT_FALSE(ec) << ec.message();
        ASSERT_TRUE(bytesRead % sizeof(u32) == 0);
        const size_t wordsRead = bytesRead / sizeof(u32);
        response.resize(wordsRead);
        spdlog::info("response={:#010x}", fmt::join(response, ", "));
        ASSERT_EQ(wordsRead, 4);
        ASSERT_EQ(response[1] & 0xffffu, i & 0xffffu);
        ASSERT_EQ(response[3], 0x5008u); // mvlc hardware id

        if (!ec && retryCount > 1)
        {
            auto tReadTotalEnd = std::chrono::steady_clock::now();
            auto readTotalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tReadTotalEnd - tReadTotalStart);
            spdlog::warn("read() succeeded after {} retries, total read time {} ms, cycle #{}", retryCount, readTotalElapsed.count(), i);
            return;
        }
    }
}
