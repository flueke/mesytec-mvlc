#include "gtest/gtest.h"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdlib.h>

#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;
using std::cout;
using std::endl;

class MVLCTestBase: public ::testing::TestWithParam<const char *>
{
    public:
        MVLCTestBase()
        {
            //spdlog::set_level(spdlog::level::trace);
            get_logger("mvlc_uploadStack")->set_level(spdlog::level::debug);
            get_logger("cmd_pipe_reader")->set_level(spdlog::level::debug);
            const std::string mvlcType = GetParam();

            if (mvlcType == "usb")
            {
                spdlog::info("MVLCTestBase using MVLC_USB");
                mvlc = make_mvlc_usb();
            }
            else if (mvlcType == "eth")
            {
                //std::string address("mvlc-0066");
                std::string address;

                if (char *envAddress = getenv("MVLC_TEST_ETH_ADDR"))
                    address = envAddress;

                if (address.empty())
                {
                    spdlog::warn("No MVLC ETH address given. Set MVLC_TEST_ETH_ADDR in the environment.");
                }

                spdlog::info("MVLCTestBase using MVLC_ETH (address={})", address);
                mvlc = make_mvlc_eth(address);
            }
        }

        ~MVLCTestBase()
        {
        }


        MVLC mvlc;
};

TEST_P(MVLCTestBase, TestReconnect)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    ec = mvlc.disconnect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_FALSE(mvlc.isConnected());

    ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());
}

TEST_P(MVLCTestBase, TestRegisterReadWrite)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    ec = mvlc.writeRegister(stacks::StackMemoryBegin, 0);
    ASSERT_TRUE(!ec) << ec.message();

    u32 value = 1234;

    ec = mvlc.readRegister(stacks::StackMemoryBegin, value);
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_EQ(value, 0u);

    ec = mvlc.writeRegister(stacks::StackMemoryBegin, 0x87654321);
    ASSERT_TRUE(!ec) << ec.message();

    ec = mvlc.readRegister(stacks::StackMemoryBegin, value);
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_EQ(value, 0x87654321);
}

TEST_P(MVLCTestBase, TestRegisterReadWriteMultiThreaded)
{
    static const unsigned ThreadCount = 4;      // total number of threads to spawn
    static const unsigned LoopCount   = 1000;   // total number of loops each thread performs

    //mesytec::mvlc::get_logger("cmd_pipe_reader")->set_level(spdlog::level::trace);

    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    auto test_fun = [&](unsigned threadIndex)
    {
        spdlog::info("Started test thread {}/{}", threadIndex+1, ThreadCount);

        for (unsigned i=0; i<LoopCount; ++i)
        {
            // Note: checking the values read back from the stack memory works
            // as long as each thread writes to a distinct memory address.
            u32 writeValue = threadIndex * 4;
            u32 readValue = 0;

            auto ec = mvlc.writeRegister(stacks::StackMemoryBegin + threadIndex * 4, writeValue);
            ASSERT_TRUE(!ec) << ec.message();

            ec = mvlc.readRegister(stacks::StackMemoryBegin + threadIndex * 4, readValue);
            ASSERT_TRUE(!ec) << ec.message();
            ASSERT_EQ(readValue, writeValue);

            ec = mvlc.writeRegister(stacks::StackMemoryBegin + threadIndex * 4, 0x87654321);
            ASSERT_TRUE(!ec) << ec.message();

            ec = mvlc.readRegister(stacks::StackMemoryBegin + threadIndex * 4, readValue);
            ASSERT_TRUE(!ec) << ec.message();
            ASSERT_EQ(readValue, 0x87654321);
        }

        spdlog::info("Test thread {}/{} done", threadIndex+1, ThreadCount);
    };

    std::vector<std::future<void>> futures;

    for (unsigned i=0; i<ThreadCount; ++i)
        futures.emplace_back(std::async(std::launch::async, test_fun, i));

    for (auto &f: futures)
        f.get();
}


