#include "event_builder.h"

#include <deque>
#include <numeric>
#include "mvlc_threading.h"
#include "util/logging.h"

namespace mesytec
{
namespace mvlc
{

IndexedTimestampFilterExtractor::IndexedTimestampFilterExtractor(const DataFilter &filter, s32 wordIndex, char matchChar)
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

TimestampFilterExtractor::TimestampFilterExtractor(const DataFilter &filter, char matchChar)
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

struct SystemEventStorage
{
    int crateIndex;
    std::vector<u32> data;
};

// maybe FIXME: Buffering module data using a std::vector here is not ideal as
// we do need to alloc for each non-empty event.
struct ModuleEventStorage
{
    u32 timestamp;
    std::vector<u32> data;

    size_t usedMemory() const
    {
        return data.size() * sizeof(*data.data());
    }
};

struct PassthroughEventStorage
{
    int crateIndex;
    int eventIndex;
    std::vector<std::vector<u32>> moduleData;
};

using mesytec::mvlc::TicketMutex;
using mesytec::mvlc::UniqueLock;
using readout_parser::PairHash;

ModuleData module_data_from_event_storage(const ModuleEventStorage &input)
{
    auto result = ModuleData
    {
        { input.data.data(), static_cast<u32>(input.data.size()) },
        .prefixSize = 0,
        .dynamicSize = static_cast<u32>(input.data.size()),
        .suffixSize = 0,
        .hasDynamic = true,
    };

    return result;
}

WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow)
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
            return { WindowMatch::too_old, static_cast<u32>(std::abs(diff)) };
    }
    else
    {
        // tsModule is after tsMain
        if (-diff > matchWindow.second)
            return { WindowMatch::too_new, static_cast<u32>(std::abs(diff)) };
    }

    return { WindowMatch::in_window, static_cast<u32>(std::abs(diff)) };
}

struct EventBuilder::Private
{
    void *userContext_ = nullptr;

    std::vector<EventSetup> setups_;
    size_t memoryLimit_ = event_builder::DefaultMemoryLimit;

    TicketMutex mutex_;
    std::condition_variable_any cv_;

    // indexes: event, pair(crateIndex, moduleIndex) -> linear module index
    std::vector<std::unordered_map<std::pair<int, unsigned>, size_t, PairHash>> linearModuleIndexTable_;
    // Reverse mapping back from calculated linear modules indexes to (crateIndex, moduleIndex)
    // indexes: event, linear module index -> pair(crateIndex, moduleIndex)
    //std::vector<std::unordered_map<size_t, std::pair<int, unsigned>>> reverseModuleIndexTable_;
    // Linear module index of the main module for each event
    // indexes: event, linear module
    std::vector<size_t> mainModuleLinearIndexes_;

    // copies of systemEvents
    std::deque<SystemEventStorage> systemEvents_;
    // copies of passthrough events (events for which event building is not enabled)
    std::deque<PassthroughEventStorage> passThroughEvents_;
    // Holds copies of module event data and the extracted event timestamp.
    // indexes: event, linear module, buffered event
    std::vector<std::vector<std::deque<ModuleEventStorage>>> moduleEventBuffers_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleMemCounters_;
    // Keeps track of the maximum total memory used for module event buffering
    // since the last call to reset()
    size_t maxUsedMemory_ = 0u;;
    // indexes: event, linear module
    std::vector<std::vector<timestamp_extractor>> moduleTimestampExtractors_;
    // indexes: event, linear module
    std::vector<std::vector<std::pair<s32, s32>>> moduleMatchWindows_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleDiscardedEvents_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleEmptyEvents_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleInvScoreSums_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleTotalHits_;

    std::vector<ModuleData> eventAssembly_;

