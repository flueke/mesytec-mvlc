#ifndef __MESYTEC_MVLC_EVENT_BUILDER_H__
#define __MESYTEC_MVLC_EVENT_BUILDER_H__

#include <functional>
#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_readout_parser.h"
#include "util/data_filter.h"
#include "util/storage_sizes.h"

namespace mesytec
{
namespace mvlc
{

using namespace util;

using ModuleData = readout_parser::ModuleData;
using Callbacks = readout_parser::ReadoutParserCallbacks;
using timestamp_extractor = std::function<u32 (const u32 *data, size_t size)>;

namespace event_builder
{

static const auto DefaultMatchWindow = std::make_pair(-8, 8);
static const size_t DefaultMemoryLimit = util::Gigabytes(1);

static const u32 TimestampMax = 0x3fffffffu; // 30 bits
static const u32 TimestampHalf = TimestampMax >> 1;
static const u32 TimestampExtractionFailed = 0xffffffffu;

}

struct MESYTEC_MVLC_EXPORT IndexedTimestampFilterExtractor
{
    public:
        IndexedTimestampFilterExtractor(const DataFilter &filter, s32 wordIndex, char matchChar = 'D');

        u32 operator()(const u32 *data, size_t size);

    private:
        DataFilter filter_;
        CacheEntry filterCache_;
        s32 index_;
};

inline IndexedTimestampFilterExtractor make_mesytec_default_timestamp_extractor()
{
    return IndexedTimestampFilterExtractor(
        make_filter("11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"), // 30 bit non-extended timestamp
        -1); // directly index the last word of the module data
}

struct MESYTEC_MVLC_EXPORT TimestampFilterExtractor
{
    public:
        TimestampFilterExtractor(const DataFilter &filter, char matchChar = 'D');

        u32 operator()(const u32 *data, size_t size);

    private:
        DataFilter filter_;
        CacheEntry filterCache_;
};

// Always produces a TimestampExtractionFailed result. Used to skip over
// modules which should be ignored in the EventBuilder.
struct MESYTEC_MVLC_EXPORT InvalidTimestampExtractor
{
    u32 operator()(const u32 * /*data*/, size_t /*size*/)
    {
        return event_builder::TimestampExtractionFailed;
    }
};

struct MESYTEC_MVLC_EXPORT EventSetup
{
    struct CrateSetup
    {
        // module timestamp extractors in crate-relative module order
        std::vector<timestamp_extractor> moduleTimestampExtractors;

        // module timestamp match windows in crate-relative module order
        std::vector<std::pair<s32, s32>> moduleMatchWindows;
    };

    // Enable event building across crates for this event.
    bool enabled;

    // Crate setups in crate index order.
    std::vector<CrateSetup> crateSetups;

    // crate and crate-relative indexes of the main module which provides the reference timestamp
    // mainModule.first  := crateIndex
    // mainModule.second := moduleIndex
    std::pair<int, int> mainModule;
};

struct MESYTEC_MVLC_EXPORT EventBuilderConfig
{
    std::vector<EventSetup> setups;
    size_t memoryLimit = event_builder::DefaultMemoryLimit;
};

class MESYTEC_MVLC_EXPORT EventBuilder
{
    public:
        explicit EventBuilder(const EventBuilderConfig &cfg, void *userContext = nullptr);
        ~EventBuilder();

        EventBuilder(EventBuilder &&);
        EventBuilder &operator=(EventBuilder &&);

        EventBuilder(const EventBuilder &) = delete;
        EventBuilder &operator=(const EventBuilder &) = delete;

        bool isEnabledFor(int eventIndex) const;
        bool isEnabledForAnyEvent() const;

        // Push data into the eventbuilder (called after parsing and multi event splitting).
        void recordEventData(int crateIndex, int eventIndex,
                             const ModuleData *moduleDataList, unsigned moduleCount);
        void recordSystemEvent(int crateIndex, const u32 *header, u32 size);

        // Attempt to build the next full events. If successful invoke the
        // callbacks to further process the assembled events. May be called
        // from a different thread than the push*() methods.

        // Note: right now doesn't do any age checking or similar. This means
        // it tries to yield one assembled output event for each input event
        // from the main module.
        size_t buildEvents(Callbacks callbacks, bool flush = false);

        bool waitForData(const std::chrono::milliseconds &maxWait);

        struct EventCounters
        {
            std::vector<size_t> discardedEvents;
            std::vector<size_t> emptyEvents;
            std::vector<size_t> invScoreSums;
            std::vector<size_t> totalHits;
        };

        struct EventBuilderCounters
        {
            std::vector<EventCounters> eventCounters;
            size_t maxMemoryUsage;
        };

        EventCounters getCounters(int eventIndex) const;
        EventBuilderCounters getCounters() const;

        size_t getMemoryUsage() const;
        size_t getMaxMemoryUsage() const;
        void discardAllEventData();
        void reset();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

enum class WindowMatch
{
    too_old,
    in_window,
    too_new
};

struct MESYTEC_MVLC_EXPORT WindowMatchResult
{
    WindowMatch match;
    // The asbsolute distance to the reference timestamp tsMain.
    // 0 -> perfect match, else the higher the worse the match.
    u32 invscore;
};

MESYTEC_MVLC_EXPORT WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow);

}
}

#endif /* __MESYTEC_MVLC_EVENT_BUILDER_H__ */
