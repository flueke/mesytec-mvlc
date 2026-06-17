#include "event_builder2.hpp"

#include <sstream>
#include <unordered_set>

#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/ticketmutex.h>
#include <spdlog/spdlog.h>

namespace mesytec::mvlc::event_builder2
{

IndexedTimestampFilterExtractor::IndexedTimestampFilterExtractor(const util::DataFilter &filter,
                                                                 s32 wordIndex, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
    , index_(wordIndex)
{
}

std::optional<u32> IndexedTimestampFilterExtractor::operator()(const u32 *data, size_t size)
{
    size_t idx = index_ < 0 ? size + index_ : index_;

    if (static_cast<size_t>(idx) < size && matches(filter_, data[idx]))
        return extract(filterCache_, data[idx]);

    return {};
}

TimestampFilterExtractor::TimestampFilterExtractor(const util::DataFilter &filter, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
{
}

std::optional<u32> TimestampFilterExtractor::operator()(const u32 *data, size_t size)
{
    for (const u32 *valuep = data; valuep < data + size; ++valuep)
    {
        if (matches(filter_, *valuep))
            return extract(filterCache_, *valuep);
    }

    return {};
}

u32 add_offset_to_timestamp(u32 ts, s32 offset)
{
    return (ts + offset) & TimestampMax; // Adjust and wrap around within 30-bit range
}

s64 timestamp_difference(s64 ts0, s64 ts1)
{
    s64 diff = ts0 - ts1;

    if (std::abs(diff) > TimestampHalf)
    {
        // overflow handling
        if (diff < 0)
            diff += TimestampMax;
        else
            diff -= TimestampMax;
    }

    return diff;
}

WindowMatchResult timestamp_match(s64 tsMain, s64 tsModule, u32 windowWidth)
{
    s64 diff = timestamp_difference(tsMain, tsModule);

    if (std::abs(diff) > windowWidth * 0.5)
    {
        if (diff >= 0)
            return {WindowMatch::too_old, static_cast<u32>(std::abs(diff))};
        else
            return {WindowMatch::too_new, static_cast<u32>(std::abs(diff))};
    }

    return {WindowMatch::in_window, static_cast<u32>(std::abs(diff))};
}

using TimestampType = s64;

struct ModuleStorage
{
    std::vector<u32> data;
    u32 prefixSize;
    u32 dynamicSize;
    u32 suffixSize;
    bool hasDynamic;
    std::optional<TimestampType> timestamp;

    explicit ModuleStorage(const ModuleData &md = {}, std::optional<TimestampType> ts = {})
        : data(md.data.data, md.data.data + md.data.size)
        , prefixSize(md.prefixSize)
        , dynamicSize(md.dynamicSize)
        , suffixSize(md.suffixSize)
        , hasDynamic(md.hasDynamic)
        , timestamp(ts)
    {
    }

    ModuleData to_module_data() const
    {
        return {
            .data = {data.data(), static_cast<u32>(data.size())},
            .prefixSize = prefixSize,
            .dynamicSize = dynamicSize,
            .suffixSize = suffixSize,
            .hasDynamic = hasDynamic,
        };
    }
};

inline bool size_consistency_check(const ModuleStorage &md)
{
    u64 partSum = md.prefixSize + md.dynamicSize + md.suffixSize;
    bool sumOk = partSum == md.data.size();
    // Note: cannot test the opposite: the current dynamicSize can be 0 but
    // hasDynamic can be true at the same time, e.g. from empty block reads.
    bool dynOk = md.dynamicSize > 0 ? md.hasDynamic : true;
    return sumOk && dynOk;
}

struct PerEventData
{
    // Timestamps of all modules of the incoming event are stored here.
    std::deque<TimestampType> allTimestamps;
    // Module data and extracted timestamps are stored here.
    std::vector<std::deque<ModuleStorage>> moduleDatas;
};

// Records module data and unmodified timestamps.
inline bool record_module_data(const ModuleData *moduleDataList, unsigned moduleCount,
                               const std::vector<ModuleConfig> &cfgs,
                               std::vector<std::deque<ModuleStorage>> &dest,
                               EventCounters &counters)
{
    assert(cfgs.size() == moduleCount);
    assert(dest.size() == moduleCount);
    assert(std::all_of(moduleDataList, moduleDataList + moduleCount,
                       [](const ModuleData &md)
                       { return mvlc::readout_parser::size_consistency_check(md); }));

    if (cfgs.size() != moduleCount)
        return false;

    if (dest.size() != moduleCount)
        return false;

    for (unsigned mi = 0; mi < moduleCount; ++mi)
    {
        const auto &mdata = moduleDataList[mi];
        const auto &mcfg = cfgs[mi];
        auto ts = mcfg.tsExtractor(mdata.data.data, mdata.data.size);

        ++counters.inputHits[mi];

        if (mdata.data.size == 0)
            ++counters.emptyInputs[mi];

        if (!mcfg.ignored && !ts.has_value() && mdata.data.size > 0)
        {
            ++counters.stampFailed[mi];

            spdlog::trace(
                "record_module_data: failed timestamp extraction, module{}, data.size={}, "
                "data={:#010x}",
                mi, mdata.data.size,
                fmt::join(mdata.data.data, mdata.data.data + mdata.data.size, ", "));
        }
        // no idea why i wanted to treat this case explicitly...
        else if (ts.has_value() && *ts == 0)
        {
            spdlog::trace(
                "record_module_data: extracted timestamp is zero, module{}, data.size={}, "
                "data={:#010x}",
                mi, mdata.data.size,
                fmt::join(mdata.data.data, mdata.data.data + mdata.data.size, ", "));
        }

        // this can push stamps without a value: the ones where the extraction failed.
        dest[mi].emplace_back(ModuleStorage(mdata, ts));

        ++counters.currentEvents[mi];
        counters.currentMem[mi] += mdata.data.size * sizeof(u32);
        counters.maxEvents[mi] = std::max(counters.maxEvents[mi], counters.currentEvents[mi]);
        counters.maxMem[mi] = std::max(counters.maxMem[mi], counters.currentMem[mi]);
    }

    return true;
}

std::string dump_counters(const EventCounters &counters)
{
    std::ostringstream oss;

    std::vector<size_t> sumOutputsDiscards(counters.outputHits.size());
    std::transform(std::begin(counters.outputHits), std::end(counters.outputHits),
                   std::begin(counters.discardsAge), std::begin(sumOutputsDiscards),
                   std::plus<size_t>());

    oss << fmt::format("module names:                  {}\n", fmt::join(counters.moduleNames, ", "));
    oss << fmt::format("incoming module data frames:   {}\n", fmt::join(counters.inputHits, ", "));
    oss << fmt::format("outgoing module data frames:   {}\n", fmt::join(counters.outputHits, ", "));
    oss << fmt::format("discards due to age:           {}\n", fmt::join(counters.discardsAge, ", "));
    oss << fmt::format("sum of outputs and discards:   {}\n", fmt::join(sumOutputsDiscards, ", "));
    oss << fmt::format("emptyInputs:                   {}\n", fmt::join(counters.emptyInputs, ", "));
    oss << fmt::format("timestamp extraction failures: {}\n", fmt::join(counters.stampFailed, ", "));

    oss << fmt::format("\n===== internal stats =====\n\n");
    oss << fmt::format("currentEvents:                 {}\n", fmt::join(counters.currentEvents, ", "));
    oss << fmt::format("maxEvents:                     {}\n", fmt::join(counters.maxEvents, ", "));

    oss << fmt::format("currentMem:                    {}\n", fmt::join(counters.currentMem, ", "));
    oss << fmt::format("maxMem:                        {}\n", fmt::join(counters.maxMem, ", "));
    oss << fmt::format("recordingFailed:               {}\n", counters.recordingFailed);
    oss << fmt::format("discardsNoStamp:               {}\n", counters.discardsNoStamp);
    oss << fmt::format("allTimestamps.size:            {}\n", counters.allTimestamps.size());
    oss << fmt::format("allTimestampsMaxSize:          {}\n", counters.allTimestampsMaxSize);

    oss << fmt::format("first 10 stamps: {}\n",
                       fmt::join(counters.allTimestamps.begin(),
                                 counters.allTimestamps.begin() +
                                     std::min<size_t>(10, counters.allTimestamps.size()),
                                 ", "));

    oss << fmt::format("last 10 stamps:  {}\n",
                       fmt::join(counters.allTimestamps.end() -
                                     std::min<size_t>(10, counters.allTimestamps.size()),
                                 counters.allTimestamps.end(), ", "));

    return oss.str();
}

bool fill(Histo &histo, double x)
{
    if (x < histo.binning.minValue)
    {
        ++histo.underflows;
    }
    else if (x >= histo.binning.maxValue)
    {
        ++histo.overflows;
    }
    else
    {
        size_t bin = static_cast<size_t>((x - histo.binning.minValue) /
                                         (histo.binning.maxValue - histo.binning.minValue) *
                                         histo.bins.size());

        assert(bin < histo.bins.size());

        if (bin < histo.bins.size())
        {
            ++histo.bins[bin];
            return true;
        }
    }

    return false;
}

// source: https://stackoverflow.com/a/15161034
struct pair_hash
{
    inline std::size_t operator()(const std::pair<size_t, size_t> &v) const
    {
        return v.first * 31 + v.second;
    }
};

Histo make_histo(const HistoBinning &binning, const std::string &title)
{
    Histo histo{};
    histo.binning = binning;
    histo.bins.resize(binning.binCount, 0);
    histo.title = title;
    return histo;
}

std::vector<ModuleDeltaHisto> create_dt_histograms(const std::vector<ModuleConfig> &moduleConfigs,
                                                   const HistoBinning &binConfig)
{
    std::vector<ModuleDeltaHisto> result;
    std::unordered_set<std::pair<size_t, size_t>, pair_hash> seen;

    for (size_t i = 0; i < moduleConfigs.size(); ++i)
    {
        for (size_t j = 0; j < moduleConfigs.size(); ++j)
        {
            if (i != j && seen.find({j, i}) == seen.end())
            {
                seen.insert({i, j});
                seen.insert({j, i});
                ModuleDeltaHisto mdh{};
                mdh.moduleIndexes = {i, j};
                mdh.histo = make_histo(binConfig, fmt::format("dt({}, {})", moduleConfigs[i].name,
                                                              moduleConfigs[j].name));
                result.emplace_back(mdh);
            }
        }
    }

    return result;
}

struct EventBuilder2::Private
{
    EventBuilderConfig cfg_;
    Callbacks callbacks_;
    void *userContext_;
    std::vector<PerEventData> perEventData_;
    // Used for the callback interface which requires a flat ModuleData array.
    std::vector<ModuleData> outputModuleData_;
    std::vector<ModuleStorage> outputModuleStorage_;
    mvlc::TicketMutex mutex_;


    // This is updated during runtime.
    BuilderCounters counters_;

    // Copy of the counters protected by a mutex. This is returned from
    // getCounters(). This deliberately has its own mutex to avoid deadlocks
    // when calling getCounters() from within a callback invoked by the event
    // builder.
    Protected<BuilderCounters> protectedCounters_;

    bool checkConsistency(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        auto a = (0 <= eventIndex && static_cast<size_t>(eventIndex) < cfg_.eventConfigs.size());
        auto b = cfg_.eventConfigs.size() == perEventData_.size();
        auto c = counters_.eventCounters.size() == perEventData_.size();

        if (a && b && c)
        {
            return std::all_of(moduleDataList, moduleDataList + moduleCount,
                               [](const ModuleData &md)
                               { return mvlc::readout_parser::size_consistency_check(md); });
        }

        return false;
    }

    bool checkModuleBuffers(int eventIndex)
    {
        auto a = (0 <= eventIndex && static_cast<size_t>(eventIndex) < cfg_.eventConfigs.size());
        auto b = cfg_.eventConfigs.size() == perEventData_.size();
        auto c = counters_.eventCounters.size() == perEventData_.size();

        if (!(a && b && c))
            return false;

        auto &eventData = perEventData_[eventIndex];
        auto moduleCount = eventData.moduleDatas.size();

        bool result = true;

        for (unsigned mi = 0; mi < moduleCount; ++mi)
        {
            auto &mds = eventData.moduleDatas[mi];

            result = result && std::all_of(std::begin(mds), std::end(mds),
                                           [](const ModuleStorage &md)
                                           { return size_consistency_check(md); });

            // This will fail for the extreme case where none of the modules in
            // an event yielded a timestamp. fillerTs in recordModuleData() will
            // not be set and the otherwise guaranteed stamp will not be
            // appended to the queue.
            result = result &&
                     std::all_of(std::begin(mds), std::end(mds),
                                 [](const ModuleStorage &md) { return md.timestamp.has_value(); });
        }

        return result;
    }

    bool recordModuleData(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        spdlog::trace("entering recordModuleData: eventIndex={}, moduleCount={}", eventIndex,
                      moduleCount);

        if (!checkConsistency(eventIndex, moduleDataList, moduleCount))
        {
            spdlog::warn("recordModuleData: eventIndex={}, moduleCount={} -> module data "
                         "consistency check failed",
                         eventIndex, moduleCount);
            return false;
        }

        auto &eventData = perEventData_[eventIndex];
        auto &eventCfg = cfg_.eventConfigs[eventIndex];
        auto &eventCtrs = counters_.eventCounters[eventIndex];

        if (!eventCfg.enabled)
        {
            // Passthrough - no need to record. Invoke output callback immediately.
            callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex, moduleDataList,
                                 moduleCount);

            for (unsigned mi = 0; mi < moduleCount; ++mi)
            {
                ++eventCtrs.inputHits[mi];
                ++eventCtrs.outputHits[mi];
            }

            return true;
        }

        // Record incoming module data and extracted timestamps.
        // If it returns false none of the ModuleStorages haven been modified.
        // Otherwise all of them have a new entry even if no timestamp could be extracted.

        bool recordOk = record_module_data(moduleDataList, moduleCount, eventCfg.moduleConfigs,
                               eventData.moduleDatas, eventCtrs);
        if (recordOk)
        {
            // The back of each 'moduleDatas' queue now contains the newest data
            // plus the optional timestamp.

            // Check if all stamps are invalid. This means none of the modules
            // yielded a stamp, either due to empty or corrupted data.
            if (std::all_of(eventData.moduleDatas.begin(), eventData.moduleDatas.end(),
                            [](const std::deque<ModuleStorage> &mds)
                            { return mds.back().timestamp == std::nullopt; }))
            {
                // No stamps at all -> discard all data from this event. We
                // wouldn't know when to yield it.
                for (size_t mi = 0; mi < moduleCount; ++mi)
                {
                    ++eventCtrs.currentEvents[mi];
                    const auto &mdata = eventData.moduleDatas[mi].back();
                    eventCtrs.currentMem[mi] += mdata.data.size() * sizeof(u32);
                    eventData.moduleDatas[mi].pop_back(); // discard
                }
                ++eventCtrs.discardsNoStamp; // count this once for the whole event
            }
        }
        else
        {
            // Some mismatch between config and actual readout data.
            ++eventCtrs.recordingFailed;
        }

        // Careful from here on out: it's not guaranteed that each modules event
        // data queue is non-empty. If all or some of the modules never yielded
        // a valid stamp their queue might still be empty.

        // Fill input side dt histograms
        for (auto &dtHisto: eventCtrs.dtInputHistos)
        {
            auto &mds0 = eventData.moduleDatas[dtHisto.moduleIndexes.first];
            auto &mds1 = eventData.moduleDatas[dtHisto.moduleIndexes.second];

            if (!mds0.empty() && !mds1.empty())
            {
                auto ts0 = eventData.moduleDatas[dtHisto.moduleIndexes.first].back().timestamp;
                auto ts1 = eventData.moduleDatas[dtHisto.moduleIndexes.second].back().timestamp;

                if (ts0.has_value() && ts1.has_value())
                {
                    s64 dt = timestamp_difference(ts0.value(), ts1.value());
                    fill(dtHisto.histo, dt);
                }
            }
        }

        // Apply offsets to the timestamps.
        for (unsigned mi = 0; mi < moduleCount; ++mi)
        {
            if (auto &mds = eventData.moduleDatas[mi]; !mds.empty())
            {
                if (auto &ms = mds.back(); ms.timestamp.has_value())
                {
                    *ms.timestamp =
                        add_offset_to_timestamp(*ms.timestamp, eventCfg.moduleConfigs[mi].offset);
                }
            }
        }

        // Determine the filler stamp for this incoming event.
        // The filler stamp is used for modules that do not yield a valid
        // stamp. Makes flushing easier and keeps non-stamped modules together
        // with their sibling modules on output.
        std::optional<TimestampType> fillerTs;

        for (unsigned mi = 0; mi < moduleCount; ++mi)
        {
            if (eventData.moduleDatas[mi].empty())
                continue;

            if (auto ts = eventData.moduleDatas[mi].back().timestamp; ts.has_value())
            {
                // Record the stamps from all modules.
                eventData.allTimestamps.push_back(*ts);

                // Assign a filler stamp if we don't have one yet.
                if (!fillerTs.has_value())
                {
                    fillerTs = ts;
                    spdlog::trace("recordModuleData: eventIndex={}, moduleIndex={} -> provides "
                                "fillerTs={}",
                                eventIndex, mi, fillerTs.value());
                }
            }
        }

        // Assign the filler stamp to modules that do not have a valid stamp.
        if (fillerTs.has_value())
        {
            for (unsigned mi = 0; mi < moduleCount; ++mi)
            {
                if (eventData.moduleDatas[mi].empty())
                    continue;

                if (!eventData.moduleDatas[mi].back().timestamp.has_value())
                {
                    eventData.moduleDatas[mi].back().timestamp = fillerTs;
                    spdlog::trace("recordModuleData: eventIndex={}, moduleIndex={} -> assign "
                                "fillerTs={}, data.size={}",
                                eventIndex, mi, fillerTs.value(),
                                eventData.moduleDatas[mi].back().data.size());
                }
                else
                {
                    spdlog::trace("recordModuleData: eventIndex={}, moduleIndex={} -> module "
                                "has valid ts, ts={}, data.size={}",
                                eventIndex, mi,
                                eventData.moduleDatas[mi].back().timestamp.value(),
                                eventData.moduleDatas[mi].back().data.size());
                }
            }
        }
        else
        {
            // No filler stamp -> none of the modules in this event yielded a valid stamp.
            // This is going to be fixed
            spdlog::trace("recordModuleData: eventIndex={} -> no fillerTs available",
                        eventIndex);
        }

        spdlog::trace("leaving recordModuleData: eventIndex={}, moduleCount={} -> return true",
                    eventIndex, moduleCount);

        spdlog::trace("leaving recordModuleData: eventIndex={}, moduleCount={} -> return {}",
                     eventIndex, moduleCount, recordOk);

        eventCtrs.allTimestamps = eventData.allTimestamps;
        eventCtrs.allTimestampsMaxSize = std::max(eventCtrs.allTimestampsMaxSize, eventData.allTimestamps.size());

        // Update the publicly accessible counters.
        *protectedCounters_.access() = counters_;
        return recordOk;
    }

    bool tryFlush(int eventIndex)
    {
        spdlog::trace("entering tryFlush: eventIndex={}", eventIndex);

        if (!checkModuleBuffers(eventIndex))
        {
            spdlog::trace("tryFlush: eventIndex={} -> checkModuleBuffers failed -> return false",
                          eventIndex);
            return false;
        }

        auto &eventData = perEventData_[eventIndex];
        auto &eventCfg = cfg_.eventConfigs[eventIndex];
        auto &eventCtrs = counters_.eventCounters[eventIndex];

        if (!eventCfg.enabled)
            return false;

        if (eventData.allTimestamps.empty())
            return false;

        const auto moduleCount = eventCfg.moduleConfigs.size();
        const auto refTs = eventData.allTimestamps.front();
        bool haveData = false;

        // Check if the latest timestamps of all modules are "in the future",
        // e.g. too new to be in the match window of the current reference
        // stamp. This means we waited long enough for stamps to exceed the
        // match window and that we can start flushing events.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &mc = eventCfg.moduleConfigs[mi];

            // no data in the queue for this module
            if (eventData.moduleDatas.at(mi).empty())
                continue;

            auto modTs = eventData.moduleDatas.at(mi).back().timestamp.value();
            auto matchResult = timestamp_match(refTs, modTs, mc.window);
            if (matchResult.match != WindowMatch::too_new)
            {
                spdlog::trace("tryFlush: module{}, refTs={}, modTs={}, window={}, match={} -> "
                              "newest stamp is not far enough in the future, cannot flush yet -> "
                              "return false",
                              mi, refTs, modTs, mc.window, (int)matchResult.match);
                return false;
            }

            haveData = true;
        }

        if (!haveData)
            return false;

        spdlog::trace(
            "tryFlush: refTs={}, all modules have a ts in the future -> flushing at least "
            "one event",
            refTs);

        // pop the refTs, so we won't encounter it again. Loop because multiple modules might yield
        // the exact same refTs.
        while (!eventData.allTimestamps.empty() && eventData.allTimestamps.front() == refTs)
        {
            eventData.allTimestamps.pop_front();
        }

        // pop data that is too old and thus can never be matched
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &moduleDatas = eventData.moduleDatas.at(mi);
            auto &moduleConfig = eventCfg.moduleConfigs.at(mi);

            while (!moduleDatas.empty())
            {
                auto modTs = moduleDatas.front().timestamp.value();
                auto matchResult = timestamp_match(refTs, modTs, moduleConfig.window);

                if (matchResult.match == WindowMatch::too_old)
                {
                    spdlog::trace("  tryFlush: mi={}, refTs={}, modTs={}, window={}, too_old -> "
                                  "discard event",
                                  mi, refTs, modTs, moduleConfig.window);
                    ++eventCtrs.discardsAge[mi];
                    --eventCtrs.currentEvents[mi];
                    eventCtrs.currentMem[mi] -= moduleDatas.front().data.size() * sizeof(u32);
                    moduleDatas.pop_front();
                }
                else
                {
                    break;
                }
            }
        }

        outputModuleStorage_.resize(moduleCount);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &moduleConfig = eventCfg.moduleConfigs.at(mi);
            auto &moduleDatas = eventData.moduleDatas.at(mi);

            outputModuleStorage_[mi] = ModuleStorage();
            // Set attributes from the config and resize data in case we do not
            // actually get real input data for this module. This way the output
            // arrays always have the expected size and structure.
            #if 0
            outputModuleStorage_[mi].prefixSize = moduleConfig.prefixSize;
            outputModuleStorage_[mi].hasDynamic = moduleConfig.hasDynamic;
            outputModuleStorage_[mi].data.resize(moduleConfig.prefixSize);
            std::fill(std::begin(outputModuleStorage_[mi].data),
                      std::end(outputModuleStorage_[mi].data), 0);
            #endif

            while (!moduleDatas.empty())
            {
                auto modTs = moduleDatas.front().timestamp.value();
                auto matchResult = timestamp_match(refTs, modTs, moduleConfig.window);

                assert(matchResult.match != WindowMatch::too_old); // this data should've been popped above
                s64 dt = refTs - modTs;

                if (matchResult.match == WindowMatch::in_window)
                {
                    spdlog::trace("  tryFlush: mi={}, refTs={}, modTs={}, dt={}, window={}, "
                                  "in_window -> add to out event",
                                  mi, refTs, modTs, dt, moduleConfig.window);
                    // Move data to the output buffer. Needed for the linear ModuleData array.
                    outputModuleStorage_[mi] = std::move(moduleDatas.front());
                    moduleDatas.pop_front();
                    ++eventCtrs.outputHits[mi];
                    --eventCtrs.currentEvents[mi];
                    eventCtrs.currentMem[mi] -= outputModuleStorage_[mi].data.size() * sizeof(u32);
                    break;
                }
                else if (matchResult.match == WindowMatch::too_new)
                {
                    spdlog::trace(
                        "  tryFlush: mi={}, refTs={}, modTs={}, dt={}, window={}, too_new "
                        "-> leave in buffer",
                        mi, refTs, modTs, dt, moduleConfig.window);
                    break;
                }
            }
        }

