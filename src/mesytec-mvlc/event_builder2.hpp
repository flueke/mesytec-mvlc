#ifndef BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32
#define BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32

#include <optional>

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/storage_sizes.h>

namespace mesytec::mvlc::event_builder2
{

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

WindowMatchResult MESYTEC_MVLC_EXPORT timestamp_match(s64 tsMain, s64 tsModule, u32 windowWidth);

using ModuleData = readout_parser::ModuleData;
using Callbacks = readout_parser::ReadoutParserCallbacks;
using timestamp_extractor = std::function<std::optional<u32>(const u32 *data, size_t size)>;

static const auto DefaultMatchOffset = 0u;
static const auto DefaultMatchWindow = 16u;
static const u32 TimestampMax = 0x3fffffffu; // 30 bits
static const u32 TimestampHalf = TimestampMax >> 1;
//static const u32 TimestampExtractionFailed = 0xffffffffu;

struct MESYTEC_MVLC_EXPORT IndexedTimestampFilterExtractor
{
  public:
    IndexedTimestampFilterExtractor(const util::DataFilter &filter, s32 wordIndex,
                                    char matchChar = 'D');

    std::optional<u32> operator()(const u32 *data, size_t size);

  private:
    util::DataFilter filter_;
    util::CacheEntry filterCache_;
    s32 index_;
};

inline IndexedTimestampFilterExtractor make_mesytec_default_timestamp_extractor()
{
    return IndexedTimestampFilterExtractor(
        util::make_filter("11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"), // 30 bit non-extended timestamp
        -1); // directly index the last word of the module data
}

struct MESYTEC_MVLC_EXPORT TimestampFilterExtractor
{
  public:
    TimestampFilterExtractor(const util::DataFilter &filter, char matchChar = 'D');

    std::optional<u32> operator()(const u32 *data, size_t size);

  private:
    util::DataFilter filter_;
    util::CacheEntry filterCache_;
};

#if 0
struct MESYTEC_MVLC_EXPORT FailingTimestampExtractor
{
    std::optional<u32> operator()(const u32 *, size_t) { return TimestampExtractionFailed; }
};
#endif

struct MESYTEC_MVLC_EXPORT EmptyTimestampExtractor
{
    std::optional<u32> operator()(const u32 *, size_t) { return {}; }
};

// Configuration ==========

struct MESYTEC_MVLC_EXPORT ModuleConfig
{
    timestamp_extractor tsExtractor;
    s32 offset; // Offset applied to the extracted timestamp. Used to correct for module specific
                // timestamp offsets.
    u32 window; // Width of the match window in timestamp units.
    bool ignored = false; // If true this module does not contribute reference timestamps.
};

struct MESYTEC_MVLC_EXPORT EventConfig
{
    std::vector<ModuleConfig> moduleConfigs;
    bool enabled; // true if event building is enabled for this event
};

struct MESYTEC_MVLC_EXPORT EventBuilderConfig
{
    std::vector<EventConfig> eventConfigs;
    int outputCrateIndex = 0;
};

// Counters and Stats ==========

struct EventCounters
{
    // data is stored per module
    std::vector<size_t> inputHits;
    std::vector<size_t> outputHits;
    std::vector<size_t> emptyInputs;
    std::vector<size_t> discardsAge;   // number of event discarded due to stamp age
    std::vector<size_t> stampFailed;   // number of failed stamp extractions

    // these can be determinted from the contents of the data buffers
    std::vector<size_t> currentEvents; // current events in the buffer
    std::vector<size_t> currentMem;    // current buffer memory usage

    // these are upated in tryFlush() and/or periodcally? not sure yet. possibly on-demand only when
    // a getCounters() method is called
    std::vector<size_t> maxEvents;     // max events buffered so far (until flushed)
    std::vector<size_t> maxMem;        // max mem usage so far (until flushed)

    // non-module specific
    size_t recordingFailed = 0;
};

std::string dump_counters(const EventCounters &counters);

struct BuilderCounters
{
    std::vector<EventCounters> eventCounters;
};

class MESYTEC_MVLC_EXPORT EventBuilder2
{
  public:
    EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext = nullptr);
    EventBuilder2(const EventBuilderConfig &cfg, void *userContext = nullptr);
    explicit EventBuilder2();
    ~EventBuilder2();

    EventBuilder2(EventBuilder2 &&);
    EventBuilder2 &operator=(EventBuilder2 &&);

    void setCallbacks(const Callbacks &callbacks);
    bool recordModuleData(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount);
    // directly invokes the output system event callback
    void handleSystemEvent(const u32 *header, u32 size);
    // returns the total number of data events flushed to the callbacks
    // if force is true all remaining events will be output until all module buffers are empty
    size_t flush(bool force = false);

    std::string debugDump() const;
    // std::string debugDumpConfig() const;

    bool isEnabledForAnyEvent() const;
    BuilderCounters getCounters() const;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvlc::event_builder2

#endif /* BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32 */
