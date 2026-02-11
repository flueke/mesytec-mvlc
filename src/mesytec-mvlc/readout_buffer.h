#ifndef __MESYTEC_MVLC_UTIL_READOUT_BUFFER_H__
#define __MESYTEC_MVLC_UTIL_READOUT_BUFFER_H__

#include <cassert>
#include <cstring>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/util/logging.h"
#include "mesytec-mvlc/util/string_view.hpp"

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT ReadoutBuffer
{
  public:
    explicit ReadoutBuffer(size_t capacity = 0)
        : m_buffer(capacity)
    {
    }

    s32 type() const { return m_type; }
    void setType(s32 t) { m_type = t; }
    void setType(ConnectionType t) { setType(static_cast<s32>(t)); }

    size_t bufferNumber() const { return m_number; }
    void setBufferNumber(size_t number) { m_number = number; }

    size_t capacity() const { return m_buffer.size(); }
    size_t used() const { return m_end - m_begin; }
    size_t free() const { return capacity() - used(); }
    bool empty() const { return used() == 0; }

    // Pointer to start of used data
    u8 *data() { return m_buffer.data() + m_begin; }
    const u8 *data() const { return m_buffer.data() + m_begin; }

    // Pointer to end of used data
    u8 *writePtr() { return m_buffer.data() + m_end; }

    // Consume bytes from the front
    void consume(size_t bytes)
    {
        assert(m_begin + bytes <= m_end);
        m_begin += bytes;
        if (m_begin == m_end)
        {
            m_begin = 0;
            m_end = 0;
        }
    }

    // Compact buffer: move used data to the front
    void compact()
    {
        [[maybe_unused]] const auto prev_used = used();

        if (m_begin > 0 && m_end > m_begin)
        {
            const size_t usedBytes = used();
            std::memmove(m_buffer.data(), data(), usedBytes);
            m_begin = 0;
            m_end = usedBytes;
            spdlog::trace("Compacted buffer: moved {} bytes to front", usedBytes);
        }
        else if (m_begin == m_end)
        {
            m_begin = 0;
            m_end = 0;
        }

        assert(used() == prev_used);
    }


    void ensureFreeSpace(size_t freeSpace)
    {
        compact();

        if (free() < freeSpace)
            m_buffer.resize(m_end + freeSpace);

        assert(free() >= freeSpace);
    }

    void ensureAvailable(size_t freeSpace)
    {
        ensureFreeSpace(freeSpace);
    }

    void clear()
    {
        m_begin = 0;
        m_end = 0;
    }

    void reset(size_t newCapacity = 0)
    {
        m_buffer.clear();
        if (newCapacity > 0)
            m_buffer.resize(newCapacity);
        m_begin = 0;
        m_end = 0;
    }

    // Advance the write pointer after writing data
    void use(size_t bytes)
    {
        assert(m_end + bytes <= capacity());
        m_end += bytes;
    }

    // Set the used size directly. Does not modify the begin pointer. Can be
    // used to truncate the buffer (consume from the end).
    void setUsed(size_t bytes)
    {
        assert(used() + bytes <= capacity());
        m_end = m_begin + bytes;
    }

    const std::vector<u8> &buffer() const { return m_buffer; }
    std::vector<u8> &buffer() { return m_buffer; }

    // Get a view of used data as u8
    nonstd::basic_string_view<u8> viewU8() const
    {
        return nonstd::basic_string_view< u8>(data(), used());
    }

    // Get a view of used data as u32. Trailing bytes are ignored.
    nonstd::basic_string_view<u32> viewU32() const
    {
        return nonstd::basic_string_view<u32>(reinterpret_cast<const u32 *>(data()),
                                              used() / sizeof(u32));
    }

    template <typename T> void push_back(const T &t)
    {
        static_assert(std::is_trivial<T>::value, "T must be a trivial type");
        ensureFreeSpace(sizeof(t));
        auto begin = reinterpret_cast<const u8 *>(&t);
        auto end = begin + sizeof(t);
        std::copy(begin, end, data() + used());
        use(sizeof(t));
    }

  private:
    s32 m_type = static_cast<s32>(ConnectionType::ETH);
    size_t m_number = 0;
    std::vector<u8> m_buffer;
    size_t m_begin = 0;
    size_t m_end = 0;
};

} // namespace mesytec::mvlc

#endif /* __MESYTEC_MVLC_UTIL_READOUT_BUFFER_H__ */
