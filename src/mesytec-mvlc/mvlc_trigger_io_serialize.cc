#include "mvlc_trigger_io_serialize.h"

#include <boost/range/adaptor/indexed.hpp>
#include <map>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/emittermanip.h>

#include "util/fmt.h"

using boost::adaptors::indexed;

namespace mesytec::mvlc::trigger_io
{

namespace
{

static const u16 StrobeGGIOOffset = 0x32u;

BasicPart select_unit(int level, int unit, const std::string &unitName = {})
{
    auto ret = RegisterWrite{ 0x0200,  static_cast<u16>(((level << 8) | unit)), RegisterWrite::Opt_HexValue };

#if 1
    ret.comment = fmt::format("select L{}.Unit{}", level, unit);

    if (!unitName.empty())
        ret.comment += unitName;
#endif

    return ret;
};

// Note: the desired unit must be selected prior to calling this function.
BasicPart write_unit_reg(u16 reg, u16 value, const std::string &comment, unsigned writeOpts = 0u)
{
    auto ret = RegisterWrite { static_cast<u16>(0x0300u + reg), value, comment, writeOpts };

    return ret;
}

// Note: the desired unit must be selected prior to calling this function.
BasicPart write_connection(u16 offset, u16 value, const std::string &sourceName = {})
{
    auto ret = RegisterWrite { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.empty())
        ret.comment = fmt::format("connect input{} to '{}'", offset / 2, sourceName);

    return ret;
}

BasicPart write_strobe_connection(u16 offset, u16 value, const std::string &sourceName = {})
{
    auto ret = RegisterWrite { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.empty())
        ret.comment = fmt::format("connect strobe_input to '{}'", sourceName);

    return ret;
}

BasicParts generate(const trigger_io::Timer &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret.push_back(write_unit_reg(2, static_cast<u16>(unit.range), "range (0:ns, 1:us, 2:ms, 3:s)"));
    ret.push_back(write_unit_reg(4, unit.delay_ns, "delay [ns]"));
    ret.push_back(write_unit_reg(6, unit.period, "period [in range units]"));
    return ret;
}

namespace io_flags
{
    using Flags = u8;
    static const Flags HasDirection    = 1u << 0;
    static const Flags HasActivation   = 1u << 1;

    static const Flags None             = 0u;
    static const Flags NIM_IO_Flags     = HasDirection | HasActivation;
    static const Flags ECL_IO_Flags     = HasActivation;
}

/* The IO structure is used for different units sharing IO properties:
 * NIM I/Os, ECL Outputs, slave triggers, and strobe gate generators.
 * The activation and direction registers are only written out if the
 * respective io_flags bit is set. The offset value is added to all base
 * register addresses.
 */
BasicParts generate(const trigger_io::IO &io, const io_flags::Flags &ioFlags, u16 offset = 0)
{
    BasicParts ret;

    ret.push_back(write_unit_reg(offset + 0, io.delay, "delay [ns]"));
    ret.push_back(write_unit_reg(offset + 2, io.width, "width [ns]"));
    ret.push_back(write_unit_reg(offset + 4, io.holdoff, "holdoff [ns]"));
    ret.push_back(write_unit_reg(offset + 6, static_cast<u16>(io.invert),
                          "invert (start on trailing edge of input)"));

    if (ioFlags & io_flags::HasDirection)
        ret.push_back(write_unit_reg(offset + 10, static_cast<u16>(io.direction), "direction (0:in, 1:out)"));

    if (ioFlags & io_flags::HasActivation)
        ret.push_back(write_unit_reg(offset + 16, static_cast<u16>(io.activate), "output activate"));

    return ret;
}

BasicParts generate(const trigger_io::TriggerResource &unit, int /*index*/)
{
    BasicParts ret;

    ret.push_back(write_unit_reg(0x80u, static_cast<u16>(unit.type),
                          "type: 0=IRQ, 1=SoftTrigger, 2=SyncTrigger"));

    ret.push_back(write_unit_reg(0, static_cast<u16>(unit.irqUtil.irqIndex),
                          "irq_index (zero-based: 0: IRQ1, .., 6: IRQ7)"));

    auto ggParts = generate(unit.syncOutTrigger.gateGenerator, io_flags::None, 6);

    for (auto &part: ggParts)
    {
        if (auto write = std::get_if<RegisterWrite>(&part))
        {
            write->comment = "slave_trigger: " + write->comment;
        }
    }

    std::copy(std::begin(ggParts), std::end(ggParts), std::back_inserter(ret));

    ret.push_back(write_unit_reg(0x82u, unit.syncOutTrigger.triggerIndex,
                          "slave trigger number (0..3)"));

    return ret;
}

BasicParts generate(const trigger_io::StackBusy &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret.push_back(write_unit_reg(0, unit.stackIndex, "stack_index"));
    return ret;
}

trigger_io::LUT_RAM make_lut_ram(const LUT &lut)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        unsigned ramValue = 0u;