    size_t getLinearModuleIndex(int crateIndex, int eventIndex, unsigned moduleIndex) const
    {
        assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < linearModuleIndexTable_.size());
        const auto &eventTable = linearModuleIndexTable_.at(eventIndex);
        const auto key = std::make_pair(crateIndex, moduleIndex);
        assert(eventTable.find(key) != eventTable.end());
        return eventTable.at(key);
    }


    EventBuilder::EventCounters getCounters(int eventIndex) const
    {
        EventCounters ret = {};
        ret.discardedEvents = moduleDiscardedEvents_.at(eventIndex);
        ret.emptyEvents = moduleEmptyEvents_.at(eventIndex);
        ret.invScoreSums = moduleInvScoreSums_.at(eventIndex);
        ret.totalHits = moduleTotalHits_.at(eventIndex);
        return ret;
    }

    size_t getMemoryUsage() const
    {
        return std::accumulate(
            std::begin(moduleMemCounters_), std::end(moduleMemCounters_), static_cast<size_t>(0u),
            [] (const size_t &a, const std::vector<size_t> &b)
            {
                return a + std::accumulate(std::begin(b), std::end(b), static_cast<size_t>(0u));
            });
    }

    size_t buildEvents(int eventIndex, Callbacks &callbacks, bool flush);

    void discardAllEventData()
    {
        for (size_t eventIndex = 0; eventIndex < moduleEventBuffers_.size(); ++eventIndex)
        {
            auto &eventBuffers = moduleEventBuffers_[eventIndex];
            auto &discards = moduleDiscardedEvents_.at(eventIndex);

            for (size_t moduleIndex = 0; moduleIndex < eventBuffers.size(); ++moduleIndex)
            {
                auto &eventBuffer = eventBuffers[moduleIndex];
                discards[moduleIndex] += eventBuffer.size();
                eventBuffer.clear();
            }
        }

        for (auto &memCounters: moduleMemCounters_)
            std::fill(std::begin(memCounters), std::end(memCounters), static_cast<size_t>(0u));

        assert(getMemoryUsage() == 0u);
    }

    void resetCounters()
    {
        maxUsedMemory_ = 0u;

        auto fill0 = [] (auto &c)
        {
            for (auto &counters: c)
                std::fill(std::begin(counters), std::end(counters), static_cast<size_t>(0u));
        };

        fill0(moduleMemCounters_);
        fill0(moduleDiscardedEvents_);
        fill0(moduleEmptyEvents_);
        fill0(moduleInvScoreSums_);
        fill0(moduleTotalHits_);
    }
};

EventBuilder::EventBuilder(const EventBuilderConfig &cfg, void *userContext)
    : d(std::make_unique<Private>())
{
    d->userContext_ = userContext;
    d->setups_ = cfg.setups;
    d->memoryLimit_ = cfg.memoryLimit;

    const size_t eventCount = d->setups_.size();

    d->linearModuleIndexTable_.resize(eventCount);
    d->mainModuleLinearIndexes_.resize(eventCount);
    d->moduleEventBuffers_.resize(eventCount);
    d->moduleMemCounters_.resize(eventCount);
    d->moduleTimestampExtractors_.resize(eventCount);
    d->moduleMatchWindows_.resize(eventCount);
    d->moduleDiscardedEvents_.resize(eventCount);
    d->moduleEmptyEvents_.resize(eventCount);
    d->moduleInvScoreSums_.resize(eventCount);
    d->moduleTotalHits_.resize(eventCount);

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        const auto &eventSetup = d->setups_.at(eventIndex);

        if (!eventSetup.enabled)
            continue;

        auto &eventTable = d->linearModuleIndexTable_.at(eventIndex);
        auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);
        auto &matchWindows = d->moduleMatchWindows_.at(eventIndex);
        auto &eventBuffers = d->moduleEventBuffers_.at(eventIndex);
        auto &memCounters = d->moduleMemCounters_.at(eventIndex);
        auto &discardedEvents = d->moduleDiscardedEvents_.at(eventIndex);
        auto &emptyEvents = d->moduleEmptyEvents_.at(eventIndex);
        auto &invScores = d->moduleInvScoreSums_.at(eventIndex);
        auto &hits = d->moduleTotalHits_.at(eventIndex);
        unsigned linearModuleIndex = 0;

        for (size_t crateIndex = 0; crateIndex < eventSetup.crateSetups.size(); ++crateIndex)
        {
            const auto &crateSetup = eventSetup.crateSetups.at(crateIndex);

            assert(crateSetup.moduleTimestampExtractors.size() == crateSetup.moduleMatchWindows.size());

            const size_t moduleCount = crateSetup.moduleTimestampExtractors.size();

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto key = std::make_pair(crateIndex, moduleIndex);
                eventTable[key] = linearModuleIndex;
                ++linearModuleIndex;

                timestampExtractors.push_back(crateSetup.moduleTimestampExtractors.at(moduleIndex));
                matchWindows.push_back(crateSetup.moduleMatchWindows.at(moduleIndex));
            }

            eventBuffers.resize(eventBuffers.size() + moduleCount);
            memCounters.resize(memCounters.size() + moduleCount);
            discardedEvents.resize(discardedEvents.size() + moduleCount);
            emptyEvents.resize(emptyEvents.size() + moduleCount);
            invScores.resize(invScores.size() + moduleCount);
            hits.resize(hits.size() + moduleCount);
        }

        size_t mainModuleLinearIndex = d->getLinearModuleIndex(
            eventSetup.mainModule.first, // crateIndex
            eventIndex,
            eventSetup.mainModule.second); // moduleIndex

        d->mainModuleLinearIndexes_[eventIndex] = mainModuleLinearIndex;
    }
}

