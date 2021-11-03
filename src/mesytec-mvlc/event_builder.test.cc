#include <gtest/gtest.h>
#include "event_builder.h"

using namespace mesytec::mvlc;

TEST(event_builder, VectorAtIsWeird)
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
        std::vector<std::vector<int>> vv1 =
        {
            { 0, 1},
            { 1, 2},
            { 3, 4},
        };

        ASSERT_EQ(vv1.size(), 3);
        ASSERT_EQ(vv1.at(0).size(), 2);
        ASSERT_EQ(vv1.at(1).size(), 2);
        ASSERT_EQ(vv1.at(2).size(), 2);
    }
}

TEST(event_builder, TimestampMatch)
{
    WindowMatchResult mr = {};

    mr = timestamp_match(150,  99, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::too_old);
    ASSERT_EQ(mr.invscore, 51u);

    mr = timestamp_match(150, 100, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 200, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 201, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::too_new);
    ASSERT_EQ(mr.invscore, 51u);
}

TEST(event_builder, ConstructDescruct)
{
    EventBuilderConfig cfg;
    EventBuilder eventbuilder(cfg);
}

namespace
{
    // Returns a Callback compatible vector of ModuleData structures pointing
    // into the moduleTestData storage.
    template<typename T>
    std::vector<ModuleData> module_data_list_from_test_data(const T &moduleTestData)
    {
        std::vector<ModuleData> result(moduleTestData.size());

        for (size_t mi = 0; mi < moduleTestData.size(); ++mi)
        {
            result[mi] = {};
            result[mi].data.data = moduleTestData[mi].data();
            result[mi].data.size = moduleTestData[mi].size();
        }

        return result;
    }

    EventSetup make_one_crate_one_event_test_setup()
    {
        auto test_timestamp_extractor = [] (const u32 *moduleData, size_t size) -> u32
        {
            if (size > 0)
                return moduleData[0];
            return 0u;
        };

        EventSetup eventSetup;
        eventSetup.enabled = true;
        eventSetup.mainModule = { 0, 1 }; // crate0, module1
        eventSetup.crateSetups.resize(1);
        eventSetup.crateSetups[0].moduleTimestampExtractors = {
            test_timestamp_extractor, // crate0, module0
            test_timestamp_extractor, // crate0, module1 (**)
            test_timestamp_extractor, // crate0, module2
        };
        eventSetup.crateSetups[0].moduleMatchWindows = {
            {  -50,  75 }, // crate0, module0
            {    0,   0 }, // crate0, module1 (**)
            {  -20, 150 }, // crate0, module2
        };

        return eventSetup;
    }

    const size_t TestSetupModuleCount = 3;
}

TEST(event_builder, MemoryUsageAndDiscarding)
{
    // Storage for the module data
    std::vector<std::array<std::vector<u32>, TestSetupModuleCount>> moduleTestData =
    {
        {
            {
                {0,       }, // crate0, module0
                {0, 1,    }, // crate0, module1 (**)
                {0, 1, 2, }, // crate0, module2
            }
        },
    };

    int crateIndex = 0;
    int eventIndex = 0;
    EventBuilderConfig cfg;
    cfg.setups = std::vector<EventSetup>{ make_one_crate_one_event_test_setup() };
    EventBuilder eventBuilder(cfg);

    ASSERT_EQ(eventBuilder.getMemoryUsage(), 0u);

    auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
    eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

    ASSERT_EQ(eventBuilder.getMemoryUsage(), 6*sizeof(u32));

    eventBuilder.discardAllEventData();

    ASSERT_EQ(eventBuilder.getMemoryUsage(), 0u);
}

