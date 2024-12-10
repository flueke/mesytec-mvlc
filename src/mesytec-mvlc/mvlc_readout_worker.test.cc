#include <gtest/gtest.h>
#include <cstring>
#include "mvlc_readout_worker.h"

using namespace mesytec::mvlc;

template<typename T> bool eq(const util::span<T> a, const util::span<T> b)
{
    if (a.size() != b.size())
        return false;

    return std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

TEST(fixup_usb_buffer, no_fixup_needed)
{
    {
        std::vector<u32> data =
        {
            0xF3000000
        };

        ReadoutBuffer tmpBuffer;
        auto input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

        auto output = fixup_usb_buffer(input, tmpBuffer);
        ASSERT_EQ(output.size(), input.size());
        ASSERT_TRUE(eq(input, output));
    }

    {
        std::vector<u32> data =
        {
            0xF3000001,
            0xAABBCCDD
        };

        ReadoutBuffer tmpBuffer;
        auto input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

        auto output = fixup_usb_buffer(input, tmpBuffer);
        ASSERT_EQ(output.size(), input.size());
        ASSERT_TRUE(eq(input, output));
        ASSERT_EQ(tmpBuffer.used(), 0);
    }

    {
        // has to follow the framing
        std::vector<u32> data =
        {
            0xF3000001,
            0x11111111,
            0xF3000002,
            0x22222222,
            0x33333333,
        };

        ReadoutBuffer tmpBuffer;
        auto input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

        auto output = fixup_usb_buffer(input, tmpBuffer);
        ASSERT_EQ(output.size(), input.size());
        ASSERT_TRUE(eq(input, output));
        ASSERT_EQ(tmpBuffer.used(), 0);
    }
}

TEST(fixup_usb_buffer, fixup_needed)
{
    {
        std::vector<u32> data =
        {
            0xF3000001,
        };

        ReadoutBuffer tmpBuffer;
        auto input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

        auto output = fixup_usb_buffer(input, tmpBuffer);
        ASSERT_EQ(output.size(), 0);
        ASSERT_EQ(tmpBuffer.used(), input.size());
    }

    {
        // has to follow the framing
        std::vector<u32> data =
        {
            0xF3000001,
            0x11111111,
            0xF3000002,
            0x22222222,
        };

        ReadoutBuffer tmpBuffer;
        auto input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

        auto output = fixup_usb_buffer(input, tmpBuffer);
        ASSERT_EQ(output.size(), 2 * sizeof(u32));

        // the first event of size 1 (== 2 words) is completely contained in the input and thus returned
        util::span<u8> inputSubspan(reinterpret_cast<u8 *>(data.data()), 2 * sizeof(u32));
        ASSERT_TRUE(eq(output, inputSubspan));
        ASSERT_EQ(tmpBuffer.used(), 2 * sizeof(u32));

        // In the using code the data in the tmp buffer is copied to the start
        // of the destination buffer of the next readout_*() call. Do the same
        // here to simulate this.

        data.clear();
        std::copy(reinterpret_cast<u32 *>(tmpBuffer.data()),
            reinterpret_cast<u32 *>(tmpBuffer.data() + tmpBuffer.used()),
            std::back_inserter(data));
        tmpBuffer.clear();

        // Add the missing word to complete the frame. This is data from the
        // next readout_*() call by the user.
        data.push_back(0x33333333);

        ASSERT_EQ(data, std::vector<u32>({ 0xF3000002, 0x22222222, 0x33333333 }));

        input = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));
        output = fixup_usb_buffer(input, tmpBuffer);

        std::vector<u32> expected = { 0xF3000002, 0x22222222, 0x33333333 };
        util::span<u8> expectedSpan(reinterpret_cast<u8 *>(expected.data()), expected.size() * sizeof(u32));

        ASSERT_EQ(output.size(), 3 * sizeof(u32));
        ASSERT_TRUE(eq(output, expectedSpan));
        ASSERT_EQ(tmpBuffer.used(), 0);
    }
}