namespace
{
    // Starts reading from stackMemoryOffset.
    // Checks for StackStart, reads until StackEnd is found or StackMemoryEnd
    // is reached. Does not place StackStart and StackEnd in the result buffer,
    // meaning only the actual stack contents are returned.
    std::vector<u32> read_stack_from_memory(MVLC &mvlc, u16 stackMemoryOffset)
    {
        u16 readAddress = stacks::StackMemoryBegin + stackMemoryOffset;
        u32 stackWord = 0u;

        if (auto ec = mvlc.readRegister(readAddress, stackWord))
            throw ec;

        readAddress += AddressIncrement;

        if (extract_frame_info(stackWord).type !=
            static_cast<u8>(stack_commands::StackCommandType::StackStart))
        {
            throw std::runtime_error(
                fmt::format("Stack memory does not begin with StackStart (0xF3): 0x{:08X}",
                            stackWord));
        }

        std::vector<u32> result;

        while (readAddress < stacks::StackMemoryEnd)
        {
            if (auto ec = mvlc.readRegister(readAddress, stackWord))
                throw  ec;

            readAddress += AddressIncrement;

            if (extract_frame_info(stackWord).type ==
                static_cast<u8>(stack_commands::StackCommandType::StackEnd))
                break;

            result.push_back(stackWord);
        }

        return result;
    }
}

TEST_P(MVLCTestBase, TestUploadShortStack)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    StackCommandBuilder sb;

    for (int i=0; i<10; ++i)
        sb.addVMEBlockRead(i*4, vme_amods::BLT32, 65535);

    auto stackBuffer = make_stack_buffer(sb);
    static const u16 uploadAddress = 512 * 4;

    ec = mvlc.uploadStack(DataPipe, uploadAddress, stackBuffer);

    ASSERT_TRUE(!ec) << ec.message();

    auto readBuffer = read_stack_from_memory(mvlc, uploadAddress);

    //log_buffer(get_logger("test"), spdlog::level::info, readBuffer, "stack memory");

    ASSERT_EQ(stackBuffer, readBuffer);
}

TEST_P(MVLCTestBase, TestUploadLongStack)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    StackCommandBuilder sb;

    for (int i=0; i<400; ++i)
        sb.addVMEBlockRead(i*4, vme_amods::BLT32, 65535);

    auto stackBuffer = make_stack_buffer(sb);
    static const u16 uploadAddress = 512 * 4;

    spdlog::info("uploading stack of size {} (bytes={})",
                 stackBuffer.size(), stackBuffer.size() * sizeof(stackBuffer[0]));

    auto tStart = std::chrono::steady_clock::now();
    ec = mvlc.uploadStack(DataPipe, uploadAddress, stackBuffer);
    auto elapsed = std::chrono::steady_clock::now() - tStart;

    spdlog::info("stack upload took {} ms",
                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

    ASSERT_TRUE(!ec) << ec.message();

    spdlog::info("reading back stack memory");

    tStart = std::chrono::steady_clock::now();
    auto readBuffer = read_stack_from_memory(mvlc, uploadAddress);
    elapsed = std::chrono::steady_clock::now() - tStart;

    spdlog::info("stack memory read took {} ms",
                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

    //log_buffer(get_logger("test"), spdlog::level::info, readBuffer, "stack memory");

    ASSERT_EQ(stackBuffer, readBuffer);
}

TEST_P(MVLCTestBase, TestUploadExceedStackMem)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    StackCommandBuilder sb;

    for (int i=0; i<1000; ++i)
        sb.addVMEBlockRead(i*4, vme_amods::BLT32, 65535);

    auto stackBuffer = make_stack_buffer(sb);
    static const u16 uploadAddress = stacks::StackMemoryWords - 100;

    ec = mvlc.uploadStack(DataPipe, uploadAddress, stackBuffer);

    // Should fail due to exceeding the stack memory area
    ASSERT_EQ(ec, MVLCErrorCode::StackMemoryExceeded);
}

auto name_generator = [] (const ::testing::TestParamInfo<MVLCTestBase::ParamType> &info)
{
    return info.param;
};

INSTANTIATE_TEST_SUITE_P(MVLCTest, MVLCTestBase,
    ::testing::Values("eth", "usb"),
    [] (const auto &info) { return info.param; });