EventBuilder::~EventBuilder()
{
}

EventBuilder::EventBuilder(EventBuilder &&o)
{
    d = std::move(o.d);
}

EventBuilder &EventBuilder::operator=(EventBuilder &&o)
{
    d = std::move(o.d);
    return *this;
}

bool EventBuilder::isEnabledFor(int eventIndex) const
{
    if (0 <= eventIndex && static_cast<size_t>(eventIndex) < d->setups_.size())
        return d->setups_[eventIndex].enabled;
    return false;
}

bool EventBuilder::isEnabledForAnyEvent() const
{
    return std::any_of(d->setups_.begin(), d->setups_.end(),
                       [] (const EventSetup &setup) { return setup.enabled; });
}

void EventBuilder::recordEventData(int crateIndex, int eventIndex,
                                   const ModuleData *moduleDataList, unsigned moduleCount)
{
    auto logger = get_logger("event_builder");

    // lock, then copy the data to an internal buffer
    UniqueLock guard(d->mutex_);

    assert(0 <= crateIndex);
    assert(0 <= eventIndex);

    // Store non eb event data so that it can be yielded from the eb thread in
    // buildEvents().
    if (!isEnabledFor(eventIndex))
    {
        PassthroughEventStorage storage;
        storage.crateIndex = crateIndex;
        storage.eventIndex = eventIndex;
        for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            auto &data = moduleDataList[moduleIndex].data;
            std::vector<u32> moduleData(data.data, data.data + data.size);
            storage.moduleData.emplace_back(moduleData);
        }
        assert(storage.moduleData.size() == moduleCount);
        d->passThroughEvents_.emplace_back(storage);
        return;
    }

    assert(isEnabledFor(eventIndex));

    // Memory usage check and possible discarding of all buffered data.
    if (d->getMemoryUsage() >= d->memoryLimit_)
    {
        logger->warn("recordEventData(): memory limit exceeded, discarding all data");
        d->discardAllEventData();
    }

    // Now record the module data.
    try
    {
        auto &moduleEventBuffers = d->moduleEventBuffers_.at(eventIndex);
        auto &moduleMemCounters = d->moduleMemCounters_.at(eventIndex);
        auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);
        auto &emptyEvents = d->moduleEmptyEvents_.at(eventIndex);
        auto &moduleHits = d->moduleTotalHits_.at(eventIndex);

        for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            const auto linearModuleIndex = d->getLinearModuleIndex(crateIndex, eventIndex, moduleIndex);

            assert(linearModuleIndex < moduleEventBuffers.size());

            ++moduleHits.at(linearModuleIndex);

            auto moduleData = moduleDataList[moduleIndex];
            auto &data = moduleData.data;

            // The readout parser can yield zero length data if a module is read
            // out using a block transfer but the module has not converted any
            // events at all. In this case it will immediately raise BERR on the
            // VME bus. This is different than the case where the module got a
            // trigger but no channel was within the thresholds. Then we do get an
            // event consisting of only the header and footer (containing the
            // timestamp).
            // The zero length events need to be skipped as there is no timestamp
            // information contained within and the builder code assumes non-zero
            // data for module events.
            if (data.size == 0)
            {
                ++emptyEvents.at(linearModuleIndex);
                continue;
            }

            u32 timestamp = timestampExtractors.at(linearModuleIndex)(data.data, data.size);
            assert(timestamp <= event_builder::TimestampMax
                   || timestamp == event_builder::TimestampExtractionFailed);

            ModuleEventStorage eventStorage =
            {
                timestamp,
                { data.data, data.data + data.size },
            };

            size_t usedMem = eventStorage.usedMemory();

            moduleEventBuffers.at(linearModuleIndex).emplace_back(eventStorage);
            moduleMemCounters.at(linearModuleIndex) += usedMem;
        }
    }
    catch (const std::exception &e)
    {
        logger->error("recordEventData(): {}", e.what());
        throw;
    }

    size_t usedMem = d->getMemoryUsage();
    d->maxUsedMemory_ = std::max(d->maxUsedMemory_, usedMem);

    logger->trace("recordEventData(): memory usage is {}", usedMem);

    guard.unlock();
    d->cv_.notify_one();
}

