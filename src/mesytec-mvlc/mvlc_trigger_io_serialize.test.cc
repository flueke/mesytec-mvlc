#include <boost/range/adaptor/indexed.hpp>
#include <gtest/gtest.h>

#include "mvlc_trigger_io_serialize_vmescript.h"
#include "mvlc_trigger_io_serialize_mvlcscript.h"
#include "mvlc_readout_config.h"
#include "util/fmt.h"

using namespace mesytec::mvlc;
using boost::adaptors::indexed;

static const char *ReferenceVMEScript =
#include "data/trigger_io_vme_script_reference_file.vmescript"
    ;

// Some tests compare against the reference file from mvme. This is fine but
// does not check if modifications really carry through.
//
// Other tests first generate a StackCommandBuilder from the TriggerIO, then do
// a serialize loop, create a 2nd builder from the deserialized result and
// compare the two builders. This was done because builder comparsion was
// already implemented. These should be fine.
//
// What's missing is more code that modifies parameters, then checks that the
// values really survived the serialization round-trip. TODO: add more test

// Simple one way test from TriggerIO to VMEScript text.
TEST(TriggerIOSerialize, SerializeToVMEScript)
{
    auto generated = trigger_io::generate_trigger_io_vmescript(trigger_io::TriggerIO{});

    ASSERT_EQ(ReferenceVMEScript, generated);
}

TEST(TriggerIOSerialize, DeserializeFromVMEScript)
{
    // Create a default trigger io config and modify some values.
    trigger_io::TriggerIO ioCfg;

    for (auto &unitName: ioCfg.l0.unitNames)
    {
        unitName += "_foobar";
    }

    for (auto &lut: ioCfg.l1.luts)
    {
        for (auto &outputName: lut.outputNames)
        {
            outputName += "_baz";
        }
    }

    for (auto &lut: ioCfg.l1.luts)
    {
        for (auto &outputName: lut.outputNames)
        {
            outputName += "_froblwobble";
        }
    }

    // Change only the non-NIM names in L3. The NIMs are shared with L0 and those names dominate.
    for (const auto &kv: ioCfg.l3.unitNames | indexed(0))
    {
        int index = kv.index(); // force conversion to signed here

        if (index < trigger_io::Level3::NIM_IO_Unit_Offset ||
            index >=
                static_cast<int>(trigger_io::Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count))
        {
            auto &unitName = kv.value();
            unitName += "_bazi_bua";
        }

    }

    ioCfg.l0.timers[0].softActivate = true;
    ioCfg.l0.timers[0].range = trigger_io::Timer::Range::ms;
    ioCfg.l0.timers[0].period = 1000;
    ioCfg.l0.timers[0].delay_ns = 100;

    ioCfg.l0.timers[1].softActivate = true;
    ioCfg.l0.timers[1].range = trigger_io::Timer::Range::s;
    ioCfg.l0.timers[1].period = 2000;
    ioCfg.l0.timers[1].delay_ns = 200;

    // Serialize and parse back.
    auto generated = trigger_io::generate_trigger_io_vmescript(ioCfg);
    auto parsed = trigger_io::parse_trigger_io_vmescript(generated);

    // Check that the modifications survived the round trip.
    ASSERT_EQ(ioCfg.l0.timers[0].softActivate, parsed.l0.timers[0].softActivate);
    ASSERT_EQ(ioCfg.l0.timers[0].range, parsed.l0.timers[0].range);
    ASSERT_EQ(ioCfg.l0.timers[0].period, parsed.l0.timers[0].period);
    ASSERT_EQ(ioCfg.l0.timers[0].delay_ns, parsed.l0.timers[0].delay_ns);

    ASSERT_EQ(ioCfg.l0.timers[1].softActivate, parsed.l0.timers[1].softActivate);
    ASSERT_EQ(ioCfg.l0.timers[1].range, parsed.l0.timers[1].range);
    ASSERT_EQ(ioCfg.l0.timers[1].period, parsed.l0.timers[1].period);
    ASSERT_EQ(ioCfg.l0.timers[1].delay_ns, parsed.l0.timers[1].delay_ns);

    ASSERT_EQ(ioCfg.l0.unitNames, parsed.l0.unitNames);

    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        const auto &lutIndex = kv.index();
        const auto &lut = kv.value();

        ASSERT_EQ(lut.outputNames, parsed.l1.luts[lutIndex].outputNames);
    }

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        const auto &lutIndex = kv.index();
        const auto &lut = kv.value();

        ASSERT_EQ(lut.outputNames, parsed.l2.luts[lutIndex].outputNames);
    }

    for (const auto &kv: ioCfg.l3.unitNames | indexed(0))
    {
        int index = kv.index(); // force conversion to signed here

        if (index < trigger_io::Level3::NIM_IO_Unit_Offset ||
            index >=
                static_cast<int>(trigger_io::Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count))
        {
            const auto &unitName = kv.value();
            ASSERT_EQ(unitName, parsed.l3.unitNames.at(index));
        }
        else
        {
            // NIM names are shared with L0
            ASSERT_EQ(parsed.l3.unitNames.at(index), parsed.l0.unitNames.at(index));
        }
    }
}

TEST(TriggerIOSerialize, SerializeToStackCommandBuilder)
{
    auto cmdBuilder = trigger_io::generate_trigger_io_command_builder(trigger_io::TriggerIO{});
    auto parsed = trigger_io::parse_trigger_io_command_builder(cmdBuilder);
    auto cmdBuilder2 = trigger_io::generate_trigger_io_command_builder(parsed);

    ASSERT_EQ(cmdBuilder, cmdBuilder2);
}

TEST(TriggerIOSerialize, SerializeToMvlcScript)
{
    trigger_io::TriggerIO triggerIo;
    auto cmdBuilder = trigger_io::generate_trigger_io_command_builder(triggerIo);
    auto mvlcScript = trigger_io::generate_trigger_io_mvlcscript(triggerIo);
    auto parsed = trigger_io::parse_trigger_io_mvlcscript(mvlcScript);
    auto cmdBuilder2 = trigger_io::generate_trigger_io_command_builder(parsed);

    ASSERT_EQ(cmdBuilder, cmdBuilder2);
}
