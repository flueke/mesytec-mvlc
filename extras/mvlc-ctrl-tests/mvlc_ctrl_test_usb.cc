#include "gtest/gtest.h"
#include <spdlog/spdlog.h>
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
}

TEST(MvlcUsb, ReadRegister)
{
    // TODO: move this into a setUp routine
    usb::Impl mvlc;
    auto ec = mvlc.connect();
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    SuperCommandBuilder cmdList;
    cmdList.addReadLocal(registers::hardware_id);

    auto request = make_command_buffer(cmdList);

    spdlog::info("request={:#08x}", fmt::join(request, ", "));
    size_t bytesWritten = 0u;
    const size_t bytesToWrite = request.size() * sizeof(u32);
    ec = mvlc.write(Pipe::Command, reinterpret_cast<const u8 *>(request.data()), bytesToWrite, bytesWritten);

    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(bytesToWrite, bytesWritten);

    //std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Linux: At this point the read timeout has been set to 0 at the end of
    // connect(). Reading small amounts of data immediately returns FT_TIMEOUT
    // and 0 bytes read. Starting from 1024 * 128, most times the expected
    // result of 12 bytes is returned, but not always.
    // The current APIv2 implementation doesn't run into problems because it
    // just reads in a loop.

    std::vector<u32> response(1024 * 64);
    const size_t responseCapacity = response.size() * sizeof(u32);
    size_t bytesRead = 0u;

    ec = mvlc.read(Pipe::Command, reinterpret_cast<u8 *>(response.data()), responseCapacity, bytesRead);

    spdlog::warn("ec={}, bytesRead={}", ec.message(), bytesRead);

    ASSERT_FALSE(ec) << ec.message();

    const size_t wordsRead = bytesRead / sizeof(u32);

    ASSERT_EQ(wordsRead, response.size());
}
