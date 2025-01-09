#include "event_builder2.hpp"
#include <deque>
#include <spdlog/spdlog.h>

namespace mesytec::mvlc::event_builder
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
    if (index_ < 0)
    {
        ssize_t abs = size + index_;

        if (0 <= abs && static_cast<size_t>(abs) < size && matches(filter_, data[abs]))
            return extract(filterCache_, data[abs]);
    }
    else if (static_cast<size_t>(index_) < size && matches(filter_, data[index_]))
    {
        return extract(filterCache_, data[index_]);
    }

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
            return { WindowMatch::too_old, static_cast<u32>(std::abs(diff)) };
        else
            return { WindowMatch::too_new, static_cast<u32>(std::abs(diff)) };
    }

    return {WindowMatch::in_window, static_cast<u32>(std::abs(diff))};
}

struct ModuleStorage
{
    std::vector<u32> data;
    u32 prefixSize;
    u32 dynamicSize;
    u32 suffixSize;
    bool hasDynamic;

    ModuleStorage(const ModuleData &md = {})
        : data(md.data.data, md.data.data + md.data.size)
        , prefixSize(md.prefixSize)
        , dynamicSize(md.dynamicSize)
        , suffixSize(md.suffixSize)
        , hasDynamic(md.hasDynamic)
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

struct PerEventData
{
    using TsType = s64;

    // Timestamps of all modules of the incoming event are stored here.
    std::deque<TsType> allTimestamps;
    // Per module timestamp buffers.
    std::vector<std::deque<TsType>> moduleTimestamps;
    // Per module data is stored here.
    std::vector<std::deque<ModuleStorage>> moduleDatas;
};

struct EventBuilder2::Private
{
    EventBuilderConfig cfg_;
    Callbacks callbacks_;
    void *userContext_;
    std::vector<PerEventData> perEventData_;
    // Used for the callback interface which requires a flat ModuleData array.
    std::vector<ModuleData> outputModuleData_;
    std::vector<ModuleStorage> outputModuleStorage_;

    void recordModuleData(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        auto &ed = perEventData_.at(eventIndex);
        auto &ec = cfg_.eventConfigs.at(eventIndex);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto md = moduleDataList[mi];
            auto &mc = ec.moduleConfigs.at(mi);
            std::optional<s64> ts = mc.tsExtractor(md.data.data, md.data.size);
            if (ts)
            {
                *ts += ec.moduleConfigs.at(mi).offset; // FIXME: this needs 30-bit overflow handling.
                if (!mc.ignored)
                    ed.allTimestamps.emplace_back(*ts);
                ed.moduleTimestamps.at(mi).emplace_back(*ts);
                ed.moduleDatas.at(mi).emplace_back(ModuleStorage(md));
            }
            else if (md.data.size > 0)
            {
                spdlog::warn("recordModuleData: failed to extract timestamp from event={}, module={}, data={}", eventIndex, mi, fmt::join(md.data.data, md.data.data + md.data.size, ", "));
            }
        }
    }

    bool tryFlush(int eventIndex)
    {
        auto &ed = perEventData_.at(eventIndex);
        auto &ec = cfg_.eventConfigs.at(eventIndex);
        const auto moduleCount = ec.moduleConfigs.size();
        const auto refTs = ed.allTimestamps.front();

        // check if the latest timestamps of all modules are "in the future",
        // e.g. too new to be in the match window of the current reference
        // stamp.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto modTs = ed.moduleTimestamps.at(mi).back();
            auto &mc = ec.moduleConfigs.at(mi);
            auto matchResult = timestamp_match(refTs, modTs, mc.window);
            if (matchResult.match != WindowMatch::too_new)
            {
                spdlog::warn("tryFlush: refTs={}, modTs={}, window={}, match={} -> return false",
                              refTs, modTs, mc.window, (int)matchResult.match);
                return false;
            }
        }

        spdlog::warn("tryFlush: refTs={}, all modules have a ts in the future -> flushing", refTs);

        // pop the refTs, so we won't encounter it again. Loop because multiple modules might yield the exact same refTs.
        while (!ed.allTimestamps.empty() && ed.allTimestamps.front() == refTs)
        {
            ed.allTimestamps.pop_front();
        }