void EventBuilder::recordSystemEvent(int crateIndex, const u32 *header, u32 size)
{
    // lock, then copy the data to an internal buffer
    SystemEventStorage ses = { crateIndex, { header, header + size } };

    UniqueLock guard(d->mutex_);
    d->systemEvents_.emplace_back(ses);

    guard.unlock();
    d->cv_.notify_one();
}

bool EventBuilder::waitForData(const std::chrono::milliseconds &maxWait)
{
    auto predicate = [this] ()
    {
        if (!d->systemEvents_.empty())
            return true;

        for (const auto &moduleBuffers: d->moduleEventBuffers_)
        {
            for (const auto &moduleBuffer: moduleBuffers)
            {
                if (!moduleBuffer.empty())
                    return true;
            }
        }

        return false;
    };

    UniqueLock guard(d->mutex_);
    return d->cv_.wait_for(guard, maxWait, predicate);
}

size_t EventBuilder::buildEvents(Callbacks callbacks, bool flush)
{
    UniqueLock guard(d->mutex_);

    // system events
    while (!d->systemEvents_.empty())
    {
        auto &ses = d->systemEvents_.front();
        callbacks.systemEvent(d->userContext_, ses.crateIndex, ses.data.data(), ses.data.size());
        d->systemEvents_.pop_front();
    }

    assert(d->systemEvents_.empty());

    // non-eb events (passthrough)
    while (!d->passThroughEvents_.empty())
    {
        auto es = d->passThroughEvents_.front();
        unsigned moduleCount = es.moduleData.size();

        d->eventAssembly_.resize(moduleCount);

        for (unsigned moduleIndex=0; moduleIndex<moduleCount; ++moduleIndex)
        {
            d->eventAssembly_[moduleIndex].data = {
                es.moduleData[moduleIndex].data(),
                static_cast<unsigned>(es.moduleData[moduleIndex].size()),
            };
        }

        callbacks.eventData(
            d->userContext_, es.crateIndex, es.eventIndex,
            d->eventAssembly_.data(), d->eventAssembly_.size());

        d->passThroughEvents_.pop_front();
    }

    assert(d->passThroughEvents_.empty());

    // readout event building
    const size_t eventCount = d->setups_.size();
    size_t result = 0u;

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        if (d->setups_[eventIndex].enabled)
            result += d->buildEvents(eventIndex, callbacks, flush);
    }

    return result;
}

