#include "event_builder2.hpp"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::event_builder2;

#if 1
TEST(EventBuilder2, TimestampOffset)
{
    // ts = TimestampMax

    {
        u32 ts = TimestampMax;
        s32 offset = 0;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, TimestampMax);
    }

    {
        u32 ts = TimestampMax;
        s32 offset = 1;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, 0u);
    }

    {
        u32 ts = TimestampMax;
        s32 offset = -1;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, TimestampMax - 1);
    }

    {
        u32 ts = TimestampMax;
        s32 offset = TimestampMax;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, TimestampMax - 1);
    }

    {
        u32 ts = TimestampMax;
        s32 offset = -TimestampMax;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, 0);
    }

    // ts = 0

    {
        u32 ts = 0;
        s32 offset = 0;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, 0);
    }

    {
        u32 ts = 0;
        s32 offset = 1;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, 1);
    }

    {
        u32 ts = 0;
        s32 offset = -1;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, TimestampMax);
    }

    {
        u32 ts = 0;
        s32 offset = TimestampMax;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, TimestampMax);
    }

    {
        u32 ts = 0;
        s32 offset = -TimestampMax;
        u32 result = add_offset_to_timestamp(ts, offset);
        ASSERT_EQ(result, 1);
    }
}
#endif

#if 1
TEST(EventBuilder2, TimestampMatch2)
{
    WindowMatchResult mr = {};

    mr = timestamp_match(150, 99, 100);
    ASSERT_EQ(mr.match, WindowMatch::too_old);
    ASSERT_EQ(mr.invscore, 51u);

    mr = timestamp_match(150, 100, 100);
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 200, 100);
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 201, 100);
    ASSERT_EQ(mr.match, WindowMatch::too_new);
    ASSERT_EQ(mr.invscore, 51u);

    mr = timestamp_match(0, 10, 20);
    ASSERT_EQ(mr.match, WindowMatch::in_window);

    //
    mr = timestamp_match(5, 10, 10);
    ASSERT_EQ(mr.match, WindowMatch::in_window);

    mr = timestamp_match(15, 10, 10);
    ASSERT_EQ(mr.match, WindowMatch::in_window);

    mr = timestamp_match(4, 10, 10);
    ASSERT_EQ(mr.match, WindowMatch::too_new);

    mr = timestamp_match(16, 10, 10);
    ASSERT_EQ(mr.match, WindowMatch::too_old);

    // TODO: test the extreme cases where a difference of TimestampHalf is reached (and almost
    // reached)
}
#endif

namespace
{

struct ModuleDataStorage
{
    std::vector<u32> data;
};

// Returns a Callback compatible vector of ModuleData structures pointing
// into the moduleTestData storage.
std::vector<ModuleData> to_module_data_list(const std::vector<ModuleDataStorage> &moduleTestData)
{
    std::vector<ModuleData> result(moduleTestData.size());

    for (size_t mi = 0; mi < moduleTestData.size(); ++mi)
    {
        result[mi] = {};
        result[mi].data.data = moduleTestData[mi].data.data();
        result[mi].data.size = moduleTestData[mi].data.size();
        result[mi].dynamicSize = moduleTestData[mi].data.size();
        result[mi].hasDynamic = true;
    }

    return result;
}

std::optional<u32> simple_timestamp_extractor(const u32 *data, size_t size)
{
    if (size > 0)
        return data[0];
    return {};
}

} // namespace

