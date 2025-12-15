#include "mvlc_trigger_io_deserialize.h"

#include <boost/range/adaptor/indexed.hpp>
#include <yaml-cpp/yaml.h>

using boost::adaptors::indexed;

namespace mesytec::mvlc::trigger_io
{

TriggerIO deserialize_trigger_io(const std::vector<WriteCommand> &writes, const std::string &metaYaml)
{
    auto levelWrites = level_writes_from_flat_writes(writes);
    return deserialize_trigger_io(levelWrites, metaYaml);
}

LevelWrites level_writes_from_flat_writes(const std::vector<WriteCommand> &writes)
{
    LevelWrites levelWrites;
    u16 level = 0;
    u16 unit  = 0;

    for (const auto &cmd: writes)
    {
        if (cmd.address == UnitSelectRegister)
        {
            level = (cmd.value >> 8) & 0xff;
            unit  = cmd.value & 0xff;
        }
        else if (cmd.address >= UnitRegisterBase)
        {
            u16 regOffset = cmd.address - UnitRegisterBase;
            levelWrites[level][unit][regOffset] = cmd.value;
        }
    }
    return levelWrites;
}

IO parse_io(const RegisterWrites &writes, u16 offset)
{
    trigger_io::IO io = {};

    try
    {

        io.delay = writes.at(offset + 0);
        io.width = writes.at(offset + 2);
        io.holdoff = writes.at(offset + 4);
        io.invert = static_cast<bool>(writes.at(offset + 6));
        io.direction = static_cast<trigger_io::IO::Direction>(writes.at(offset + 10));
        io.activate = static_cast<bool>(writes.at(offset + 16));
    }
    catch (const std::out_of_range &)
    {
    }

    return io;
}

LUT_RAM parse_lut_ram(const RegisterWrites &writes)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t line = 0; line < ram.size(); line++)
    {
        u16 regAddress = line * 2;
        try
        {
            ram[line] = writes.at(regAddress);
        }
        catch (const std::out_of_range &)
        {
        }
    }

    return ram;
}

LUT parse_lut(const RegisterWrites &writes)
{
    auto ram = parse_lut_ram(writes);

    LUT lut = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        u8 ramValue = trigger_io::lookup(ram, address);

        // Distribute the 3 output bits stored in a single RAM cell to the 3
        // output arrays in lut.lutContents.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            lut.lutContents[output][address] = (ramValue >> output) & 0b1;
        }
    }

    try
    {
        lut.strobedOutputs = writes.at(0x20);
    }
    catch (const std::out_of_range &)
    {
    }

    lut.strobeGG = parse_io(writes, LUT::StrobeGGIOOffset);

    // Force the width to 8 if it was set to 0 or not specified.
    if (lut.strobeGG.width == 0)
        lut.strobeGG.width = LUT::StrobeGGDefaultWidth;

    return lut;
}

void apply_meta_info_yaml(TriggerIO &ioCfg, const std::string &metaYaml)
{
    YAML::Node yRoot = YAML::Load(metaYaml);

    if (!yRoot || !yRoot["names"])
        return;

    //
    // Names
    //
    const auto &yLevelNames = yRoot["names"];

    // Level0 - flat list of unitnames
    if (const auto &yNames = yLevelNames["level0"])
    {
        for (const auto &kv: ioCfg.l0.unitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &unitName = kv.value();

            if (yNames[unitIndex])
                unitName = yNames[unitIndex].as<std::string>();

            // Copy NIM_IO names to the level3 structure.
            if (Level3::NIM_IO_Unit_Offset <= static_cast<unsigned>(unitIndex) &&
                static_cast<unsigned>(unitIndex) <
                    (Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count))
            {
                ioCfg.l3.unitNames[unitIndex] = unitName;
            }
        }
    }

    // Level1 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level1"])
    {
        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = yNames[outputIndex].as<std::string>();
                }
            }
        }
    }

    // Level2 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level2"])
    {
        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = yNames[outputIndex].as<std::string>();
                }
            }
        }
    }

    // Level3 - flat list of unitnames. NIM ios shared with L0
    if (const auto &yNames = yLevelNames["level3"])
    {
        for (const auto &kv: ioCfg.l3.unitNames | indexed(0))
        {
            const size_t &unitIndex = kv.index();
            auto &unitName = kv.value();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex &&
                unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            if (yNames[unitIndex])
                unitName = yNames[unitIndex].as<std::string>();
        }
    }

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    if (!yRoot["settings"])
        return;

    const auto &ySettings = yRoot["settings"];

    if (const auto &yLevelSettings = ySettings["level0"])
    {
        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {
            }
        }
    }

    if (const auto &yLevelSettings = ySettings["level3"])
    {
        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();
            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {
            }
        }
    }
}