        // Fill output side dt histograms.
        for (auto &dtHisto: eventCtrs.dtOutputHistos)
        {
            auto ts0 = outputModuleStorage_[dtHisto.moduleIndexes.first].timestamp;
            auto ts1 = outputModuleStorage_[dtHisto.moduleIndexes.second].timestamp;

            if (ts0.has_value() && ts1.has_value())
            {
                s64 dt = timestamp_difference(ts0.value(), ts1.value());
                fill(dtHisto.histo, dt);
            }
        }

        // printf debug stuff. TODO: remove this at some point
        std::vector<std::string> debugStamps;
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            if (outputModuleStorage_[mi].timestamp.has_value())
            {
                debugStamps.push_back(
                    fmt::format("{}", outputModuleStorage_[mi].timestamp.value()));
            }
            else
            {
                debugStamps.push_back("n/a");
            }
        }

        spdlog::trace("tryFlush: eventIndex={}, refTs={}, outputStamps={}", eventIndex, refTs,
                      fmt::join(debugStamps, ", "));


        const bool hasOutputData = std::any_of(outputModuleStorage_.begin(), outputModuleStorage_.end(),
                                       [](const ModuleStorage &ms)
                                       { return !ms.data.empty(); });

        if (hasOutputData)
        {
            outputModuleData_.resize(moduleCount);

            for (size_t mi = 0; mi < moduleCount; ++mi)
            {
                if (!size_consistency_check(outputModuleStorage_[mi]))
                {
                    spdlog::error("  tryFlush: mi={}, size_consistency_check failed", mi);
                }
                assert(size_consistency_check(outputModuleStorage_[mi]));
                if (outputModuleStorage_[mi].data.empty())
                {
                    // don't have real data for this module. fixup the structure
                    // based on the input config (only has an effect if
                    // moduleConfig.prefixSize != 0)
                    auto &moduleConfig = eventCfg.moduleConfigs.at(mi);
                    outputModuleStorage_[mi].prefixSize = moduleConfig.prefixSize;
                    outputModuleStorage_[mi].hasDynamic = moduleConfig.hasDynamic;
                    outputModuleStorage_[mi].data.resize(moduleConfig.prefixSize);
                    // fill with zeros
                    std::fill(std::begin(outputModuleStorage_[mi].data),
                            std::end(outputModuleStorage_[mi].data), 0);
                }
                else
                {
                    // we do have real data for this module. the structure
                    // should match what the input config says by definition
                    outputModuleData_[mi] = outputModuleStorage_[mi].to_module_data();
                }
                assert(mvlc::readout_parser::size_consistency_check(outputModuleData_[mi]));
            }

            callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex,
                                outputModuleData_.data(), moduleCount);
        }
        else
        {
            spdlog::trace("tryFlush: eventIndex={} -> no output data in any module after flushing "
                          "-> not calling callback", eventIndex);
        }

        return true;
    }

    size_t forceFlush(int eventIndex)
    {
        spdlog::trace("entering forceFlush: eventIndex={}", eventIndex);
        auto &ed = perEventData_.at(eventIndex);
        bool haveData = false;
        size_t result = 0;
        const auto moduleCount = ed.moduleDatas.size();
        outputModuleData_.resize(moduleCount);
        outputModuleStorage_.resize(moduleCount);
        auto &eventCtrs = counters_.eventCounters[eventIndex];

        do
        {
            haveData = false;
            for (size_t mi = 0; mi < moduleCount; ++mi)
            {
                auto &mds = ed.moduleDatas.at(mi);
                if (mds.empty())
                {
                    outputModuleData_[mi] = {};
                    continue;
                }
                // Move data to the output buffer. Needed for the linear ModuleData array.
                outputModuleStorage_[mi] = std::move(mds.front());
                mds.pop_front();
                outputModuleData_[mi] = outputModuleStorage_[mi].to_module_data();
                ++eventCtrs.outputHits[mi];
                --eventCtrs.currentEvents[mi];
                eventCtrs.currentMem[mi] -= outputModuleStorage_[mi].data.size() * sizeof(u32);
                haveData = true;
            }

            if (haveData)
            {
                callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex,
                                     outputModuleData_.data(), moduleCount);
                ++result;
            }
        }
        while (haveData);

        spdlog::trace("leaving forceFlush: eventIndex={} -> flushed {} events", eventIndex, result);

        return result;
    }
};