        // Combine the three separate output entries into a single value
        // suitable for the MVLC LUT RAM.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            if (lut.lutContents[output].test(address))
            {
                ramValue |= 1u << output;
                assert(ramValue < (1u << trigger_io::LUT::OutputBits));
            }
        }

        trigger_io::set(ram, address, ramValue);
    }

    return ram;
}

BasicParts write_lut_ram(const trigger_io::LUT_RAM &ram)
{
    BasicParts ret;

    for (const auto &kv: ram | indexed(0))
    {
        u16 reg = kv.index() * sizeof(u16); // register address increment is 2 bytes
        u16 cell = reg * 2;
        auto comment = fmt::format("cells {}-{}", cell, cell + 3);
        ret.push_back(write_unit_reg(reg, kv.value(), comment, RegisterWrite::Opt_HexValue));
    }

    return ret;
}

BasicParts write_lut(const LUT &lut)
{
    return write_lut_ram(make_lut_ram(lut));
}

BasicParts generate(const trigger_io::StackStart &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret.push_back(write_unit_reg(0, static_cast<u16>(unit.activate), "activate"));
    ret.push_back(write_unit_reg(2, unit.stackIndex, "stack index"));
    ret.push_back(write_unit_reg(4, unit.delay_ns, "delay [ns]"));
    return ret;
}

BasicParts generate(const trigger_io::MasterTrigger &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret.push_back(write_unit_reg(0, static_cast<u16>(unit.activate), "activate"));
    return ret;
}

BasicParts generate(const trigger_io::Counter &unit, int index)
{
    (void) index;

    BasicParts ret;
    ret.push_back(write_unit_reg(14, static_cast<u16>(unit.clearOnLatch), "clear on latch"));
    return ret;
}

} // end anon namespace

