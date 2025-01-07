#include <gtest/gtest.h>
#include "event_builder2.hpp"

using namespace mesytec::mvlc::event_builder;

TEST(EventBuilder2, recordModuleData)
{
    EventConfig eventConfig;
    ModuleConfig moduleConfig;
    moduleConfig.tsExtractor = make_mesytec_default_timestamp_extractor();
    EventBuilderConfig cfg;
    EventBuilder2 eventbuilder(cfg, {});
}