template <typename T> void resize_and_clear(size_t size, T &t)
{
    t.resize(size);
    std::fill(std::begin(t), std::end(t), typename T::value_type{});
}

template <typename... Ts> void resize_and_clear(size_t size, Ts &...args)
{
    (resize_and_clear(size, args), ...);
}

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext)
    : d(std::make_unique<Private>())
{
    for (size_t ei = 0; ei < cfg.eventConfigs.size(); ++ei)
    {
        const auto &ecfg = cfg.eventConfigs.at(ei);
        for (size_t mi = 0; mi < ecfg.moduleConfigs.size(); ++mi)
        {
            const auto &mcfg = ecfg.moduleConfigs.at(mi);
            if (!mcfg.hasDynamic && mcfg.prefixSize == 0)
            {
                throw std::runtime_error(
                    fmt::format("EventBuilder2: config error: eventIndex={}, moduleIndex={} -> "
                                "static prefix size must be set if hasDynamic==false",
                                ei, mi));
            }
        }
    }

    d->cfg_ = cfg;
    d->callbacks_ = callbacks;
    d->userContext_ = userContext;
    d->perEventData_.resize(cfg.eventConfigs.size());
    d->counters_.eventCounters.resize(cfg.eventConfigs.size());

    // Initialize counters and create histograms
    for (size_t ei = 0; ei < cfg.eventConfigs.size(); ++ei)
    {
        auto &ec = cfg.eventConfigs.at(ei);
        auto &ed = d->perEventData_.at(ei);
        auto &ctrs = d->counters_.eventCounters.at(ei);
        resize_and_clear(ec.moduleConfigs.size(), ed.moduleDatas, ctrs.inputHits, ctrs.outputHits,
                         ctrs.emptyInputs, ctrs.discardsAge, ctrs.stampFailed, ctrs.currentEvents,
                         ctrs.currentMem, ctrs.maxEvents, ctrs.maxMem, ctrs.moduleNames);

        for (size_t mi = 0; mi < ec.moduleConfigs.size(); ++mi)
        {
            ctrs.moduleNames[mi] = ec.moduleConfigs.at(mi).name;
            ed.moduleDatas.at(mi) = std::deque<ModuleStorage>{};
        }

        ctrs.dtInputHistos = create_dt_histograms(ec.moduleConfigs, cfg.dtHistoBinning);
        ctrs.dtOutputHistos = create_dt_histograms(ec.moduleConfigs, cfg.dtHistoBinning);
        ctrs.eventName = ec.name;
    }

    *d->protectedCounters_.access() = d->counters_; // initial update of the publicly accessible counters
}

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, void *userContext)
    : EventBuilder2(cfg, {}, userContext)
{
}

