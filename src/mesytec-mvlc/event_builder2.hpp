#ifndef BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32
#define BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/storage_sizes.h>

namespace mesytec::mvlc::event_builder
{

enum class WindowMatch
{
    too_old,
    in_window,
    too_new
};

WindowMatch MESYTEC_MVLC_EXPORT timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow);

using ModuleData = readout_parser::ModuleData;
using Callbacks = readout_parser::ReadoutParserCallbacks;
using timestamp_extractor = std::function<u32(const u32 *data, size_t size)>;

static const auto DefaultMatchWindow = std::make_pair<s32, s32>(-8, 8);
static const size_t DefaultMemoryLimit = util::Gigabytes(1);
static const u32 TimestampMax = 0x3fffffffu; // 30 bits
static const u32 TimestampHalf = TimestampMax >> 1;
static const u32 TimestampExtractionFailed = 0xffffffffu;

struct MESYTEC_MVLC_EXPORT IndexedTimestampFilterExtractor
{
  public:
    IndexedTimestampFilterExtractor(const util::DataFilter &filter, s32 wordIndex,
                                    char matchChar = 'D');

    u32 operator()(const u32 *data, size_t size);

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

    u32 operator()(const u32 *data, size_t size);

  private:
    util::DataFilter filter_;
    util::CacheEntry filterCache_;
};

// Always produces a TimestampExtractionFailed result. Used to skip over
// modules which should be ignored in the EventBuilder.
struct MESYTEC_MVLC_EXPORT InvalidTimestampExtractor
{
    u32 operator()(const u32 *,  size_t)
    {
        return event_builder::TimestampExtractionFailed;
    }
};

struct MESYTEC_MVLC_EXPORT ModuleConfig
{
    timestamp_extractor tsExtractor;
    s32 offset; // Offset applied to the extracted timestamp. Used to correct for module specific
                // timestamp offsets.
    std::pair<s32, s32> matchWindow;
    bool ignored = false;
};

struct MESYTEC_MVLC_EXPORT EventConfig
{
    std::vector<ModuleConfig> moduleConfigs;
};

struct MESYTEC_MVLC_EXPORT EventBuilderConfig
{
    std::vector<EventConfig> eventConfigs;
    int outputCrateIndex = 0;
};

class EventBuilder2
{
    public:
        EventBuilder2(const EventBuilderConfig &cfg, Callbacks callbacks, void *userContext = nullptr);
        ~EventBuilder2();

        void handleModuleData(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount);
        void handleSystemEvent(const u32 *header, u32 size);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // namespace mesytec::mvlc::event_builder

#endif /* BC399B1E_8B8A_42B2_8AD4_CB28FCE98B32 */