TEST(event_builder, SingleCrateWindowMatchingNoOverflow)
{
    // Storage for the module data
    std::vector<std::array<std::vector<u32>, TestSetupModuleCount>> moduleTestData =
    {
        {
            {
                {  25 }, // crate0, module0         too old
                { 150 }, // crate0, module1 (**)
                { 200 }, // crate0, module2         in window
            }
        },
        {
            {
                { 101 }, // crate0, module0         in window but yielded in event#1
                { 151 }, // crate0, module1 (**)
                { 350 }, // crate0, module2         too new
            }
        },
        {
            {
                { 225 }, // crate0, module0         in window but yielded in event#2
                { 252 }, // crate0, module1 (**)
                { 666 }, // crate0, module2         too new but the previous 350 should be yielded
            }
        },
    };

    int crateIndex = 0;
    int eventIndex = 0;
    EventBuilderConfig cfg;
    cfg.setups = std::vector<EventSetup>{ make_one_crate_one_event_test_setup() };

    // push one event, then flush
    {
        EventBuilder eventBuilder(cfg);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            ++dataCallbackCount;
            ASSERT_EQ(moduleDataList[0].data.size, 0);

            ASSERT_EQ(moduleDataList[1].data.size, 1);
            ASSERT_EQ(moduleDataList[1].data.data[0], 150);

            ASSERT_EQ(moduleDataList[2].data.size, 1);
            ASSERT_EQ(moduleDataList[2].data.data[0], 200);
        };

        auto systemEventCallback = [&] (void *, int crateIndex, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 1);

        ASSERT_EQ(dataCallbackCount, 1);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }

    // push two events, then flush
    {
        EventBuilder eventBuilder(cfg);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[1]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            switch (dataCallbackCount++)
            {
                case 0:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 101);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 150);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 200);
                    } break;
#if 0
                case 1:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 0);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 151);

                        ASSERT_EQ(moduleDataList[2].data.size, 0);
                    } break;
#endif
            }
        };

        auto systemEventCallback = [&] (void *, int crateIndex, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 1);

        ASSERT_EQ(dataCallbackCount, 1);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }

    // push three events, then flush
    {
        EventBuilder eventBuilder(cfg);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[1]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[2]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            switch (dataCallbackCount++)
            {
                case 0:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 101);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 150);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 200);
                    } break;
                case 1:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 225);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 151);

                        ASSERT_EQ(moduleDataList[2].data.size, 0);
                    } break;
#if 0
                case 2:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 0);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 252);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 350);
                    } break;
#endif
            }
        };

        auto systemEventCallback = [&] (void *, int crateIndex, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 2);

        ASSERT_EQ(dataCallbackCount, 2);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }
}

TEST(event_builder, SingleCrateWindowMatchingOverflow)
{
    // Storage for the module data
    std::vector<std::array<std::vector<u32>, TestSetupModuleCount>> moduleTestData =
    {
        {
            {
                { 100 }, // crate0, module0         in window
                { 150 }, // crate0, module1 (**)
                { 200 }, // crate0, module2         in window
            }
        },
        {
            {
                { 0x3fffffff -  5 },    // crate0, module0         in window, not yet overflowed
                {  10 },                // crate0, module1 (**)    overflow from 150 to 10
                { 0x3fffffff -  10 },   // crate0, module2         in window, not yet overflowed
            }
        },
        {
            {
                {  50 },                // crate0, module0         in window, overflow
                { 0x3fffffff - 15 },    // crate0, module1 (**)    no overflow this time
                { 100 },                // crate0, module2         in window, overflow
            }
        },
        {
            {
                { 2000 },                // crate0, module0         not in window
                { 0x3fffffff - 17 },    // crate0, module1 (**)
                { 3000 },                // crate0, module2         not in window
            }
        },
    };

    int crateIndex = 0;
    int eventIndex = 0;
    EventBuilderConfig cfg;
    cfg.setups = std::vector<EventSetup>{ make_one_crate_one_event_test_setup() };

    // push three events, then flush
    {
        EventBuilder eventBuilder(cfg);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[1]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[2]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[3]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            switch (dataCallbackCount++)
            {
                case 0:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 100);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 150);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 200);
                    } break;
                case 1:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 0x3fffffff - 5);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 10);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 0x3fffffff - 10);
                    } break;
                case 2:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 1);
                        ASSERT_EQ(moduleDataList[0].data.data[0], 50);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 0x3fffffff - 15);

                        ASSERT_EQ(moduleDataList[2].data.size, 1);
                        ASSERT_EQ(moduleDataList[2].data.data[0], 100);
                    } break;
                case 3:
                    {
                        ASSERT_EQ(moduleDataList[0].data.size, 0);

                        ASSERT_EQ(moduleDataList[1].data.size, 1);
                        ASSERT_EQ(moduleDataList[1].data.data[0], 0x3fffffff - 17);

                        ASSERT_EQ(moduleDataList[2].data.size, 0);
                    } break;
            }
        };

        auto systemEventCallback = [&] (void *, int crateIndex, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 4);

        ASSERT_EQ(dataCallbackCount, 4);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }
}