EventBuilder2::EventBuilder2()
    : EventBuilder2({}, {}, nullptr)
{
}

EventBuilder2::EventBuilder2(EventBuilder2 &&) = default;
EventBuilder2 &EventBuilder2::operator=(EventBuilder2 &&) = default;

EventBuilder2::~EventBuilder2() {}

void EventBuilder2::setCallbacks(const Callbacks &callbacks) { d->callbacks_ = callbacks; }

bool EventBuilder2::recordModuleData(int eventIndex, const ModuleData *moduleDataList,
                                     unsigned moduleCount)
{
    std::unique_lock<mvlc::TicketMutex> guard(d->mutex_);
    if (0 > eventIndex || static_cast<size_t>(eventIndex) >= d->perEventData_.size())
    {
        // TODO: count EventIndexOutOfRange
        return false;
    }

    d->recordModuleData(eventIndex, moduleDataList, moduleCount);
    return true;
}

void EventBuilder2::handleSystemEvent(const u32 *header, u32 size)
{
    std::unique_lock<mvlc::TicketMutex> guard(d->mutex_);
    d->callbacks_.systemEvent(d->userContext_, d->cfg_.outputCrateIndex, header, size);
}

size_t EventBuilder2::flush(bool force)
{
    std::unique_lock<mvlc::TicketMutex> guard(d->mutex_);
    size_t flushed = 0;

    if (force)
    {
        for (size_t eventIndex = 0; eventIndex < d->perEventData_.size(); ++eventIndex)
        {
            flushed += d->forceFlush(eventIndex);
        }
    }
    else
    {

        for (size_t eventIndex = 0; eventIndex < d->perEventData_.size(); ++eventIndex)
        {
            // fmt::print("pre tryFlush: {}\n", debugDump());
            while (d->tryFlush(eventIndex))
            {
                ++flushed;
            }
            // if (flushed > 0)
            //     fmt::print("post tryFlush: {}\n", debugDump());
        }
    }

    // counters lock is held by the public recordModuleData()
    d->protectedCounters_.access().ref() = d->counters_;

    return flushed;
}

