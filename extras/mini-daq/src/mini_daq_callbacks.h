#ifndef __MINI_DAQ_CALLBACKS_H__
#define __MINI_DAQ_CALLBACKS_H__

#include <limits>
#include <unordered_map>

#include <mesytec-mvlc/mvlc_readout_parser.h>

namespace mesytec
{
namespace mvlc
{
namespace mini_daq
{

struct PairHash
{
    template <typename T1, typename T2>
        std::size_t operator() (const std::pair<T1, T2> &pair) const
        {
            return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
        }
};

struct MiniDAQStats
{
    struct EventSizeInfo
    {
        size_t min = std::numeric_limits<size_t>::max();
        size_t max = 0u;
        size_t sum = 0u;
    };

    using ModulePartHits = std::unordered_map<std::pair<int, int>, size_t, PairHash>;
    using ModulePartSizes = std::unordered_map<std::pair<int, int>, EventSizeInfo, PairHash>;

    std::unordered_map<int, size_t> eventHits;

    ModulePartHits modulePrefixHits;
    ModulePartHits moduleDynamicHits;
    ModulePartHits moduleSuffixHits;

    ModulePartSizes modulePrefixSizes;
    ModulePartSizes moduleDynamicSizes;
    ModulePartSizes moduleSuffixSizes;
};

readout_parser::ReadoutParserCallbacks make_mini_daq_callbacks(MiniDAQStats &stats);

std::ostream &dump_mini_daq_stats(std::ostream &out, const MiniDAQStats &stats);

} // end namespace mesytec
} // end namespace mvlc
} // end jnamespace mini_daq

#endif /* __MINI_DAQ_CALLBACKS_H__ */