ScriptParts generate_trigger_io_parts(const TriggerIO &ioCfg)
{
    ScriptParts ret;

    //
    // Level0
    //

    ret.push_back("Level0 #####################################################");

    for (const auto &kv: ioCfg.l0.timers | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index()];
        ub += select_unit(0, kv.index());
        ub += generate(kv.value(), kv.index());
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l0.triggerResources | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.TriggerResourceOffset];
        ub += select_unit(0, kv.index() + ioCfg.l0.TriggerResourceOffset);
        ub += generate(kv.value(), kv.index());
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.StackBusyOffset];
        ub += select_unit(0, kv.index() + ioCfg.l0.StackBusyOffset);
        ub += generate(kv.value(), kv.index());
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.NIM_IO_Offset];
        ub += select_unit(0, kv.index() + ioCfg.l0.NIM_IO_Offset);
        ub += generate(kv.value(), io_flags::NIM_IO_Flags);
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
    {
        UnitBlock ub;
        ub.comment = ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.IRQ_Inputs_Offset];
        ub += select_unit(0, kv.index() + ioCfg.l0.IRQ_Inputs_Offset);
        ub += generate(kv.value(), io_flags::None);
        ret.push_back(ub);
    }

    //
    // Level1
    //

    ret.push_back("Level1 #####################################################");

    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        UnitBlock ub;
        ub.comment = fmt::format("L1.LUT{}", unitIndex);
        ub += select_unit(1, unitIndex);
        ub += write_lut(kv.value());

        if (unitIndex == 2)
        {
            const auto &inputChoices = Level1::LUT2DynamicInputChoices;

            for (size_t input = 0; input < LUT_DynamicInputCount; ++input)
            {
                unsigned conValue = ioCfg.l1.lut2Connections[input];
                UnitAddress conAddress = inputChoices[input][conValue];
                u16 regOffset = input * 2;

                ub += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
            }
        }

        ret.push_back(ub);
    }

    //
    // Level2
    //

    ret.push_back("Level2 #####################################################");

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        UnitBlock ub;
        ub.comment = fmt::format("L2.LUT{}", unitIndex);
        ub += select_unit(2, unitIndex);
        ub += write_lut(kv.value());
        ub += write_unit_reg(0x20, kv.value().strobedOutputs.to_ulong(),
                              "strobed_outputs", RegisterWrite::Opt_BinValue);

        const auto &l2InputChoices = Level2::DynamicInputChoices[unitIndex];

        for (size_t input = 0; input < LUT_DynamicInputCount; input++)
        {
            unsigned conValue = ioCfg.l2.lutConnections[unitIndex][input];
            UnitAddress conAddress = l2InputChoices.lutChoices[input][conValue];
            u16 regOffset = input * 2;

            ub += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        // strobe GG
        ub += fmt::format("L2.LUT{} strobe gate generator", unitIndex);
        ub += generate(kv.value().strobeGG, io_flags::None, StrobeGGIOOffset);

        // strobe_input
        {
            unsigned conValue = ioCfg.l2.strobeConnections[unitIndex];
            UnitAddress conAddress = l2InputChoices.strobeChoices[conValue];
            u16 regOffset = 6;

            ub += write_strobe_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        ret.push_back(ub);
    }

    //
    // Level3
    //

    ret.push_back("Level3 #####################################################");

    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        unsigned unitIndex = kv.index();

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.MasterTriggersOffset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.CountersOffset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), unitIndex);

        // counter input
        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));

        // latch input
        conValue = ioCfg.l3.connections[unitIndex][1];
        conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][1][conValue];

        ub += write_connection(2, conValue, lookup_name(ioCfg, conAddress));
        ret.push_back(ub);
    }

    // Level3 NIM connections
    ret.push_back("NIM unit connections (Note: NIM setup is done in the Level0 section)");
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret.push_back(ub);
    }

    for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.ECL_Unit_Offset;

        UnitBlock ub;
        ub.comment = ioCfg.l3.DefaultUnitNames[unitIndex];
        ub += select_unit(3, unitIndex);
        ub += generate(kv.value(), io_flags::ECL_IO_Flags);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ub += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
        ret.push_back(ub);
    }

    return ret;
}

std::string generate_meta_info_yaml(const TriggerIO &ioCfg)
{
    // unit number -> unit name
    using NameMap = std::map<unsigned, std::string>;

    YAML::Emitter out;
    assert(out.good()); // initial consistency check
    out << YAML::BeginMap;
    out << YAML::Key << "names" << YAML::Value << YAML::BeginMap;

    // Level0 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l0.DefaultUnitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l0.unitNames.at(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            m[unitIndex] = unitName;
        }

        if (!m.empty())
            out << YAML::Key << "level0" << YAML::Value << m;
    }

    // Level1 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();

                m[outputIndex] = outputName;
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level1" << YAML::Value << lutMaps;
    }

    // Level2 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();

                m[outputIndex] = outputName;
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level2" << YAML::Value << lutMaps;
    }

    // Level3 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l3.DefaultUnitNames | indexed(0))
        {
            const size_t unitIndex = kv.index();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex
                && unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l3.unitNames.at(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            m[unitIndex] = unitName;
        }

        if (!m.empty())
            out << YAML::Key << "level3" << YAML::Value << m;
    }

    out << YAML::EndMap;
    assert(out.good()); // consistency check

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    out << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;

    {
        out << YAML::Key << "level0" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    {
        out << YAML::Key << "level3" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    out << YAML::EndMap;
    assert(out.good()); // consistency check

    return out.c_str();
}

}