std::string EventBuilder2::debugDump(const std::string &info) const
{
    std::unique_lock<mvlc::TicketMutex> guard(d->mutex_);
    std::string result;

    if (!info.empty())
    {
        result += fmt::format("EB2 debug dump: {}\n", info);
    }

    for (size_t ei = 0; ei < d->perEventData_.size(); ++ei)
    {
        result += fmt::format("Event {}:\n", ei);
        auto &ed = d->perEventData_.at(ei);

        auto stampsToPrint = std::min(static_cast<size_t>(10), ed.allTimestamps.size());
        auto stampsBegin = std::begin(ed.allTimestamps);
        auto stampsEnd = stampsBegin;
        std::advance(stampsEnd, stampsToPrint);
        auto stampsBegin2 = std::end(ed.allTimestamps);
        std::advance(stampsBegin2, -stampsToPrint);


        result += fmt::format("  First {} timestamps of {}: {}\n", stampsToPrint,
                              ed.allTimestamps.size(), fmt::join(stampsBegin, stampsEnd, ", "));

        result += fmt::format("  Last {} timestamps of {}: {}\n", stampsToPrint,
                              ed.allTimestamps.size(), fmt::join(stampsBegin2, std::end(ed.allTimestamps), ", "));



        for (size_t mi = 0; mi < ed.moduleDatas.size(); ++mi)
        {
            auto window = d->cfg_.eventConfigs.at(ei).moduleConfigs.at(mi).window;
            auto &moduleDatas = ed.moduleDatas.at(mi);
            auto stampsToPrint = std::min(static_cast<size_t>(10), moduleDatas.size());
            auto stampsBegin = std::begin(moduleDatas);
            auto stampsEnd = stampsBegin;
            std::advance(stampsEnd, stampsToPrint);
            std::vector<std::string> stamps;
            std::transform(stampsBegin, stampsEnd, std::back_inserter(stamps),
                           [](const ModuleStorage &md) {
                               return md.timestamp.has_value()
                                          ? std::to_string(md.timestamp.value())
                                          : "no ts";
                           });

            result += fmt::format(
                "  Module {}, bufferedEvents={}, window={}, first {} timestamps of {}: {}\n", mi,
                moduleDatas.size(), window, stampsToPrint, stamps.size(), fmt::join(stamps, ", "));
        }
    }
    return result;
}