#if 0
// Version 2:
// - get rid of minMainModuleEvents
// - instead attempt to yield events on every call:
//   * if main module is not present -> return
//   * discard module data that is too old
//   * if all modules are in the match window: yield the event, pop the data afterwards
//   * if some module data is too new: return and attempt to build the
//     specific main module event again on the next call
size_t EventBuilder::Private::buildEvents(int eventIndex, Callbacks &callbacks, bool flush)
{
    auto &eventBuffers = moduleEventBuffers_.at(eventIndex);
    const auto &matchWindows = moduleMatchWindows_.at(eventIndex);
    assert(eventBuffers.size() == matchWindows.size());
    const size_t moduleCount = eventBuffers.size();
    auto mainModuleIndex = mainModuleLinearIndexes_.at(eventIndex);
    assert(mainModuleIndex < moduleCount);
    const auto &mainBuffer = eventBuffers.at(mainModuleIndex);
    auto &discardedEvents = moduleDiscardedEvents_.at(eventIndex);
    auto &invScores = moduleInvScoreSums_.at(eventIndex);
    auto &memCounters = moduleMemCounters_.at(eventIndex);

    // Have to always resize as module counts vary for different eventIndexes.
    eventAssembly_.resize(moduleCount);

    size_t result = 0u;
    bool buildingDone = false;

    while (!mainBuffer.empty() && !buildingDone)
    {
        u32 mainModuleTimestamp = eventBuffers.at(mainModuleIndex).front().timestamp;
        std::fill(eventAssembly_.begin(), eventAssembly_.end(), ModuleData{});
        u32 eventInvScore = 0u;

        for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            if (eventBuffers.at(moduleIndex).empty() && !flush)
            {
                buildingDone = true;
                break;
            }

            auto &matchWindow = matchWindows[moduleIndex];
            bool eventDone = false;

            while (!eventDone && !eventBuffers.at(moduleIndex).empty())
            {
                auto &moduleEvent = eventBuffers.at(moduleIndex).front();
                auto matchResult = timestamp_match(mainModuleTimestamp, moduleEvent.timestamp, matchWindow);

                switch (matchResult.match)
                {
                    case WindowMatch::too_old:
                        {
                            const auto &eventStorage = eventBuffers.at(moduleIndex).front();
                            size_t usedMem = eventStorage.usedMemory();
                            assert(memCounters.at(moduleIndex) >= usedMem);
                            memCounters.at(moduleIndex) -= usedMem;
                            eventBuffers.at(moduleIndex).pop_front();
                            ++discardedEvents.at(moduleIndex);
                        }
                        break;

                    case WindowMatch::in_window:
                        eventAssembly_[moduleIndex] = module_data_from_event_storage(moduleEvent);
                        eventInvScore += matchResult.invscore;
                        invScores.at(moduleIndex) += matchResult.invscore;
                        eventDone = true;
                        break;

                    case WindowMatch::too_new:
                        eventDone = true;
                        buildingDone = !flush;
                        break;
                }
            }
        }

        // Modules have been checked, eventAssembly has been filled if
        // possible. Now yield the assembled event.
        if (flush || std::all_of(std::begin(eventAssembly_), std::end(eventAssembly_),
                                 [] (const ModuleData &d) { return d.data.data != nullptr; }))
        {
            const int crateIndex = 0;
            callbacks.eventData(userContext_, crateIndex, eventIndex, eventAssembly_.data(), moduleCount);
            ++result;

            // Now, after, the callback, pop the consumed module events off the deques.
            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto &moduleData = eventAssembly_[moduleIndex];

                if (moduleData.data.data)
                {
                    assert(!eventBuffers.at(moduleIndex).empty());
                    const auto &eventStorage = eventBuffers.at(moduleIndex).front();
                    size_t usedMem = eventStorage.usedMemory();
                    assert(memCounters.at(moduleIndex) >= usedMem);
                    memCounters.at(moduleIndex) -= usedMem;
                    eventBuffers.at(moduleIndex).pop_front();
                }
            }
        }
    }

    if (flush)
    {
        // Flush out all remaining data. This should only be module
        // events that are too new and thus do not fall into the match
        // window.
        std::for_each(std::begin(eventBuffers), std::end(eventBuffers),
                      [] (auto &eb) { eb.clear(); });

        assert(std::all_of(std::begin(eventBuffers), std::end(eventBuffers),
                           [] (const auto &eb) { return eb.empty(); }));

        std::fill(std::begin(memCounters), std::end(memCounters), 0u);
    }

    return result;
}
#endif
#if 1
// Version 3:
// - Get rid of the 'buildingDone' boolean.
// - Behavior change: try to only yield complete events, meaning events where
// all modules are present.
//
size_t EventBuilder::Private::buildEvents(int eventIndex, Callbacks &callbacks, bool flush)
{
    if (flush)
        get_logger("event_builder")->debug("Private::buildEvents(): flush requested!");

    auto &eventBuffers = moduleEventBuffers_.at(eventIndex);
    const auto &matchWindows = moduleMatchWindows_.at(eventIndex);
    assert(eventBuffers.size() == matchWindows.size());
    const size_t moduleCount = eventBuffers.size();
    auto mainModuleIndex = mainModuleLinearIndexes_.at(eventIndex);
    assert(mainModuleIndex < moduleCount);
    const auto &mainBuffer = eventBuffers.at(mainModuleIndex);
    auto &discardedEvents = moduleDiscardedEvents_.at(eventIndex);
    auto &invScores = moduleInvScoreSums_.at(eventIndex);
    auto &memCounters = moduleMemCounters_.at(eventIndex);

    // Have to always resize as module counts vary for different eventIndexes.
    eventAssembly_.resize(moduleCount);

    size_t result = 0u;

    // Loop while we have data from the main module and none of the module
    // buffers are empty or we're flushing.
    while (!mainBuffer.empty() &&
           (flush || !std::any_of(std::begin(eventBuffers), std::end(eventBuffers),
                                  [] (const auto &buffer) { return buffer.empty(); })))
    {
        u32 mainModuleTimestamp = mainBuffer.front().timestamp;
        std::fill(eventAssembly_.begin(), eventAssembly_.end(), ModuleData{});
        u32 eventInvScore = 0u;

        for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            auto &matchWindow = matchWindows[moduleIndex];
            bool moduleDone = false;
            auto &eventBuffer = eventBuffers.at(moduleIndex);

            while (!moduleDone && !eventBuffer.empty())
            {
                auto &moduleEvent = eventBuffer.front();
                WindowMatchResult matchResult = {};

                if (moduleEvent.timestamp != event_builder::TimestampExtractionFailed)
                    matchResult = timestamp_match(mainModuleTimestamp, moduleEvent.timestamp, matchWindow);
                else
                {
                    // The extractor returned TimestampExtractionFailed. Either the module data
                    // did not contain a timestamp or the InvalidTimestampExtractor is used to
                    // make the EventBuilder ignore the module.
                    matchResult.match = WindowMatch::in_window;
                    matchResult.invscore = std::numeric_limits<u32>::max();
                }

                switch (matchResult.match)
                {
                    case WindowMatch::too_old:
                        {
                            // This module event is too old relative to the
                            // main module. It cannot be matched at any future
                            // point in time. Pop the event off the queue and
                            // update counters.
                            size_t usedMem = moduleEvent.usedMemory();
                            assert(memCounters.at(moduleIndex) >= usedMem);
                            memCounters.at(moduleIndex) -= usedMem;
                            eventBuffer.pop_front();
                            ++discardedEvents.at(moduleIndex);
                        }
                        break;

                    case WindowMatch::in_window:
                        // The module event is in the timestamp match window.
                        // Update the event assembly and counters but do not
                        // pop the event off the queue yet as the eventAssembly
                        // points to the queue.
                        eventAssembly_[moduleIndex] = module_data_from_event_storage(moduleEvent);
                        eventInvScore += matchResult.invscore;
                        invScores.at(moduleIndex) += matchResult.invscore;
                        moduleDone = true;
                        break;

                    case WindowMatch::too_new:
                        // The module event is too young to be matched with the
                        // current main module event.
                        moduleDone = true;
                        break;
                }
            }
        }

        // At this point module events that are too old to be matched with the
        // current main module event have been popped of the queues. An attempt
        // to match all modules in the current event has been made but some of
        // the modules may contain null data in case no window match was found.
        // No match can mean that either the module data is too new to be
        // matched with the current main module event or there is no more data
        // for the respective module as it has not arrived yet.

        if (!flush)
        {
            bool yieldEvent = true;

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                if (!eventAssembly_.at(moduleIndex).data.data && eventBuffers.at(moduleIndex).empty())
                {
                    yieldEvent = false;
                    break;
                }
            }

            if (!yieldEvent)
                break;
        }

        {
            // Assembled events are always mapped to crate 0.
            const int crateIndex = 0;
            callbacks.eventData(userContext_, crateIndex, eventIndex, eventAssembly_.data(), moduleCount);
            ++result;

            // After the callback we can pop the consumed module events off the deques.
            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto &moduleData = eventAssembly_[moduleIndex];

                if (moduleData.data.data)
                {
                    assert(!eventBuffers.at(moduleIndex).empty());
                    const auto &eventStorage = eventBuffers.at(moduleIndex).front();
                    size_t usedMem = eventStorage.usedMemory();
                    assert(memCounters.at(moduleIndex) >= usedMem);
                    memCounters.at(moduleIndex) -= usedMem;
                    eventBuffers.at(moduleIndex).pop_front();
                }
            }
        }
    }

    if (flush)
    {
        // Flush out all remaining data that's left in the buffers.
        std::for_each(std::begin(eventBuffers), std::end(eventBuffers),
                      [] (auto &eb) { eb.clear(); });

        assert(std::all_of(std::begin(eventBuffers), std::end(eventBuffers),
                           [] (const auto &eb) { return eb.empty(); }));

        // Zero out the per module memory usage counters.
        std::fill(std::begin(memCounters), std::end(memCounters), 0u);
    }

    get_logger("event_builder")->trace("buildEvents(): built {} events", result);

    return result;
}
#endif

