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
                std::string address("mvlc-0066");

                if (char *envAddress = getenv("MVLC_TEST_ETH_ADDR"))
                    address = envAddress;

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

#ifndef _WIN32
TEST_P(MVLCTestBase, TestRegisterReadWriteMultiThreaded)
{
    auto ec = mvlc.connect();
    ASSERT_TRUE(!ec) << ec.message();
    ASSERT_TRUE(mvlc.isConnected());

    auto test_fun = [&]()
    {
        for (int i=0; i<100; ++i)
        {
            auto ec = mvlc.writeRegister(stacks::StackMemoryBegin, 0);
            ASSERT_TRUE(!ec) << ec.message();

            u32 value = 0;

            ec = mvlc.readRegister(stacks::StackMemoryBegin, value);
            ASSERT_TRUE(!ec) << ec.message();

            ec = mvlc.writeRegister(stacks::StackMemoryBegin, 0x87654321);
            ASSERT_TRUE(!ec) << ec.message();

            ec = mvlc.readRegister(stacks::StackMemoryBegin, value);
            ASSERT_TRUE(!ec) << ec.message();
        }
    };

    std::vector<std::future<void>> futures;

    for (int i=0; i<10; ++i)
        futures.emplace_back(std::async(std::launch::async, test_fun));

    for (auto &f: futures)
        f.get();
}
#endif


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
        sb.addVMEBlockRead(i*4, 0x09, 65535);

    auto stackBuffer = make_stack_buffer(sb);

    ec = mvlc.uploadStack(DataPipe, stacks::ImmediateStackEndWord, stackBuffer);

    ASSERT_TRUE(!ec) << ec.message();

    auto readBuffer = read_stack_from_memory(mvlc, stacks::ImmediateStackEndWord);

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
        sb.addVMEBlockRead(i*4, 0x09, 65535);

    auto stackBuffer = make_stack_buffer(sb);

    spdlog::info("uploading stack of size {} (bytes={})",
                 stackBuffer.size(), stackBuffer.size() * sizeof(stackBuffer[0]));

    auto tStart = std::chrono::steady_clock::now();
    ec = mvlc.uploadStack(DataPipe, stacks::ImmediateStackEndWord, stackBuffer);
    auto elapsed = std::chrono::steady_clock::now() - tStart;

    spdlog::info("stack upload took {} ms",
                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

    ASSERT_TRUE(!ec) << ec.message();

    spdlog::info("reading back stack memory");

    tStart = std::chrono::steady_clock::now();
    auto readBuffer = read_stack_from_memory(mvlc, stacks::ImmediateStackEndWord);
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
        sb.addVMEBlockRead(i*4, 0x09, 65535);

    auto stackBuffer = make_stack_buffer(sb);

    ec = mvlc.uploadStack(DataPipe, stacks::ImmediateStackEndWord, stackBuffer);

    // Should fail due to exceeding the stack memory area
    ASSERT_TRUE(ec) << ec.message();
}

INSTANTIATE_TEST_CASE_P(MVLCTest, MVLCTestBase, ::testing::Values("eth", "usb"));