inline TriggerIO trigger_io_from_writes(const LevelWrites &levelWrites)
{
    TriggerIO ioCfg;

    try
    {

        // level0
        {
            auto &writes = levelWrites[0];

            for (const auto &kv: ioCfg.l0.timers | indexed(0))
            {
                unsigned unitIndex = kv.index();
                auto &unit = kv.value();

                unit.range = static_cast<trigger_io::Timer::Range>(writes.at(unitIndex).at(2));
                unit.delay_ns = writes.at(unitIndex).at(4);
                unit.period = writes.at(unitIndex).at(6);
            }

            for (const auto &kv: ioCfg.l0.triggerResources | indexed(0))
            {
                unsigned unitIndex = kv.index() + Level0::TriggerResourceOffset;
                auto &unit = kv.value();

                unit.type = static_cast<TriggerResource::Type>(writes.at(unitIndex).at(0x80u));
                unit.irqUtil.irqIndex = writes.at(unitIndex).at(0);
                unit.syncOutTrigger.gateGenerator = parse_io(writes.at(unitIndex), 6);
                unit.syncOutTrigger.triggerIndex = writes.at(unitIndex).at(0x82u);
            }

            for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
            {

                unsigned unitIndex = kv.index() + Level0::StackBusyOffset;
                auto &unit = kv.value();

                unit.stackIndex = writes.at(unitIndex).at(0);
            }

            for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
            {

                unsigned unitIndex = kv.index() + Level0::NIM_IO_Offset;
                auto &unit = kv.value();

                unit = parse_io(writes.at(unitIndex));
            }

            for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
            {

                unsigned unitIndex = kv.index() + Level0::IRQ_Inputs_Offset;
                auto &unit = kv.value();

                unit = parse_io(writes.at(unitIndex));
            }
        }

        // level1
        {
            auto &writes = levelWrites[1];

            for (const auto &kv: ioCfg.l1.luts | indexed(0))
            {
                unsigned unitIndex = kv.index();
                auto &unit = kv.value();
                unit = parse_lut(writes.at(unitIndex));

                if (unitIndex == 2)
                {
                    // dynamic input connections
                    for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
                    {
                        ioCfg.l1.lut2Connections[input] =
                            writes.at(unitIndex).at(UnitConnectBase + 2 * input);
                    }
                }
            }
        }

        // level2
        {
            auto &writes = levelWrites[2];

            for (const auto &kv: ioCfg.l2.luts | indexed(0))
            {
                unsigned unitIndex = kv.index();
                auto &unit = kv.value();
                // This parses the LUT and the strobe GG settings
                unit = parse_lut(writes.at(unitIndex));

                // dynamic input connections
                for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
                {
                    ioCfg.l2.lutConnections[unitIndex][input] =
                        writes.at(unitIndex).at(UnitConnectBase + 2 * input);
                }

                // strobe GG connection
                ioCfg.l2.strobeConnections[unitIndex] =
                    writes.at(unitIndex).at(UnitConnectBase + 6);
            }
        }

        // level3
        {
            // Copy NIM settings parsed from level0 data to the level3 NIM
            // structures.
            std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), ioCfg.l3.ioNIM.begin());

            auto &writes = levelWrites[3];

            for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
            {
                unsigned unitIndex = kv.index();
                auto &unit = kv.value();

                unit.activate = static_cast<bool>(writes.at(unitIndex).at(0));
                unit.stackIndex = writes.at(unitIndex).at(2);
                unit.delay_ns = writes.at(unitIndex).at(4);

                ioCfg.l3.connections[unitIndex] = {writes.at(unitIndex).at(0x80)};
            }

            for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
            {
                unsigned unitIndex = kv.index() + Level3::MasterTriggersOffset;
                auto &unit = kv.value();

                unit.activate = static_cast<bool>(writes.at(unitIndex).at(0));

                ioCfg.l3.connections[unitIndex] = {writes.at(unitIndex).at(0x80)};
            }

            for (const auto &kv: ioCfg.l3.counters | indexed(0))
            {
                unsigned unitIndex = kv.index() + Level3::CountersOffset;
                auto &unit = kv.value();

                unit.clearOnLatch = static_cast<bool>(writes.at(unitIndex).at(14));

                ioCfg.l3.connections[unitIndex] = {writes.at(unitIndex).at(0x80),
                                                   writes.at(unitIndex).at(0x82)};
            }

            for (const auto &kv: ioCfg.l3.ioNIM | indexed(0))
            {
                // level3 NIM connections (setup is done in level0)
                unsigned unitIndex = kv.index() + Level3::NIM_IO_Unit_Offset;

                ioCfg.l3.connections[unitIndex] = {writes.at(unitIndex).at(0x80)};
            }

            for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
            {
                unsigned unitIndex = kv.index() + Level3::ECL_Unit_Offset;
                auto &unit = kv.value();

                unit = parse_io(writes.at(unitIndex));

                ioCfg.l3.connections[unitIndex] = {writes.at(unitIndex).at(0x80)};
            }
        }
    }
    catch (const std::out_of_range &)
    {
    }

    return ioCfg;
}

TriggerIO deserialize_trigger_io(const LevelWrites &levelWrites, const std::string &metaYaml)
{
    auto ret = trigger_io_from_writes(levelWrites);
    apply_meta_info_yaml(ret, metaYaml);
    return ret;
}

} // namespace mesytec::mvlc::trigger_io
