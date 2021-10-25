#ifndef __MESYTEC_MVLC_DATA_FILTER_H__
#define __MESYTEC_MVLC_DATA_FILTER_H__

#include <array>
#include <string>
#include "mesytec-mvlc/util/bits.h"

namespace mesytec
{
namespace mvlc
{
namespace util
{

// Always doing a bit gather is a speedup if bmi2 is available, otherwise
// branching is faster.
#ifdef __BMI2__
#define A2_DATA_FILTER_ALWAYS_GATHER
#endif

static const s32 FilterSize = 32;

struct DataFilter
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

struct CacheEntry
{
    u32 extractMask = 0;
    u8 extractBits  = 0;
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
    bool needGather = false;
    u8 extractShift = 0;
#endif
};

DataFilter make_filter(const std::string &filter, s32 wordIndex = -1);

inline bool matches(const DataFilter &filter, u32 value, s32 wordIndex = -1)
{
    return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
        && ((value & filter.matchMask) == filter.matchValue);
}

CacheEntry make_cache_entry(const DataFilter &filter, char marker);

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

std::string to_string(const DataFilter &filter);

}
}
}

#endif /* __MESYTEC_MVLC_DATA_FILTER_H__ */
