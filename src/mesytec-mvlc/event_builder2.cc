#include "event_builder2.hpp"

#include <deque>
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

WindowMatchResult timestamp_match(s64 tsMain, s64 tsModule, u32 windowWidth)
{
    s64 diff = tsMain - tsModule;

    if (std::abs(diff) > TimestampHalf)
    {
        // overflow handling
        if (diff < 0)
            diff += TimestampMax;
        else
            diff -= TimestampMax;
    }

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

    ModuleStorage(const ModuleData &md = {}, std::optional<TimestampType> ts = {})
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

        if (ts.has_value())
        {
            *ts = add_offset_to_timestamp(*ts, mcfg.offset);
        }
        else if (!mcfg.ignored && mdata.data.size > 0)
        {
            ++counters.stampFailed[mi];
            //spdlog::warn("record_module_data: failed timestamp extraction, module{}, data.size={}, data={:#010x}",
            //    mi, mdata.data.size, fmt::join(mdata.data.data, mdata.data.data + mdata.data.size, ", "));
        }

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

    oss << fmt::format("inputHits:          {}\n", fmt::join(counters.inputHits, ", "));
    oss << fmt::format("discardsAge:        {}\n", fmt::join(counters.discardsAge, ", "));
    oss << fmt::format("outputHits:         {}\n", fmt::join(counters.outputHits, ", "));
    oss << fmt::format("sumOutputsDiscards: {}\n", fmt::join(sumOutputsDiscards, ", "));
    oss << fmt::format("emptyInputs:        {}\n", fmt::join(counters.emptyInputs, ", "));
    oss << fmt::format("stampFailed:        {}\n", fmt::join(counters.stampFailed, ", "));

    oss << fmt::format("currentEvents:      {}\n", fmt::join(counters.currentEvents, ", "));
    oss << fmt::format("maxEvents:          {}\n", fmt::join(counters.maxEvents, ", "));

    oss << fmt::format("currentMem:         {}\n", fmt::join(counters.currentMem, ", "));
    oss << fmt::format("maxMem:             {}\n", fmt::join(counters.maxMem, ", "));

    return oss.str();
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

    BuilderCounters counters_;
    mvlc::TicketMutex countersMutex_;

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
        if (!checkConsistency(eventIndex, moduleDataList, moduleCount))
            return false;

        auto &eventData = perEventData_[eventIndex];
        auto &eventCfg = cfg_.eventConfigs[eventIndex];
        auto &eventCtrs = counters_.eventCounters[eventIndex];

        if (!eventCfg.enabled)
        {
            callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex, moduleDataList,
                                 moduleCount);

            for (unsigned mi = 0; mi < moduleCount; ++mi)
            {
                ++eventCtrs.inputHits[mi];
                ++eventCtrs.outputHits[mi];
            }

            return true;
        }

        // If it returns false none of the ModuleStorages haven been modified.
        // Otherwise all of them have a new entry.
        if (record_module_data(moduleDataList, moduleCount, eventCfg.moduleConfigs,
                               eventData.moduleDatas, eventCtrs))
        {
            // The filler stamp is used for modules that do not yield a valid
            // stamp. Makes flushing easier and keeps non-stamped modules together
            // with their sister modules on output.
            std::optional<TimestampType> fillerTs;

            for (unsigned mi = 0; mi < moduleCount; ++mi)
            {
                if (auto ts = eventData.moduleDatas[mi].back().timestamp; ts.has_value())
                {
                    eventData.allTimestamps.push_back(*ts);

                    if (!fillerTs.has_value())
                        fillerTs = ts;
                }
            }

            if (fillerTs.has_value())
            {
                for (unsigned mi = 0; mi < moduleCount; ++mi)
                {
                    if (!eventData.moduleDatas[mi].back().timestamp.has_value())
                        eventData.moduleDatas[mi].back().timestamp = fillerTs;
                }
            }

            return true;
        }

        ++eventCtrs.recordingFailed;
        return false;
    }

    bool tryFlush(int eventIndex)
    {
        if (!checkModuleBuffers(eventIndex))
            return false;

        auto &eventData = perEventData_[eventIndex];
        auto &eventCfg = cfg_.eventConfigs[eventIndex];
        auto &eventCtrs = counters_.eventCounters[eventIndex];

        if (!eventCfg.enabled)
            return false;

        const auto moduleCount = eventCfg.moduleConfigs.size();
        const auto refTs = eventData.allTimestamps.front();

        // check if the latest timestamps of all modules are "in the future",
        // e.g. too new to be in the match window of the current reference
        // stamp.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &mc = eventCfg.moduleConfigs[mi];

            // ignored or no data in the queue
            if (mc.ignored || eventData.moduleDatas.at(mi).empty())
                continue;

            auto modTs = eventData.moduleDatas.at(mi).back().timestamp.value();
            auto matchResult = timestamp_match(refTs, modTs, mc.window);
            if (matchResult.match != WindowMatch::too_new)
            {
                // spdlog::warn("tryFlush: module{}, refTs={}, modTs={}, window={}, match={} -> "
                //              "newest stamp is not far enough in the future, cannot flush yet -> "
                //              "return false",
                //              mi, refTs, modTs, mc.window, (int)matchResult.match);
                return false;
            }
        }

        // spdlog::warn("tryFlush: refTs={}, all modules have a ts in the future -> flushing at
        // least "
        //              "one event", refTs);

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

            outputModuleStorage_[mi] = {};
            // copy hasDynamic from the config so that the output format is
            // guaranteed to be consistent
            outputModuleStorage_[mi].hasDynamic = moduleConfig.hasDynamic;

            while (!moduleDatas.empty())
            {
                auto modTs = moduleDatas.front().timestamp.value();
                auto matchResult = timestamp_match(refTs, modTs, moduleConfig.window);

                assert(matchResult.match != WindowMatch::too_old);

                if (matchResult.match == WindowMatch::in_window)
                {
                    // spdlog::warn("  tryFlush: mi={}, refTs={}, modTs={}, dt={}, window={}, "
                    //              "in_window -> add to out event",
                    //              mi, refTs, modTs, dt, moduleConfig.window);
                    //  Move data to the output buffer. Needed for the linear ModuleData array.
                    outputModuleStorage_[mi] = std::move(moduleDatas.front());
                    moduleDatas.pop_front();
                    ++eventCtrs.outputHits[mi];
                    --eventCtrs.currentEvents[mi];
                    eventCtrs.currentMem[mi] -= outputModuleStorage_[mi].data.size() * sizeof(u32);
                    break;
                }
                else if (matchResult.match == WindowMatch::too_new)
                {
                    // spdlog::warn("  tryFlush: mi={}, refTs={}, modTs={}, dt={}, window={},
                    // too_new "
                    //              "-> leave in buffer",
                    //              mi, refTs, modTs, dt, moduleConfig.window);
                    //  it's too new so we leave it in the buffer
                    break;
                }
            }
        }

        outputModuleData_.resize(moduleCount);
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            outputModuleData_[mi] = outputModuleStorage_[mi].to_module_data();
            assert(mvlc::readout_parser::size_consistency_check(outputModuleData_[mi]));
        }

        callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex,
                             outputModuleData_.data(), moduleCount);

        return true;
    }

    size_t forceFlush(int eventIndex)
    {
        auto &ed = perEventData_.at(eventIndex);
        bool haveData = false;
        size_t result = 0;
        const auto moduleCount = ed.moduleDatas.size();
        outputModuleData_.resize(moduleCount);
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

        return result;
    }
};

