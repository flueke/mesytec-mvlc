#include "mini_daq_callbacks.h"
#include <fmt/format.h>

using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace mini_daq
{

readout_parser::ReadoutParserCallbacks make_mini_daq_callbacks(MiniDAQStats &stats)
{
    readout_parser::ReadoutParserCallbacks callbacks;

    callbacks.beginEvent = [&stats] (int eventIndex)
    {
        stats.eventHits[eventIndex]++;
    };

    callbacks.modulePrefix = [&stats] (int ei, int mi,  const u32 *data, u32 size)
    {
        auto index = std::make_pair(ei, mi);

        ++stats.modulePrefixHits[index];

        auto &sizeInfo = stats.modulePrefixSizes[index];
        sizeInfo.min = std::min(sizeInfo.min, static_cast<size_t>(size));
        sizeInfo.max = std::max(sizeInfo.max, static_cast<size_t>(size));
        sizeInfo.sum += size;
    };

    callbacks.moduleDynamic = [&stats] (int ei, int mi,  const u32 *data, u32 size)
    {
        //cout << "ei=" << ei << ", mi=" << mi << ", data=" << data << ", size=" << size << endl;
        //util::log_buffer(cout, basic_string_view<u32>(data, size));
        auto index = std::make_pair(ei, mi);

        ++stats.moduleDynamicHits[index];

        auto &sizeInfo = stats.moduleDynamicSizes[index];
        sizeInfo.min = std::min(sizeInfo.min, static_cast<size_t>(size));
        sizeInfo.max = std::max(sizeInfo.max, static_cast<size_t>(size));
        sizeInfo.sum += size;
    };

    callbacks.moduleSuffix = [&stats] (int ei, int mi,  const u32 *data, u32 size)
    {
        auto index = std::make_pair(ei, mi);

        ++stats.moduleSuffixHits[index];

        auto &sizeInfo = stats.moduleSuffixSizes[index];
        sizeInfo.min = std::min(sizeInfo.min, static_cast<size_t>(size));
        sizeInfo.max = std::max(sizeInfo.max, static_cast<size_t>(size));
        sizeInfo.sum += size;
    };

    return callbacks;
}

std::ostream &dump_mini_daq_stats(std::ostream &out, const MiniDAQStats &stats)
{
    auto dump_hits_and_sizes = [&out] (
        const std::string &partTitle,
        const MiniDAQStats::ModulePartHits &hits,
        const MiniDAQStats::ModulePartSizes &sizes)
    {
        out << "module " + partTitle + " hits: ";

        for (const auto &kv: hits)
        {
            out << fmt::format(
                "ei={}, mi={}, hits={}; ",
                kv.first.first, kv.first.second, kv.second);
        }

        out << endl;

        out << "module " + partTitle + " sizes: ";

        for (const auto &kv: sizes)
        {
            out << fmt::format(
                "ei={}, mi={}, min={}, max={}, avg={}; ",
                kv.first.first, kv.first.second,
                kv.second.min,
                kv.second.max,
                kv.second.sum / static_cast<double>(hits.at(kv.first)));
        }

        out << endl;
    };

    out << "eventHits: ";
    for (const auto &kv: stats.eventHits)
        out << fmt::format("ei={}, hits={}, ", kv.first, kv.second);
    out << endl;

    dump_hits_and_sizes("prefix", stats.modulePrefixHits, stats.modulePrefixSizes);
    dump_hits_and_sizes("dynamic", stats.moduleDynamicHits, stats.moduleDynamicSizes);
    dump_hits_and_sizes("suffix", stats.moduleSuffixHits, stats.moduleSuffixSizes);

    return out;
}

} // end namespace mesytec
} // end namespace mvlc
} // end jnamespace mini_daq
