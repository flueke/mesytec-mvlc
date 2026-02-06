#ifndef C1FFD3EE_8AC7_4CDB_95F4_029E84A1F2AF
#define C1FFD3EE_8AC7_4CDB_95F4_029E84A1F2AF

#include <cassert>
#include <cstring>
#include <string_view>
#include <vector>

#include "int_types.h"
#include "logging.h"

namespace mesytec::mvlc
{

// Linear buffer with begin/end indices for efficient consumption of data from
// the front without constant reallocation.
class LinearBuffer
{
  public:
    LinearBuffer() = default;

    // Reserve initial capacity
    explicit LinearBuffer(size_t initialCapacity) { buffer_.reserve(initialCapacity); }

    // Get view of used data as u8
    std::basic_string_view<u8> viewU8() const
    {
        return std::basic_string_view<u8>(data(), used());
    }

    // Get view of used data as u32. Trailing bytes are ignored.
    std::basic_string_view<u32> viewU32() const
    {
        return std::basic_string_view<u32>(reinterpret_cast<const u32 *>(data()),
                                           used() / sizeof(u32));
    }

    // Get pointer to start of used data
    u8 *data() { return buffer_.data() + begin_; }
    const u8 *data() const { return buffer_.data() + begin_; }

    // Get pointer to end of used data (for writing)
    u8 *writePtr() { return buffer_.data() + end_; }

    // Number of bytes of used data
    size_t used() const { return end_ - begin_; }

    // Number of bytes available for writing without reallocation
    size_t available() const { return buffer_.size() - end_; }

    // Total capacity
    size_t capacity() const { return buffer_.size(); }

    // Is buffer empty
    bool empty() const { return begin_ == end_; }

    // Advance the write pointer after writing data
    void commit(size_t bytes)
    {
        assert(end_ + bytes <= buffer_.size());
        end_ += bytes;
    }

    // Consume bytes from the front
    void consume(size_t bytes)
    {
        assert(begin_ + bytes <= end_);
        begin_ += bytes;
        if (begin_ == end_)
        {
            begin_ = 0;
            end_ = 0;
        }
    }

    // Compact buffer: move used data to front
    void compact()
    {
        const auto prev_used = used();

        if (begin_ > 0 && end_ > begin_)
        {
            const size_t usedBytes = used();
            std::memmove(buffer_.data(), data(), usedBytes);
            begin_ = 0;
            end_ = usedBytes;
            spdlog::trace("Compacted buffer: moved {} bytes to front", usedBytes);
        }
        else if (begin_ == end_)
        {
            begin_ = 0;
            end_ = 0;
        }

        assert(used() == prev_used);
    }

    // Ensure at least 'needed' bytes are available for writing
    void ensureAvailable(size_t needed)
    {
        compact();

        if (available() < needed)
        {
            buffer_.resize(end_ + needed);
        }
    }

    // Clear the buffer
    void clear()
    {
        begin_ = 0;
        end_ = 0;
    }

    // Reset and optionally reserve new capacity
    void reset(size_t newCapacity = 0)
    {
        buffer_.clear();
        begin_ = 0;
        end_ = 0;
        if (newCapacity > 0)
        {
            buffer_.reserve(newCapacity);
        }
    }

  private:
    std::vector<u8> buffer_;
    size_t begin_ = 0; // Start of valid data
    size_t end_ = 0;   // One past end of valid data
};

} // namespace mesytec::mvlc

#endif /* C1FFD3EE_8AC7_4CDB_95F4_029E84A1F2AF */