EventBuilder::EventCounters EventBuilder::getCounters(int eventIndex) const
{
    UniqueLock guard(d->mutex_);
    return d->getCounters(eventIndex);
}

EventBuilder::EventBuilderCounters EventBuilder::getCounters() const
{
    UniqueLock guard(d->mutex_);

    std::vector<EventCounters> eventCounters;
    eventCounters.reserve(d->moduleDiscardedEvents_.size());

    for (size_t ei=0; ei<d->moduleDiscardedEvents_.size(); ++ei)
        eventCounters.emplace_back(d->getCounters(ei));

    EventBuilderCounters result;
    result.eventCounters = std::move(eventCounters);
    result.maxMemoryUsage = d->maxUsedMemory_;
    return result;
}

size_t EventBuilder::getMemoryUsage() const
{
    UniqueLock guard(d->mutex_);
    return d->getMemoryUsage();
}

size_t EventBuilder::getMaxMemoryUsage() const
{
    UniqueLock guard(d->mutex_);
    return d->maxUsedMemory_;
}

void EventBuilder::discardAllEventData()
{
    UniqueLock guard(d->mutex_);
    d->discardAllEventData();
}

void EventBuilder::reset()
{
    UniqueLock guard(d->mutex_);
    d->discardAllEventData();
    d->resetCounters();
}

size_t EventBuilder::getLinearModuleIndex(int crateIndex, int eventIndex, unsigned moduleIndex) const
{
    return d->getLinearModuleIndex(crateIndex, eventIndex, moduleIndex);
}

}
}
