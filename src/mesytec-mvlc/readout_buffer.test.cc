#include <gtest/gtest.h>

#include "readout_buffer.h"

using namespace mesytec::mvlc;
TEST(ReadoutBuffer, BasicUsage)
{
    ReadoutBuffer buffer(1024);

    EXPECT_EQ(buffer.capacity(), 1024);
    EXPECT_EQ(buffer.used(), 0);
    EXPECT_EQ(buffer.free(), 1024);
    EXPECT_TRUE(buffer.empty());

    const char *data = "Hello, world!";
    size_t dataSize = std::strlen(data);

    // Write data into the buffer
    std::memcpy(buffer.data(), data, dataSize);
    buffer.setUsed(dataSize);

    EXPECT_EQ(buffer.used(), dataSize);
    EXPECT_EQ(std::string((const char *)buffer.data(), buffer.used()), "Hello, world!");

    // Consume some bytes
    buffer.consume(7);
    EXPECT_EQ(buffer.used(), 6);
    EXPECT_EQ(std::string((const char *)buffer.data(), buffer.used()), "world!");
}
