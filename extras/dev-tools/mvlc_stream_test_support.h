#ifndef EA7E715D_CA0E_40F7_A86F_25433386C414
#define EA7E715D_CA0E_40F7_A86F_25433386C414

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <mesytec-mvlc/util/int_types.h>

using namespace mesytec::mvlc;

static constexpr uint32_t MAGIC_PATTERN = 0xCAFEBABE;

#define PACK_AND_ALIGN4 __attribute__((packed, aligned(4)))

struct PACK_AND_ALIGN4 TestBuffer
{
    uint32_t magic;
    uint32_t sequence_number;
    uint32_t buffer_size; // Number of uint32_t values following
    // Followed by buffer_size uint32_t values with pattern
};

inline void generate_test_data(std::vector<u8> &dest, u32 bufferNumber, size_t dataWords = 256)
{
    dest.resize(sizeof(TestBuffer) + dataWords * sizeof(u32));
    auto header = reinterpret_cast<TestBuffer *>(dest.data());
    auto data = reinterpret_cast<u32 *>(dest.data() + sizeof(TestBuffer));

    header->magic = MAGIC_PATTERN;
    header->sequence_number = bufferNumber;
    header->buffer_size = static_cast<u32>(dataWords);

    std::generate(data, data + dataWords, [n = 0u]() mutable {
        return n++;
    });
}

inline bool verify_test_data(const std::vector<u8> &buffer, u32 expectedBufferNumber)
{
    if (buffer.size() < sizeof(TestBuffer))
        return false;

    auto header = reinterpret_cast<const TestBuffer *>(buffer.data());
    auto data = reinterpret_cast<const u32 *>(buffer.data() + sizeof(TestBuffer));

    if (header->magic != MAGIC_PATTERN)
        return false;
    if (header->sequence_number != expectedBufferNumber)
        return false;
    if (header->buffer_size != (buffer.size() - sizeof(TestBuffer)) / sizeof(u32))
        return false;

    for (u32 i = 0; i < header->buffer_size; ++i)
    {
        if (data[i] != i)
            return false;
    }

    return true;
}

#endif /* EA7E715D_CA0E_40F7_A86F_25433386C414 */
