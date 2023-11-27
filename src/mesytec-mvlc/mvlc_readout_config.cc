#include "mvlc_readout_config.h"

#include <cassert>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/emittermanip.h>

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

            stack.addGroup(groupName, groupCommands);
        }
    }

    return stack;
}

} // end anon namespace

/// Serializes a CrateConfig to YAML format.
std::string to_yaml(const CrateConfig &crateConfig)
{
    YAML::Emitter out;

    out << YAML::Hex;

    assert(out.good());

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
    out << YAML::Key << "init_trigger_io" << YAML::Value << crateConfig.initTriggerIO;
    out << YAML::Key << "init_commands" << YAML::Value << crateConfig.initCommands;
    out << YAML::Key << "stop_commands" << YAML::Value << crateConfig.stopCommands;
    out << YAML::Key << "mcst_daq_start" << YAML::Value << crateConfig.mcstDaqStart;
    out << YAML::Key << "mcst_daq_stop" << YAML::Value << crateConfig.mcstDaqStop;

    out << YAML::EndMap; // end crate

    assert(out.good());

    return out.c_str();
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
CrateConfig crate_config_from_yaml(std::istream &input)
{
    CrateConfig result = {};

    YAML::Node yRoot = YAML::Load(input);

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

CrateConfig MESYTEC_MVLC_EXPORT crate_config_from_yaml_file(const std::string &filename)
{
    std::ifstream input(filename);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    return crate_config_from_yaml(input);
}

std::string to_yaml(const StackCommandBuilder &sb)
{
    YAML::Emitter out;
    out << YAML::Hex;
    assert(out.good());
    out << sb;
    assert(out.good());
    return out.c_str();
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

StackCommandBuilder MESYTEC_MVLC_EXPORT stack_command_builder_from_yaml_file(
    const std::string &filename)
{
    std::ifstream input(filename);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    return stack_command_builder_from_yaml(input);
}

} // end namespace mvlc
} // end namespace mesytec
