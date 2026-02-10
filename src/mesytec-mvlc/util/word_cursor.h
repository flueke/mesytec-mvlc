#ifndef F8126200_BCAD_475B_A5CE_114980339C0B
#define F8126200_BCAD_475B_A5CE_114980339C0B

#include "cpp_compat.h"
#include <cstdint>
#include <optional>

namespace mesytec::mvlc::util
{

struct WordCursor
{
    util::span<const std::uint32_t> buf;
    std::size_t pos = 0;

    bool empty() const { return pos >= buf.size(); }
    std::size_t remaining() const { return buf.size() - pos; }

    bool can_read(std::size_t n) const { return remaining() >= n; }

    // Peek without advancing
    std::optional<std::uint32_t> peek() const
    {
        if (empty())
            return std::nullopt;
        return buf[pos];
    }

    // Read one word and advance
    std::optional<std::uint32_t> read()
    {
        if (empty())
            return std::nullopt;
        return buf[pos++];
    }

    // Read N words as a subspan (view)
    std::optional<util::span<const std::uint32_t>> read_span(std::size_t n)
    {
        if (!can_read(n))
            return std::nullopt;
        auto s = buf.subspan(pos, n);
        pos += n;
        return s;
    }
};

} // namespace mesytec::mvlc

#endif /* F8126200_BCAD_475B_A5CE_114980339C0B */