bool EventBuilder2::isEnabledForAnyEvent() const
{
    for (const auto &ec: d->cfg_.eventConfigs)
    {
        if (ec.enabled)
            return true;
    }
    return false;
}

BuilderCounters EventBuilder2::getCounters() const
{
    return d->protectedCounters_.copy();
}

std::vector<std::vector<ModuleDeltaHisto>> BuilderCounters::getInputDtHistograms() const
{
    std::vector<std::vector<ModuleDeltaHisto>> result;

    for (const auto &eventCtrs: eventCounters)
    {
        result.emplace_back(eventCtrs.dtInputHistos);
    }

    return result;
}

std::vector<ModuleDeltaHisto> BuilderCounters::getInputDtHistograms(int eventIndex) const
{
    std::vector<ModuleDeltaHisto> result;

    if (0 <= eventIndex && static_cast<size_t>(eventIndex) < eventCounters.size())
    {
        result = eventCounters.at(eventIndex).dtInputHistos;
    }

    return result;
}

std::vector<std::vector<ModuleDeltaHisto>> BuilderCounters::getOutputDtHistograms() const
{
    std::vector<std::vector<ModuleDeltaHisto>> result;

    for (const auto &eventCtrs: eventCounters)
    {
        result.emplace_back(eventCtrs.dtOutputHistos);
    }

    return result;
}

std::vector<ModuleDeltaHisto> BuilderCounters::getOutputDtHistograms(int eventIndex) const
{
    std::vector<ModuleDeltaHisto> result;

    if (0 <= eventIndex && static_cast<size_t>(eventIndex) < eventCounters.size())
    {
        result = eventCounters.at(eventIndex).dtOutputHistos;
    }

    return result;
}

} // namespace mesytec::mvlc::event_builder2
