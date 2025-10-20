#ifndef EA7E715D_CA0E_40F7_A86F_25433386C414
#define EA7E715D_CA0E_40F7_A86F_25433386C414

#include <cstdint>
#include <vector>
#include <cstring>

static constexpr uint32_t MAGIC_PATTERN = 0xDEADBEEF;

struct TestBuffer
{
    uint32_t sequence_number;
    uint32_t magic;
    uint32_t buffer_size; // Number of uint32_t values following
    uint32_t checksum;
    // Followed by buffer_size uint32_t values with pattern
};

// Generate test data with verifiable pattern
inline std::vector<uint8_t> generateTestBuffer(uint32_t seq_num, size_t data_words = 256)
{
    TestBuffer header{.sequence_number = seq_num,
                      .magic = MAGIC_PATTERN,
                      .buffer_size = static_cast<uint32_t>(data_words),
                      .checksum = 0};

    std::vector<uint32_t> data;
    data.resize(data_words);

    // Generate predictable pattern: seq_num + index
    for (size_t i = 0; i < data_words; ++i)
    {
        data[i] = seq_num + static_cast<uint32_t>(i);
    }

    // Calculate checksum
    header.checksum = seq_num ^ MAGIC_PATTERN ^ data_words;
    for (uint32_t val: data)
    {
        header.checksum ^= val;
    }

    // Pack into byte buffer
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(header) + data.size() * sizeof(uint32_t));

    memcpy(buffer.data(), &header, sizeof(header));
    memcpy(buffer.data() + sizeof(header), data.data(), data.size() * sizeof(uint32_t));

    return buffer;
}

// Verify received buffer
inline bool verifyTestBuffer(const std::vector<uint8_t> &buffer, uint32_t expected_seq)
{
    if (buffer.size() < sizeof(TestBuffer))
        return false;

    TestBuffer header;
    memcpy(&header, buffer.data(), sizeof(header));

    if (header.sequence_number != expected_seq)
        return false;
    if (header.magic != MAGIC_PATTERN)
        return false;

    size_t expected_size = sizeof(header) + header.buffer_size * sizeof(uint32_t);
    if (buffer.size() != expected_size)
        return false;

    // Verify data pattern and checksum
    const uint32_t *data = reinterpret_cast<const uint32_t *>(buffer.data() + sizeof(header));
    uint32_t calc_checksum = expected_seq ^ MAGIC_PATTERN ^ header.buffer_size;

    for (uint32_t i = 0; i < header.buffer_size; ++i)
    {
        if (data[i] != expected_seq + i)
            return false;
        calc_checksum ^= data[i];
    }

    return calc_checksum == header.checksum;
}

#endif /* EA7E715D_CA0E_40F7_A86F_25433386C414 */
