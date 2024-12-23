#ifndef __MESYTEC_MVLC_DATA_FILTER_H__
#define __MESYTEC_MVLC_DATA_FILTER_H__

#include <array>
#include <optional>
#include <string>
#include <vector>
#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/util/bits.h"

namespace mesytec::mvlc::util
{

// Always doing a bit gather is a speedup if bmi2 is available, otherwise
// branching is faster.
#ifdef __BMI2__
#define A2_DATA_FILTER_ALWAYS_GATHER
#endif

static const s32 FilterSize = 32;

struct MESYTEC_MVLC_EXPORT DataFilter
{
    std::array<char, FilterSize> filter;
    u32 matchMask  = 0;
    u32 matchValue = 0;
    s32 matchWordIndex = -1;

    // bool operator==(const DataFilter &o) const = default; // c++20
    inline bool operator==(const DataFilter &o) const
    {
        return (filter == o.filter
                && matchMask == o.matchMask
                && matchValue == o.matchValue
                && matchWordIndex == o.matchWordIndex);
    }
};

struct MESYTEC_MVLC_EXPORT CacheEntry
{
    u32 extractMask = 0;
    u8 extractBits  = 0;
    bool needGather = false;
    u8 extractShift = 0;
};

MESYTEC_MVLC_EXPORT DataFilter make_filter(const std::string &filter, s32 wordIndex = -1);

inline bool matches(const DataFilter &filter, u32 value, s32 wordIndex = -1)
{
    return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
        && ((value & filter.matchMask) == filter.matchValue);
}

MESYTEC_MVLC_EXPORT CacheEntry make_cache_entry(const DataFilter &filter, char marker);

// Note: a match is assumed.
inline u32 extract(const CacheEntry &cache, u32 value)
{
#ifdef A2_DATA_FILTER_ALWAYS_GATHER
    u32 result = bit_gather(value, cache.extractMask);
#else
    u32 result = ((value & cache.extractMask) >> cache.extractShift);

    if (cache.needGather)
    {
        result = bit_gather(result, cache.extractMask >> cache.extractShift);
    }
#endif
    return result;
}

// Note: a match is assumed.
inline u32 extract(const DataFilter &filter, u32 value, char marker)
{
    auto cache = make_cache_entry(filter, marker);
    return extract(cache, value);
}

inline u8 get_extract_bits(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractBits;
}

inline u32 get_extract_mask(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractMask;
}

inline u8 get_extract_shift(const DataFilter &filter, char marker)
{
    return make_cache_entry(filter, marker).extractShift;
}

MESYTEC_MVLC_EXPORT std::string to_string(const DataFilter &filter);

struct MESYTEC_MVLC_EXPORT FilterWithCaches
{
    DataFilter filter;
    std::vector<char> markers;
    std::vector<CacheEntry> caches;
};

FilterWithCaches MESYTEC_MVLC_EXPORT make_filter_with_caches(const std::string &pattern);
std::optional<CacheEntry> MESYTEC_MVLC_EXPORT get_cache_entry(const FilterWithCaches &filters, char marker);

inline bool matches(const FilterWithCaches &filter, u32 value, s32 wordIndex = -1)
{
    return matches(filter.filter, value, wordIndex);
}

inline std::optional<u64> extract(const FilterWithCaches &filters, u32 value, char marker)
{
    if (auto cache = get_cache_entry(filters, std::tolower(marker)))
    {
        return extract(*cache, value);
    }
    return std::nullopt;
}

}

#endif /* __MESYTEC_MVLC_DATA_FILTER_H__ */