TEST(event_builder, SingleCratePerfectMatches)
{
    int crateIndex = 0;
    int eventIndex = 0;
    EventBuilderConfig cfg;
    cfg.setups = std::vector<EventSetup>{ make_one_crate_one_event_test_setup() };
    EventBuilder eventBuilder(cfg);

    // Storage for a single event (3 modules)
    std::array<std::array<u32, 1>, TestSetupModuleCount> eventStorage = {};

    // Callback compatible ModuleData array pointing into the eventStorage storage.
    std::array<ModuleData, TestSetupModuleCount> eventData =
    {
        ModuleData { .data = { eventStorage[0].data(), 1 } },
        ModuleData { .data = { eventStorage[1].data(), 1 } },
        ModuleData { .data = { eventStorage[2].data(), 1 } },
    };

    u32 ts = 0;

    // Push 999 events using the event number as the timestamp value for all modules.
    for (ts = 0; ts < 999; ++ts)
    {
        for (auto &moduleData: eventStorage)
            moduleData[0] = ts;

        eventBuilder.recordEventData(crateIndex, eventIndex, eventData.data(), eventData.size());
    }

    size_t dataCallbackCount = 0;
    size_t systemCallbackCount = 0;

    auto eventDataCallback = [&] (void *, int crateIndex, int eventIndex, const ModuleData *eventData, unsigned moduleCount)
    {
        ++dataCallbackCount;
    };

    auto systemEventCallback = [&] (void *, int crateIndex, const u32 *header, u32 size)
    {
        ++systemCallbackCount;
    };

    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, false), 999);;
    ASSERT_EQ(dataCallbackCount, 999);
    ASSERT_EQ(systemCallbackCount, 0);

    // Push one more event to reach 1000 buffered
    {
        for (auto &moduleData: eventStorage)
            moduleData[0] = ts;

        eventBuilder.recordEventData(crateIndex, eventIndex, eventData.data(), eventData.size());
    }

    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, false), 1);;
    ASSERT_EQ(dataCallbackCount, 1000);
    ASSERT_EQ(systemCallbackCount, 0);

    // No change, cannot build
    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, false), 0);;
    ASSERT_EQ(dataCallbackCount, 1000);
    ASSERT_EQ(systemCallbackCount, 0);

    // Flush but no more data left
    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 0);;
    ASSERT_EQ(dataCallbackCount, 1000);
    ASSERT_EQ(systemCallbackCount, 0);

    // Push one more event to reach 1 buffered
    {
        for (auto &moduleData: eventStorage)
            moduleData[0] = ts;

        eventBuilder.recordEventData(crateIndex, eventIndex, eventData.data(), eventData.size());
    }

    // Can build the single event
    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, false), 1);;
    ASSERT_EQ(dataCallbackCount, 1001);
    ASSERT_EQ(systemCallbackCount, 0);

    // Flush
    ASSERT_EQ(eventBuilder.buildEvents({ eventDataCallback, systemEventCallback }, true), 0);;
    ASSERT_EQ(dataCallbackCount, 1001);
    ASSERT_EQ(systemCallbackCount, 0);
}