template <typename T> void resize_and_clear(size_t size, T &t)
{
    t.resize(size);
    std::fill(std::begin(t), std::end(t), typename T::value_type{});
}

template <typename... Ts> void resize_and_clear(size_t size, Ts &&...args)
{
    (resize_and_clear(size, args), ...);
}

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext)
    : d(std::make_unique<Private>())
{
    d->cfg_ = cfg;
    d->callbacks_ = callbacks;
    d->userContext_ = userContext;
    d->perEventData_.resize(cfg.eventConfigs.size());
    d->counters_.eventCounters.resize(cfg.eventConfigs.size());

    for (size_t ei = 0; ei < cfg.eventConfigs.size(); ++ei)
    {
        auto &ec = cfg.eventConfigs.at(ei);
        auto &ed = d->perEventData_.at(ei);
        auto &ctrs = d->counters_.eventCounters.at(ei);
        resize_and_clear(ec.moduleConfigs.size(), ed.moduleDatas, ctrs.inputHits, ctrs.outputHits,
                         ctrs.emptyInputs, ctrs.discardsAge, ctrs.stampFailed, ctrs.currentEvents,
                         ctrs.currentMem, ctrs.maxEvents, ctrs.maxMem);
    }
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
    std::unique_lock<mvlc::TicketMutex> guard(d->countersMutex_);
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
    std::unique_lock<mvlc::TicketMutex> guard(d->countersMutex_);
    d->callbacks_.systemEvent(d->userContext_, d->cfg_.outputCrateIndex, header, size);
}

size_t EventBuilder2::flush(bool force)
{
    std::unique_lock<mvlc::TicketMutex> guard(d->countersMutex_);
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

    return flushed;
}

std::string EventBuilder2::debugDump() const
{
    std::unique_lock<mvlc::TicketMutex> guard(d->countersMutex_);
    std::string result;

    for (size_t ei = 0; ei < d->perEventData_.size(); ++ei)
    {
        result += fmt::format("Event {}:\n", ei);
        auto &ed = d->perEventData_.at(ei);

        auto stampsToPrint = std::min(static_cast<size_t>(10), ed.allTimestamps.size());
        auto stampsBegin = std::begin(ed.allTimestamps);
        auto stampsEnd = stampsBegin;
        std::advance(stampsEnd, stampsToPrint);

        result += fmt::format("  First {} timestamps of {}: {}\n", stampsToPrint,
                              ed.allTimestamps.size(), fmt::join(stampsBegin, stampsEnd, ", "));

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
    std::unique_lock<mvlc::TicketMutex> guard(d->countersMutex_);
    return d->counters_;
}

} // namespace mesytec::mvlc::event_builder2
