#include "mvlc_readout_config.h"

#include <cassert>
#include <fstream>

#include "util/fmt.h"
#include "util/string_util.h"
#include "util/yaml_json.h"
#include "util/yaml_json_internal.h"

namespace mesytec
{
namespace mvlc
{

namespace
{
std::string to_string(ConnectionType ct)
{
    switch (ct)
    {
        case ConnectionType::USB:
            return listfile::get_filemagic_usb();

        case ConnectionType::ETH:
            return listfile::get_filemagic_eth();
    }

    return {};
}

ConnectionType connection_type_from_string(const std::string &str)
{
    if (str == listfile::get_filemagic_usb())
        return ConnectionType::USB;

    if (str == listfile::get_filemagic_eth())
        return ConnectionType::ETH;

    throw std::runtime_error("invalid connection type: " + str);
}

} // end anon namespace

bool CrateConfig::operator==(const CrateConfig &o) const
{
    return crateId == o.crateId
        && connectionType == o.connectionType
        && usbIndex == o.usbIndex
        && usbSerial == o.usbSerial
        && ethHost == o.ethHost
        && stacks == o.stacks
        && triggers == o.triggers
        && initRegisters == o.initRegisters
        && initTriggerIO == o.initTriggerIO
        && initCommands == o.initCommands
        && stopCommands == o.stopCommands
        && mcstDaqStart == o.mcstDaqStart
        && mcstDaqStop == o.mcstDaqStop
        ;
}

namespace
{

YAML::Emitter &operator<<(YAML::Emitter &out, const StackCommandBuilder &stack)
{
    out << YAML::BeginMap;

    out << YAML::Key << "name" << YAML::Value << stack.getName();

    out << YAML::Key << "groups" << YAML::BeginSeq;

    for (const auto &group: stack.getGroups())
    {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << group.name;

        out << YAML::Key << "contents" << YAML::Value;

        out << YAML::BeginSeq;
        for (const auto &cmd: group.commands)
            out << to_string(cmd);
        out << YAML::EndSeq;

        out << YAML::Key << "meta" << YAML::Value << group.meta;

        out << YAML::EndMap;
    }

    out << YAML::EndSeq; // groups

    out << YAML::EndMap;

    return out;
}

StackCommandBuilder stack_command_builder_from_yaml(const YAML::Node &yStack)
{
    StackCommandBuilder stack;

    stack.setName(yStack["name"].as<std::string>());

    if (const auto &yGroups = yStack["groups"])
    {
        for (const auto &yGroup: yGroups)
        {
            std::string groupName = yGroup["name"].as<std::string>();

            std::vector<StackCommand> groupCommands;

            for (const auto &yCmd: yGroup["contents"])
                groupCommands.emplace_back(stack_command_from_string(yCmd.as<std::string>()));


            std::map<std::string, std::string> moduleMeta;

            if (const auto &yMeta = yGroup["meta"])
                moduleMeta = yMeta.as<std::map<std::string, std::string>>();

            stack.addGroup(groupName, groupCommands, moduleMeta);
        }
    }

    return stack;
}

YAML::Emitter &operator<<(YAML::Emitter &out, const CrateConfig &crateConfig)
{
    assert(out.good());

    out << YAML::Hex;

    out << YAML::BeginMap;
    out << YAML::Key << "crate" << YAML::Value << YAML::BeginMap;

    out << YAML::Key << "crateId" << YAML::Value << crateConfig.crateId;
    out << YAML::Key << "mvlc_connection" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "type" << YAML::Value << to_string(crateConfig.connectionType);
    out << YAML::Key << "usbIndex" << YAML::Value << std::to_string(crateConfig.usbIndex);
    out << YAML::Key << "usbSerial" << YAML::Value << crateConfig.usbSerial;
    out << YAML::Key << "ethHost" << YAML::Value << crateConfig.ethHost;
    out << YAML::Key << "ethJumboEnable" << YAML::Value << crateConfig.ethJumboEnable;
    out << YAML::EndMap; // end mvlc_connection

    out << YAML::Key << "readout_stacks" << YAML::Value << YAML::BeginSeq;

    for (const auto &stack: crateConfig.stacks)
        out << stack;

    out << YAML::EndSeq; // end readout_stacks

    out << YAML::Key << "stack_triggers" << YAML::Value << crateConfig.triggers;

    out << YAML::Key << "init_registers" << YAML::Value << YAML::BeginMap;
    for (const auto &regWrite: crateConfig.initRegisters)
    {
        out << YAML::Key << fmt::format("0x{:04x}", regWrite.first)
            << YAML::Value << fmt::format("0x{:08x}", regWrite.second);
    }
    out << YAML::EndMap; // end init_registers

    out << YAML::Key << "init_trigger_io" << YAML::Value << crateConfig.initTriggerIO;
    out << YAML::Key << "init_commands" << YAML::Value << crateConfig.initCommands;
    out << YAML::Key << "stop_commands" << YAML::Value << crateConfig.stopCommands;
    out << YAML::Key << "mcst_daq_start" << YAML::Value << crateConfig.mcstDaqStart;
    out << YAML::Key << "mcst_daq_stop" << YAML::Value << crateConfig.mcstDaqStop;

    out << YAML::EndMap; // end crate

    assert(out.good());

    return out;
}

} // end anon namespace

inline YAML::Emitter & to_yaml(YAML::Emitter &out, const CrateConfig &crateConfig)
{
    out << YAML::Hex;
    assert(out.good());
    out << crateConfig;
    assert(out.good());
    return out;
}

/// Parses a CreateConfig from the given YAML input string.
/// throws std::runtime_error on error.
CrateConfig crate_config_from_yaml(const std::string &yamlText)
{
    std::istringstream iss(yamlText);
    return crate_config_from_yaml(iss);
}

/// Parses a CreateConfig from the given YAML input stream.
/// throws std::runtime_error on error.
CrateConfig crate_config_from_yaml(const YAML::Node &yRoot)
{
    CrateConfig result = {};

    if (!yRoot)
        throw std::runtime_error("CrateConfig YAML data is empty");

    if (!yRoot["crate"])
        throw std::runtime_error("No 'crate' node found in YAML input");

    if (const auto &yCrate = yRoot["crate"])
    {
        if (yRoot["crateId"])
            result.crateId = yCrate["crateId"].as<unsigned>();

        if (const auto &yCon = yCrate["mvlc_connection"])
        {
            result.connectionType = connection_type_from_string(yCon["type"].as<std::string>());
            result.usbIndex = yCon["usbIndex"].as<int>();
            result.usbSerial = yCon["usbSerial"].as<std::string>();
            result.ethHost = yCon["ethHost"].as<std::string>();
            result.ethJumboEnable = yCon["ethJumboEnable"].as<bool>();
        }

        if (const auto &yStacks = yCrate["readout_stacks"])
        {
            for (const auto &yStack: yStacks)
                result.stacks.emplace_back(stack_command_builder_from_yaml(yStack));
        }

        if (const auto &yTriggers = yCrate["stack_triggers"])
        {
            for (const auto &yTrig: yTriggers)
                result.triggers.push_back(yTrig.as<u32>());
        }

        if (const auto &yInitRegisters = yCrate["init_registers"])
        {
            for (auto it = yInitRegisters.begin(); it != yInitRegisters.end(); ++it)
            {
                auto sAddr = it->first.as<std::string>();
                auto sVal  = it->second.as<std::string>();
                auto addr = util::parse_unsigned<u16>(sAddr);
                auto val  = util::parse_unsigned<u32>(sVal);
                if (addr && val)
                {
                    result.initRegisters.emplace_back(std::make_pair(*addr, *val));
                }
            }
        }

        result.initTriggerIO = stack_command_builder_from_yaml(yCrate["init_trigger_io"]);
        result.initCommands = stack_command_builder_from_yaml(yCrate["init_commands"]);
        result.stopCommands = stack_command_builder_from_yaml(yCrate["stop_commands"]);

        // The mcst nodes where not present initially. CrateConfigs generated
        // by older versions do not contain them.
        if (yCrate["mcst_daq_start"])
            result.mcstDaqStart = stack_command_builder_from_yaml(yCrate["mcst_daq_start"]);

        if (yCrate["mcst_daq_stop"])
            result.mcstDaqStop = stack_command_builder_from_yaml(yCrate["mcst_daq_stop"]);
    }

    return result;
}

CrateConfig crate_config_from_yaml(std::istream &input)
{
    YAML::Node yRoot = YAML::Load(input);

    if (!yRoot)
        throw std::runtime_error("CrateConfig YAML data is empty");

    return crate_config_from_yaml(yRoot);
}

CrateConfig MESYTEC_MVLC_EXPORT crate_config_from_yaml_file(const std::string &filename)
{
    std::ifstream input(filename);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    return crate_config_from_yaml(input);
}

inline YAML::Emitter & to_yaml(YAML::Emitter &out, const StackCommandBuilder &sb)
{
    out << YAML::Hex;
    assert(out.good());
    out << sb;
    assert(out.good());
    return out;
}

std::string to_yaml(const StackCommandBuilder &sb)
{
    YAML::Emitter yEmitter;
    return to_yaml(yEmitter, sb).c_str();
}

std::string to_yaml(const CrateConfig &crateConfig)
{
    YAML::Emitter yEmitter;
    return to_yaml(yEmitter, crateConfig).c_str();
}

StackCommandBuilder stack_command_builder_from_yaml(const std::string &yaml)
{
    std::istringstream iss(yaml);
    return stack_command_builder_from_yaml(iss);
}

StackCommandBuilder stack_command_builder_from_yaml(std::istream &input)
{
    YAML::Node yRoot = YAML::Load(input);

    if (!yRoot)
        throw std::runtime_error("StackCommandBuilder YAML data is empty");

    return stack_command_builder_from_yaml(yRoot);
}

StackCommandBuilder stack_command_builder_from_yaml_file(
    const std::string &filename)
{
    std::ifstream input(filename);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    return stack_command_builder_from_yaml(input);
}

namespace detail
{
template<typename T>
std::string to_json(const T &t)
{
    YAML::Emitter yEmitter;
    YAML::Node yRoot = YAML::Load(to_yaml(yEmitter, t).c_str());

    if (!yRoot)
        return {};

    return util::detail::yaml_to_json(yRoot).dump(true);
}
}

std::string to_json(const CrateConfig &t) { return detail::to_json(t); }
std::string to_json(const StackCommandBuilder &t) { return detail::to_json(t); }

CrateConfig crate_config_from_json(const std::string &json)
{
    return crate_config_from_yaml(util::json_to_yaml(json));
}

StackCommandBuilder stack_command_builder_from_json(const std::string &json)
{
    return stack_command_builder_from_yaml(util::json_to_yaml(json));
}

} // end namespace mvlc
} // end namespace mesytec
