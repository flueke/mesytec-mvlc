#include "event_builder2.hpp"
#include <deque>

namespace mesytec::mvlc::event_builder
{

IndexedTimestampFilterExtractor::IndexedTimestampFilterExtractor(const util::DataFilter &filter, s32 wordIndex, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
    , index_(wordIndex)
{
}

u32 IndexedTimestampFilterExtractor::operator()(const u32 *data, size_t size)
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

    return event_builder::TimestampExtractionFailed;
}

TimestampFilterExtractor::TimestampFilterExtractor(const util::DataFilter &filter, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
{
}

u32 TimestampFilterExtractor::operator()(const u32 *data, size_t size)
{
    for (const u32 *valuep = data; valuep < data + size; ++valuep)
    {
        if (matches(filter_, *valuep))
            return extract(filterCache_, *valuep);
    }

    return event_builder::TimestampExtractionFailed;
}

WindowMatch timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow)
{
    using namespace event_builder;

    s64 diff = static_cast<s64>(tsMain) - static_cast<s64>(tsModule);

    if (std::abs(diff) > TimestampHalf)
    {
        // overflow handling
        if (diff < 0)
            diff += TimestampMax;
        else
            diff -= TimestampMax;
    }

    if (diff >= 0)
    {
        // tsModule is before tsMain
        if (diff > -matchWindow.first)
            return WindowMatch::too_old;
    }
    else
    {
        // tsModule is after tsMain
        if (-diff > matchWindow.second)
            return WindowMatch::too_new;
    }

    return WindowMatch::in_window;
}

struct PerEventData
{
    using TsType = s64;
    // Timestamps of all modules of the incoming event are stored here.
    std::deque<TsType> allTimestamps;
    // Per module timestamp buffers.
    std::vector<std::deque<TsType>> moduleTimestamps;
    // Per module (offset, size) pairs pointing into the data_buffer.
    std::vector<std::deque<std::vector<u32>>> moduleDatas;
};

struct EventBuilder2::Private
{
    EventBuilderConfig cfg_;
    Callbacks callbacks_;
    void *userContext_;
    std::vector<PerEventData> perEventData_;
    // Used for the callback interface which requires a flat ModuleData array.
    std::vector<ModuleData> outputModuleData_;
    std::vector<std::vector<u32>> outputModuleStorage_;

    void recordModuleData(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        auto &ed = perEventData_.at(eventIndex);
        auto &ec = cfg_.eventConfigs.at(eventIndex);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto md = moduleDataList[mi].hasDynamic ? dynamic_span(moduleDataList[mi])
                                                    : prefix_span(moduleDataList[mi]);
            auto &mc = ec.moduleConfigs.at(mi);
            s64 ts = mc.tsExtractor(md.data, md.size);
            // ts += ec.moduleConfigs.at(mi).offset; // FIXME: this needs 30-bit overflow handling.
            if (!mc.ignored)
                ed.allTimestamps.emplace_back(ts);
            ed.moduleTimestamps.at(mi).emplace_back(ts);
            ed.moduleDatas.at(mi).emplace_back(md.data, md.data + md.size);
        }
    }

    size_t tryFlush(int eventIndex, bool force = false)
    {
        auto &ed = perEventData_.at(eventIndex);
        auto &ec = cfg_.eventConfigs.at(eventIndex);
        const auto moduleCount = ec.moduleConfigs.size();
        const auto refTs = ed.allTimestamps.front();

        // check if the latest timestamps of all modules are "in the future",
        // e.g. too new to be in the match window of the current reference
        // stamp.
        for (size_t mi=0; mi<moduleCount; ++mi)
        {
            auto modTs = ed.moduleTimestamps.at(mi).back();
            auto &mc = ec.moduleConfigs.at(mi);
            if (auto matchResult = timestamp_match(refTs, modTs, mc.matchWindow);
                matchResult != WindowMatch::too_new)
            {
                return 0;
            }
        }

        ed.allTimestamps.pop_front(); // pop the refTs, so we won't encounter it again
        outputModuleStorage_.resize(moduleCount);

        for (size_t mi=0; mi<moduleCount; ++mi)
        {
            auto &mc = ec.moduleConfigs.at(mi);
            auto &mds = ed.moduleDatas.at(mi);
            auto &mts = ed.moduleTimestamps.at(mi);

            outputModuleStorage_[mi].clear();

            while (!mts.empty())
            {
                auto modTs = mts.front();
                auto matchResult = timestamp_match(refTs, modTs, mc.matchWindow);

                if (matchResult == WindowMatch::too_old)
                {
                    // discard old data
                    mds.pop_front();
                    mts.pop_front();
                }
                else if (matchResult == WindowMatch::in_window)
                {
                    // Copy data to the output buffer. Needed to get a linear ModuleData array later on.
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
        for (size_t mi=0; mi<moduleCount; ++mi)
        {
            outputModuleData_[mi] = ModuleData{ .data = { outputModuleStorage_[mi].data(), static_cast<u32>(outputModuleStorage_[mi].size()) } };
        }

        callbacks_.eventData(userContext_, cfg_.outputCrateIndex, eventIndex, outputModuleData_.data(), moduleCount);
    }
};

EventBuilder2::EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext)
    : d(std::make_unique<Private>())
{
    d->cfg_ = cfg;
    d->callbacks_ = callbacks;
    d->userContext_ = userContext;
    d->perEventData_.resize(cfg.eventConfigs.size());
}

EventBuilder2::~EventBuilder2() = default;

void EventBuilder2::handleModuleData(int eventIndex, const ModuleData *moduleDataList,
                                     unsigned moduleCount)
{
    if (0 > eventIndex || static_cast<size_t>(eventIndex) >= d->perEventData_.size())
    {
        // TODO: count EventIndexOutOfRange
        return;
    }

    d->recordModuleData(eventIndex, moduleDataList, moduleCount);
    while (d->tryFlush(eventIndex) > 0) { /* noop */}
}

void EventBuilder2::handleSystemEvent(const u32 *header, u32 size) {}

} // namespace mesytec::mvlc::event_builder
