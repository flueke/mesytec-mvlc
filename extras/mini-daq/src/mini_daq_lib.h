#ifndef __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__
#define __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__

#include <limits>
#include <unordered_map>

#include <mesytec-mvlc/mvlc_readout_parser.h>

namespace mesytec
{
namespace mvlc
{
namespace mini_daq
{

// Helper enabling the use of std::pair as the key in std::unordered_map.
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

    // Event hit counts by eventIndex
    std::unordered_map<int, size_t> eventHits;

    // Part specific hit counts by (eventIndex, moduleIndex)
    ModulePartHits modulePrefixHits;
    ModulePartHits moduleDynamicHits;
    ModulePartHits moduleSuffixHits;

    // Part specific event size information by (eventIndex, moduleIndex)
    ModulePartSizes modulePrefixSizes;
    ModulePartSizes moduleDynamicSizes;
    ModulePartSizes moduleSuffixSizes;
};

// Creates a a set of readout parser callbacks which update the given
// MiniDAQStats when invoked by the parser.
readout_parser::ReadoutParserCallbacks make_mini_daq_callbacks(MiniDAQStats &stats);

// Formatted output of given stats structure.
std::ostream &dump_mini_daq_parser_stats(std::ostream &out, const MiniDAQStats &stats);

} // end namespace mesytec
} // end namespace mvlc
} // end namespace mini_daq

#endif /* __MESYTEC_MVLC_EXTRAS_MINI_DAQ_LIB_H__ */
