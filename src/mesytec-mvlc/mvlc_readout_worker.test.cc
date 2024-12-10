#include <gtest/gtest.h>
#include "mvlc_readout_worker.h"

using namespace mesytec::mvlc;

TEST(fixup_usb_buffer, no_fixup_needed)
{
    std::vector<u32> data =
    {
        0xF3000000
    };

    ReadoutBuffer tmpBuffer;
    auto inputSpan = util::span<u8>(reinterpret_cast<u8 *>(data.data()), data.size() * sizeof(u32));

    auto output = fixup_usb_buffer(inputSpan, tmpBuffer);
    ASSERT_EQ(output.size(), 0);
}
