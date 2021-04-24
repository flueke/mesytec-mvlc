#include "gtest/gtest.h"
#include <fmt/format.h>
#include <iostream>
#include <spdlog/spdlog.h>

#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;
using std::cout;
using std::endl;

class MVLCTestBase: public ::testing::TestWithParam<const char *>
{
    public:
        MVLCTestBase()
        {
            std::string mvlcType = GetParam();

            if (mvlcType == "usb")
            {
                spdlog::info("usb");
                mvlc = make_mvlc_usb();
            }
            else if (mvlcType == "eth")
            {
                spdlog::info("eth");
                mvlc = make_mvlc_eth("mvlc-0007");
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

INSTANTIATE_TEST_CASE_P(MVLCTest, MVLCTestBase, ::testing::Values("eth", "usb"));