#if 1
TEST(EventBuilder2, OneModule)
{
    const std::vector<u32> sysEventData = {0x12345678, 0x87654321};
    std::vector<ModuleDataStorage> moduleTestData;

    ModuleConfig moduleConfig;
    moduleConfig.tsExtractor = simple_timestamp_extractor;
    moduleConfig.window = 20;
    moduleConfig.offset = 0; // offset does not matter with only one module
    EventConfig eventConfig;
    eventConfig.moduleConfigs.push_back(moduleConfig);
    eventConfig.enabled = true;
    EventBuilderConfig cfg;
    cfg.eventConfigs.push_back(eventConfig);

    size_t dataCallbackCount = 0;
    size_t systemCallbackCount = 0;

    auto eventDataCallback = [&](void *, int crateIndex, int eventIndex,
                                 const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ++dataCallbackCount;
        fmt::print("eventDataCallback: crateIndex={}, eventIndex={}, moduleCount={}\n", crateIndex,
                   eventIndex, moduleCount);
        fmt::print("eventDataCallback: module0: size={}, data[0]={}\n", moduleDataList[0].data.size,
                   moduleDataList[0].data.data[0]);
    };

    auto systemEventCallback = [&](void *, int crateIndex, const u32 *header, u32 size)
    {
        ++systemCallbackCount;
        fmt::print("systemEventCallback: crateIndex={}, size={}\n", crateIndex, size);
    };

    EventBuilder2 eb(cfg, {eventDataCallback, systemEventCallback});

    moduleTestData = {{{0}}}; // ts0 = 0, cannot yield yet
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    eb.handleSystemEvent(sysEventData.data(), sysEventData.size());

    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);
    ASSERT_EQ(systemCallbackCount, 1);

    moduleTestData = {{{5}}}; // ts1 = 5, still cannot yield as no stamp is "too_new".
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);
    ASSERT_EQ(systemCallbackCount, 1);

    moduleTestData = {{{10}}}; // ts2 = 10, still in window (0+=10), so no yield yet.
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);
    ASSERT_EQ(systemCallbackCount, 1);

    moduleTestData = {{{11}}}; // ts3 = 11, outside the window (0+=10), so event0 is yielded
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 1);
    ASSERT_EQ(systemCallbackCount, 1);

    // remaining stamps are 5, 10, 11. Need 5 + 10 + 1 to yield
    moduleTestData = {{{15}}}; // ts4 = 15, inside the window, so no yield yet.
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 1);
    ASSERT_EQ(systemCallbackCount, 1);

    // remaining stamps are 5, 10, 11, 15. Need 5 + 10 + 1 to yield
    moduleTestData = {{{16}}}; // ts5 = 16, outside window, so event1 is yielded
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 2);
    ASSERT_EQ(systemCallbackCount, 1);

    fmt::print(eb.debugDump());
    // remaining stamps are 10, 11, 15, 16. Force flush now
    ASSERT_EQ(eb.flush(true), 4);
    ASSERT_EQ(dataCallbackCount, 2 + 4);
    ASSERT_EQ(systemCallbackCount, 1);
    fmt::print(eb.debugDump());
}
#endif