        outputModuleStorage_.resize(moduleCount);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &mc = ec.moduleConfigs.at(mi);
            auto &mds = ed.moduleDatas.at(mi);
            auto &mts = ed.moduleTimestamps.at(mi);

            outputModuleStorage_[mi].data.clear();

            while (!mts.empty())
            {
                auto modTs = mts.front();
                auto matchResult = timestamp_match(refTs, modTs, mc.window);

                if (matchResult.match == WindowMatch::too_old)
                {
                    // discard old data
                    mds.pop_front();
                    mts.pop_front();
                }
                else if (matchResult.match == WindowMatch::in_window)
                {
                    // Move data to the output buffer. Needed for the linear ModuleData array.
                    std::swap(outputModuleStorage_[mi], mds.front());
                    mds.pop_front();
                    mts.pop_front();
                    break;
                }
                else
                {
                    // it's too new so we leave it in the buffer
                    break;
                }
            }
        }

        outputModuleData_.resize(moduleCount);
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            outputModuleData_[mi] = outputModuleStorage_[mi].to_module_data();
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

        do
        {
            haveData = false;
            for (size_t mi=0; mi<moduleCount; ++mi)
            {
                auto &mds = ed.moduleDatas.at(mi);
                if (mds.empty())
                {
                    outputModuleData_[mi] = {};
                    continue;
                }
                // Move data to the output buffer. Needed for the linear ModuleData array.
                std::swap(outputModuleStorage_[mi], mds.front());
                mds.pop_front();
                outputModuleData_[mi] = outputModuleStorage_[mi].to_module_data();
                haveData = true;
            }
            if (haveData)
            {
                callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex,
                                    outputModuleData_.data(), moduleCount);
                ++result;
            }
        } while (haveData);

        return result;
    }
};

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext)
    : d(std::make_unique<Private>())
{
    d->cfg_ = cfg;
    d->callbacks_ = callbacks;
    d->userContext_ = userContext;
    d->perEventData_.resize(cfg.eventConfigs.size());

    for (size_t ei = 0; ei < cfg.eventConfigs.size(); ++ei)
    {
        auto &ec = cfg.eventConfigs.at(ei);
        auto &ed = d->perEventData_.at(ei);
        ed.moduleTimestamps.resize(ec.moduleConfigs.size());
        ed.moduleDatas.resize(ec.moduleConfigs.size());
    }
}

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, void *userContext)
    : EventBuilder2(cfg, {}, userContext)
{
}

EventBuilder2::~EventBuilder2() = default;

void EventBuilder2::setCallbacks(const Callbacks &callbacks) { d->callbacks_ = callbacks; }

bool EventBuilder2::recordModuleData(int eventIndex, const ModuleData *moduleDataList,
                                     unsigned moduleCount)
{
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
    d->callbacks_.systemEvent(d->userContext_, d->cfg_.outputCrateIndex, header, size);
}

size_t EventBuilder2::flush(bool force)
{
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
            fmt::print("pre tryFlush: {}\n", debugDump());
            while (d->tryFlush(eventIndex))
            {
                ++flushed;
            }
            fmt::print("post tryFlush: {}\n", debugDump());
        }
    }

    return flushed;
}

std::string EventBuilder2::debugDump() const
{
    std::string result;
    for (size_t ei = 0; ei < d->perEventData_.size(); ++ei)
    {
        result += fmt::format("Event {}:\n", ei);
        auto &ed = d->perEventData_.at(ei);

        result += fmt::format("  All timestamps: {}\n", fmt::join(ed.allTimestamps, ", "));

        for (size_t mi=0; mi<ed.moduleDatas.size(); ++mi)
        {
            auto window = d->cfg_.eventConfigs.at(ei).moduleConfigs.at(mi).window;
            result += fmt::format("  Module {}, window={}, timestamps: {}\n", mi, window, fmt::join(ed.moduleTimestamps.at(mi), ", "));
        }
    }
    return result;
}

} // namespace mesytec::mvlc::event_builder