#if 1
TEST(EventBuilder2, TwoModules)
{
    ModuleConfig mod0;
    mod0.tsExtractor = simple_timestamp_extractor;
    mod0.window = 20;
    mod0.offset = 0;

    ModuleConfig mod1;
    mod1.tsExtractor = simple_timestamp_extractor;
    mod1.window = 20;
    mod1.offset = 0;

    EventConfig eventConfig;
    eventConfig.moduleConfigs = {mod0, mod1};
    eventConfig.enabled = true;
    EventBuilderConfig cfg;
    cfg.eventConfigs.push_back(eventConfig);

    size_t dataCallbackCount = 0;

    auto eventDataCallback = [&](void *, int crateIndex, int eventIndex,
                                 const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ++dataCallbackCount;
        fmt::print("eventDataCallback: crateIndex={}, eventIndex={}, moduleCount={}\n", crateIndex,
                   eventIndex, moduleCount);
        for (size_t i = 0; i < moduleCount; ++i)
            fmt::print("eventDataCallback:   module{}: size={}, data[0]={}\n", i,
                       moduleDataList[0].data.size, moduleDataList[0].data.data[0]);
    };

    auto systemEventCallback = [&](void *, int, const u32 *, u32) {};

    EventBuilder2 eb(cfg, {eventDataCallback, systemEventCallback});

    std::vector<ModuleDataStorage> moduleTestData;

    moduleTestData = {{{0}}, {{0}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());

    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);
    fmt::print(eb.debugDump());

    moduleTestData = {{{5}}, {{5}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);

    moduleTestData = {{{10}}, {{10}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);

    moduleTestData = {{{11}}, {{11}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    fmt::print(eb.debugDump());
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 1);

    // remaining stamps are 5, 10, 11. Need 5 + 10 + 1 to yield
    moduleTestData = {{{15}}, {{15}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 1);

    // remaining stamps are 5, 10, 11, 15. Need 5 + 10 + 1 to yield
    moduleTestData = {{{16}}, {{16}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 2);

    fmt::print(eb.debugDump());
    // remaining stamps are 10, 11, 15, 16. Force flush now
    ASSERT_EQ(eb.flush(true), 4);
    ASSERT_EQ(dataCallbackCount, 2 + 4);
    fmt::print(eb.debugDump());
}
#endif

#if 1
TEST(EventBuilder2, TwoModulesOneIsSlow)
{
    ModuleConfig mod0;
    mod0.tsExtractor = simple_timestamp_extractor;
    mod0.window = 20;
    mod0.offset = 0;

    ModuleConfig mod1;
    mod1.tsExtractor = simple_timestamp_extractor;
    mod1.window = 20;
    mod1.offset = 0;

    EventConfig eventConfig;
    eventConfig.moduleConfigs = {mod0, mod1};
    eventConfig.enabled = true;
    EventBuilderConfig cfg;
    cfg.eventConfigs.push_back(eventConfig);

    size_t dataCallbackCount = 0;

    auto eventDataCallback = [&](void *, int crateIndex, int eventIndex,
                                 const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ++dataCallbackCount;
        spdlog::info("eventDataCallback: crateIndex={}, eventIndex={}, moduleCount={}", crateIndex,
                     eventIndex, moduleCount);
        for (size_t i = 0; i < moduleCount; ++i)
        {
            spdlog::info(
                "eventDataCallback: module{}: size={}, data={}", i, moduleDataList[i].data.size,
                fmt::join(moduleDataList[i].data.data,
                          moduleDataList[i].data.data + moduleDataList[i].data.size, ", "));
        }
    };

    auto systemEventCallback = [&](void *, int, const u32 *, u32) {};

    EventBuilder2 eb(cfg, {eventDataCallback, systemEventCallback});

    std::vector<ModuleDataStorage> moduleTestData;

    moduleTestData = {{{0}}, {{0}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());

    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);
    fmt::print(eb.debugDump());

    moduleTestData = {{{5}}, {{5}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);

    moduleTestData = {{{10}}, {{10}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 0);

    moduleTestData = {{{11}}, {{}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    fmt::print(eb.debugDump());
    // 0, 5, 10, 11 and 0, 5, 10, <artifical 11 from mod0> -> yield
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 1);

    moduleTestData = {{{15}}, {{15}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    // 5, 10, 11, 15 and 5, 10, 11, 15 -> cannot yield due to age
    ASSERT_EQ(eb.flush(), 0);
    ASSERT_EQ(dataCallbackCount, 1);

    moduleTestData = {{{16}}, {{16}}};
    eb.recordModuleData(0, to_module_data_list(moduleTestData).data(), moduleTestData.size());
    // 5, 10, 11, 15, 16 and 5, 10, 11, 15, 16 -> yield
    ASSERT_EQ(eb.flush(), 1);
    ASSERT_EQ(dataCallbackCount, 2);

    // 10, 11, 15, 16 and 10, 11, 15, 16 -> cannot yield due to age
    ASSERT_EQ(eb.flush(true), 4);
    ASSERT_EQ(dataCallbackCount, 6);
}
#endif

TEST(EventBuilder2, VectorAtIsWeird)
{
    {
        std::vector<int> v1 = {0, 1, 2};
        ASSERT_EQ(v1.size(), 3);
        ASSERT_EQ(v1.at(0), 0);
        ASSERT_EQ(v1.at(1), 1);
        ASSERT_EQ(v1.at(2), 2);
        ASSERT_THROW(static_cast<void>(v1.at(3)), std::out_of_range);
    }
    {
        std::vector<std::vector<int>> vv1 = {
            {0, 1},
            {1, 2},
            {3, 4},
        };

        ASSERT_EQ(vv1.size(), 3);
        ASSERT_EQ(vv1.at(0).size(), 2);
        ASSERT_EQ(vv1.at(1).size(), 2);
        ASSERT_EQ(vv1.at(2).size(), 2);
    }
}
