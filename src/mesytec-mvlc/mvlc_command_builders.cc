#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "util/fmt.h"
#include "util/string_util.h"
#include "vme_constants.h"

namespace mesytec
{
namespace mvlc
{

namespace
{

bool is_super_command(u16 v)
{
    using SuperCT = SuperCommandType;

    return (v == static_cast<u16>(SuperCT::CmdBufferStart)
            || v == static_cast<u16>(SuperCT::CmdBufferEnd)
            || v == static_cast<u16>(SuperCT::ReferenceWord)
            || v == static_cast<u16>(SuperCT::ReadLocal)
            || v == static_cast<u16>(SuperCT::ReadLocalBlock)
            || v == static_cast<u16>(SuperCT::WriteLocal)
            || v == static_cast<u16>(SuperCT::WriteReset));
}

bool is_stack_command(u8 v)
{
    using StackCT = StackCommandType;

    return (v == static_cast<u8>(StackCT::StackStart)
            || v == static_cast<u8>(StackCT::StackEnd)
            || v == static_cast<u8>(StackCT::VMEWrite)
            || v == static_cast<u8>(StackCT::VMERead)
            || v == static_cast<u8>(StackCT::VMEMBLTSwapped)
            || v == static_cast<u8>(StackCT::WriteMarker)
            || v == static_cast<u8>(StackCT::WriteSpecial)
            || v == static_cast<u8>(StackCT::SetAddressIncMode)
            || v == static_cast<u8>(StackCT::Wait)
            || v == static_cast<u8>(StackCT::SignalAccu)
            || v == static_cast<u8>(StackCT::MaskShiftAccu)
            || v == static_cast<u8>(StackCT::SetAccu)
            || v == static_cast<u8>(StackCT::ReadToAccu)
            || v == static_cast<u8>(StackCT::CompareLoopAccu)
            );
}

}

//
// SuperCommandBuilder
//

SuperCommandBuilder &SuperCommandBuilder::addReferenceWord(u16 refValue)
{
    m_commands.push_back({ SuperCommandType::ReferenceWord, 0u, refValue });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addReadLocal(u16 address)
{
    m_commands.push_back({ SuperCommandType::ReadLocal, address, 0 });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addReadLocalBlock(u16 address, u16 words)
{
    m_commands.push_back({ SuperCommandType::ReadLocalBlock, address, words });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addWriteLocal(u16 address, u32 value)
{
    m_commands.push_back({ SuperCommandType::WriteLocal, address, value });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addWriteReset()
{
    m_commands.push_back({ SuperCommandType::WriteReset, 0, 0 });
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addCommand(const SuperCommand &cmd)
{
    m_commands.push_back(cmd);
    return *this;
}

SuperCommandBuilder &SuperCommandBuilder::addCommands(const std::vector<SuperCommand> &commands)
{
    std::copy(std::begin(commands), std::end(commands), std::back_inserter(m_commands));
    return *this;
}

// Below are shortcut methods which internally create a stack using
// outputPipe=CommandPipe(=0) and offset=0
SuperCommandBuilder &SuperCommandBuilder::addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth, bool lateRead)
{
    auto stack = StackCommandBuilder().addVMERead(address, amod, dataWidth, lateRead);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    auto stack = StackCommandBuilder().addVMEBlockRead(address, amod, maxTransfers);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEMBLTSwapped(u32 address, u8 amod, u16 maxTransfers)
{
    auto stack = StackCommandBuilder().addVMEMBLTSwapped(address, amod, maxTransfers);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEMBLTSwapped(u32 address, u16 maxTransfers)
{
    auto stack = StackCommandBuilder().addVMEMBLTSwapped(address, maxTransfers);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    auto stack = StackCommandBuilder().addVMEWrite(address, value, amod, dataWidth);
    return addCommands(make_stack_upload_commands(CommandPipe, 0u, stack));
}

SuperCommandBuilder &SuperCommandBuilder::addStackUpload(
    const StackCommandBuilder &stackBuilder,
    u8 stackOutputPipe, u16 stackMemoryOffset)
{
    return addCommands(make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stackBuilder));
}

SuperCommandBuilder &SuperCommandBuilder::addStackUpload(
    const std::vector<u32> &stackBuffer,
    u8 stackOutputPipe, u16 stackMemoryOffset)
{
    return addCommands(make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stackBuffer));
}

std::vector<SuperCommand> SuperCommandBuilder::getCommands() const
{
    return m_commands;
}

//
// StackCommand
//

std::string address_inc_mode_to_string(AddressIncrementMode mode)
{
    switch (mode)
    {
        case AddressIncrementMode::FIFO: return "fifo";
        case AddressIncrementMode::Memory: return "mem";
    }
    throw std::runtime_error("invalid AddressIncrementMode");
}

AddressIncrementMode address_inc_mode_from_string(const std::string &mode)
{
    if (mode == "fifo") return AddressIncrementMode::FIFO;
    if (mode == "mem") return AddressIncrementMode::Memory;
    throw std::runtime_error("invalid AddressIncrementMode");
}

std::string accu_comparator_to_string(AccuComparator comparator)
{
    switch (comparator)
    {
        case AccuComparator::EQ: return "eq";
        case AccuComparator::LT: return "lt";
        case AccuComparator::GT: return "gt";
        default: break;
    }
    throw std::runtime_error("invalid AccuComparator");
}

AccuComparator accu_comparator_from_string(const std::string &comparator)
{
    if (comparator == "eq") return AccuComparator::EQ;
    if (comparator == "lt") return AccuComparator::LT;
    if (comparator == "gt") return AccuComparator::GT;
    throw std::runtime_error("invalid AccuComparator");
}

namespace
{

std::string to_string(const VMEDataWidth &dw)
{
    switch (dw)
    {
        case VMEDataWidth::D16:
            return "d16";

        case VMEDataWidth::D32:
            return "d32";
    }

    throw std::runtime_error("invalid VMEDataWidth");
}

VMEDataWidth vme_data_width_from_string(const std::string &str)
{
    if (str == "d16")
        return VMEDataWidth::D16;
    else if (str == "d32")
        return VMEDataWidth::D32;

    throw std::runtime_error("invalid VMEDataWidth");
}

} // end anon namespace

std::string to_string(const StackCommand &cmd)
{
    using CT = StackCommand::CommandType;

    switch (cmd.type)
    {
        case CT::Invalid:
            return "invalid";

        case CT::StackStart:
            return "stack_start";

        case CT::StackEnd:
            return "stack_end";

        case CT::VMERead:
            if (!vme_amods::is_block_mode(cmd.amod))
            {
                auto ret = fmt::format(
                    "vme_read {:#04x} {} {:#010x}",
                    cmd.amod, to_string(cmd.dataWidth), cmd.address);

                if (cmd.lateRead)
                    ret += " late";

                return ret;
            }

            // block mode
            // FIXME: Blk2eSST is missing
            return fmt::format(
                "vme_block_read {:#04x} {} {:#010x}",
                cmd.amod, cmd.transfers, cmd.address);

        case CT::VMEMBLTSwapped:
            return fmt::format(
                "vme_mblt_swapped {:#04x} {} {:#010x}",
                cmd.amod, cmd.transfers, cmd.address);

        case CT::VMEWrite:
            return fmt::format(
                "vme_write {:#04x} {} {:#010x} {:#010x}",
                cmd.amod, to_string(cmd.dataWidth), cmd.address, cmd.value);

        case CT::WriteMarker:
            return fmt::format("write_marker {:#010x}", cmd.value);

        case CT::WriteSpecial:
            return fmt::format("write_special {}", cmd.value);

        case CT::SetAddressIncMode:
                return fmt::format("set_address_inc_mode {}",
                    address_inc_mode_to_string(static_cast<AddressIncrementMode>(cmd.value)));

        case CT::Wait:
                return fmt::format("wait {}", cmd.value);

        case CT::SignalAccu:
                return fmt::format("signal_accu");

        case CT::MaskShiftAccu:
                // args: mask, shift
                return fmt::format("mask_shift_accu {:#010x} {}", cmd.address, cmd.value);

        case CT::SetAccu:
                return fmt::format("set_accu {}", cmd.value);

        case CT::ReadToAccu:
                // same as non-block vme reads
                return fmt::format("read_to_accu {:#04x} {} {:#010x}",
                    cmd.amod, to_string(cmd.dataWidth), cmd.address);

        case CT::CompareLoopAccu:
                // args: compare mode, value
                return fmt::format("compare_loop_accu {} {}",
                    accu_comparator_to_string(static_cast<AccuComparator>(cmd.value)), cmd.address);

        case CT::SoftwareDelay:
            return fmt::format("software_delay {}", cmd.value);

        case CT::Custom:
            YAML::Emitter out;

            out << YAML::BeginMap;
            out << YAML::Key << "custom_cmd" << YAML::Value;

            {
                out << YAML::BeginMap;
                out << YAML::Key << "output_words" << YAML::Value << std::to_string(cmd.transfers);
                out << YAML::Key << "custom_contents" << YAML::Value;
                {
                    out << YAML::BeginSeq;
                    for (const u32 w: cmd.customValues)
                    {
                        out << fmt::format("{:#010x}", w);
                    }
                    out << YAML::EndSeq;
                }
                out << YAML::EndMap;
                assert(out.good());
            }

            out << YAML::EndMap;

            return out.c_str();
    }

    return {};
}

StackCommand stack_command_from_string(const std::string &str)
{
    using CT = StackCommand::CommandType;

    if (str.empty())
        throw std::runtime_error("empty line");

    StackCommand result = {};
    std::istringstream iss(str);
    std::string name;
    std::string arg;
    iss >> name;

    if (name == "stack_start")
        result.type = CT::StackStart;
    else if (name == "stack_end")
        result.type = CT::StackEnd;
    else if (name == "vme_read")
    {
        result.type = CT::VMERead;
        iss >> arg; result.amod = std::stoul(arg, nullptr, 0);
        iss >> arg; result.dataWidth = vme_data_width_from_string(arg);
        iss >> arg; result.address = std::stoul(arg, nullptr, 0);
        arg = {};
        iss >> arg; result.lateRead = (arg == "slow" || arg == "late");
    }
    else if (name == "vme_block_read")
    {
        // FIXME: Blk2eSST is missing
        result.type = CT::VMERead;
        iss >> arg; result.amod = std::stoul(arg, nullptr, 0);
        iss >> arg; result.transfers = std::stoul(arg, nullptr, 0);
        iss >> arg; result.address = std::stoul(arg, nullptr, 0);
    }
    else if (name == "vme_mblt_swapped")
    {
        result.type = CT::VMEMBLTSwapped;
        iss >> arg; result.amod = std::stoul(arg, nullptr, 0);
        iss >> arg; result.transfers = std::stoul(arg, nullptr, 0);
        iss >> arg; result.address = std::stoul(arg, nullptr, 0);
    }
    else if (name == "vme_write")
    {
        result.type = CT::VMEWrite;
        iss >> arg; result.amod = std::stoul(arg, nullptr, 0);
        iss >> arg; result.dataWidth = vme_data_width_from_string(arg);
        iss >> arg; result.address = std::stoul(arg, nullptr, 0);
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "write_marker")
    {
        result.type = CT::WriteMarker;
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "write_special")
    {
        result.type = CT::WriteSpecial;
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "set_address_inc_mode")
    {
        result.type = CT::SetAddressIncMode;
        iss >> arg; result.value = static_cast<u32>(address_inc_mode_from_string(arg));
    }
    else if (name == "wait")
    {
        result.type = CT::Wait;
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "signal_accu")
    {
        result.type = CT::SignalAccu;
    }
    else if (name == "mask_shift_accu")
    {
        result.type = CT::MaskShiftAccu;
        iss >> arg; result.address = std::stoul(arg, nullptr, 0); // mask
        iss >> arg; result.value = std::stoul(arg, nullptr, 0); // shift
    }
    else if (name == "set_accu")
    {
        result.type = CT::SetAccu;
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "read_to_accu")
    {
        result.type = CT::ReadToAccu;
        iss >> arg; result.amod = std::stoul(arg, nullptr, 0);
        iss >> arg; result.dataWidth = vme_data_width_from_string(arg);
        iss >> arg; result.address = std::stoul(arg, nullptr, 0);
        arg = {};
        iss >> arg; result.lateRead = (arg == "slow" || arg == "late");
    }
    else if (name == "compare_loop_accu")
    {
        result.type = CT::CompareLoopAccu;
        iss >> arg; result.value = static_cast<u32>(accu_comparator_from_string(arg)); // comparator
        iss >> arg; result.address = std::stoul(arg, nullptr, 0); // value to compare against
    }
    else if (name == "software_delay")
    {
        result.type = CT::SoftwareDelay;
        iss >> arg; result.value = std::stoul(arg, nullptr, 0);
    }
    else if (name == "custom_cmd:")
    {
        // Note: the custom command is encoded as inline YAML!
        YAML::Node yRoot = YAML::Load(str);

        if (!yRoot || !yRoot["custom_cmd"])
            throw std::runtime_error("Could not parse inline YAML for 'custom_cmd'");

        yRoot = yRoot["custom_cmd"];

        result.type = CT::Custom;

        if (yRoot["output_words"])
            result.transfers = yRoot["output_words"].as<u32>();

        for (const auto &yVal: yRoot["custom_contents"])
        {
            result.customValues.push_back(yVal.as<u32>());
        }
    }
    else
        throw std::runtime_error("invalid command");

    return result;
}

//
// StackCommandBuilder
//

using CommandType = StackCommand::CommandType;

StackCommandBuilder::StackCommandBuilder(const std::vector<StackCommand> &commands)
{
    for (const auto &cmd: commands)
        addCommand(cmd);
}

StackCommandBuilder::StackCommandBuilder(const std::string &name)
    : m_name(name)
{}

StackCommandBuilder::StackCommandBuilder(const std::string &name, const std::vector<StackCommand> &commands)
    : m_name(name)
{
    for (const auto &cmd: commands)
        addCommand(cmd);
}

bool StackCommandBuilder::operator==(const StackCommandBuilder &o) const
{
    return m_name == o.m_name
        && m_suppressPipeOutput == o.m_suppressPipeOutput
        && m_groups == o.m_groups;
}

StackCommandBuilder &StackCommandBuilder::addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth, bool lateRead)
{
    StackCommand cmd = {};
    cmd.type = CommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;
    cmd.lateRead = lateRead;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers)
{
    StackCommand cmd = {};
    cmd.type = CommandType::VMERead;
    cmd.address = address;
    cmd.amod = amod;
    cmd.transfers = maxTransfers;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addVMEMBLTSwapped(u32 address, u8 amod, u16 maxTransfers)
{
    StackCommand cmd = {};
    cmd.type = CommandType::VMEMBLTSwapped;
    cmd.address = address;
    cmd.amod = amod;
    cmd.transfers = maxTransfers;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addVMEMBLTSwapped(u32 address, u16 maxTransfers)
{
    return addVMEMBLTSwapped(address, vme_amods::MBLT64, maxTransfers);
}


StackCommandBuilder &StackCommandBuilder::addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth)
{
    StackCommand cmd = {};
    cmd.type = CommandType::VMEWrite;
    cmd.address = address;
    cmd.value = value;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addWriteMarker(u32 value)
{
    StackCommand cmd = {};
    cmd.type = CommandType::WriteMarker;
    cmd.value = value;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addSetAddressIncMode(const AddressIncrementMode &mode)
{
    StackCommand cmd = {};
    cmd.type = CommandType::SetAddressIncMode;
    cmd.value = static_cast<u32>(mode);

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addWait(u32 clocks)
{
    StackCommand cmd = {};
    cmd.type = CommandType::Wait;
    cmd.value = clocks;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addSignalAccu()
{
    StackCommand cmd = {};
    cmd.type = CommandType::SignalAccu;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addMaskShiftAccu(u32 mask, u8 shift)
{
    StackCommand cmd = {};
    cmd.type = CommandType::MaskShiftAccu;
    cmd.value = shift;
    cmd.address = mask;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addSetAccu(u32 value)
{
    StackCommand cmd = {};
    cmd.type = CommandType::SetAccu;
    cmd.value = value;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addReadToAccu(u32 address, u8 amod, VMEDataWidth dataWidth, bool lateRead)
{
    StackCommand cmd = {};
    cmd.type = CommandType::ReadToAccu;
    cmd.address = address;
    cmd.amod = amod;
    cmd.dataWidth = dataWidth;
    cmd.lateRead = lateRead;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addCompareLoopAccu(AccuComparator comp, u32 value)
{
    StackCommand cmd = {};
    cmd.type = CommandType::CompareLoopAccu;
    cmd.value = static_cast<u32>(comp);
    cmd.address = value;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addWriteSpecial(u32 specialValue)
{
    StackCommand cmd = {};
    cmd.type = CommandType::WriteSpecial;
    cmd.value = specialValue;

    return addCommand(cmd);
}

StackCommandBuilder &StackCommandBuilder::addSoftwareDelay(const std::chrono::milliseconds &ms)
{
    StackCommand cmd = {};
    cmd.type = CommandType::SoftwareDelay;
    cmd.value = ms.count();

    addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addCommand(const StackCommand &cmd)
{
    if (!hasOpenGroup())
        beginGroup();

    assert(hasOpenGroup());

    m_groups.back().commands.push_back(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::beginGroup(const std::string &name)
{
    m_groups.emplace_back(Group{name, {}});
    return *this;
}

std::vector<StackCommand> StackCommandBuilder::getCommands() const
{
    std::vector<StackCommand> ret;

    std::for_each(
        std::begin(m_groups), std::end(m_groups),
        [&ret] (const Group &group)
        {
            std::copy(
                std::begin(group.commands), std::end(group.commands),
                std::back_inserter(ret));
        });

    return ret;
}

std::vector<StackCommand> StackCommandBuilder::getCommands(size_t groupIndex) const
{
    return getGroup(groupIndex).commands;
}

std::vector<StackCommand> StackCommandBuilder::getCommands(const std::string &groupName) const
{
    return getGroup(groupName).commands;
}

const StackCommandBuilder::Group &StackCommandBuilder::getGroup(size_t groupIndex) const
{
    return m_groups[groupIndex];
}

StackCommandBuilder::Group StackCommandBuilder::getGroup(const std::string &groupName) const
{
    auto it = std::find_if(
        std::begin(m_groups), std::end(m_groups),
        [&groupName] (const Group &group)
        {
            return group.name == groupName;
        });

    if (it != std::end(m_groups))
        return *it;

    return {};
}

StackCommandBuilder &StackCommandBuilder::addGroup(
    const std::string &name, const std::vector<StackCommand> &commands)
{
    beginGroup(name);

    for (const auto &cmd: commands)
        addCommand(cmd);

    return *this;
}

StackCommandBuilder &StackCommandBuilder::addGroup(
    const StackCommandBuilder::Group &group)
{
    m_groups.push_back(group);

    return *this;
}

bool MESYTEC_MVLC_EXPORT produces_output(const StackCommand &cmd)
{
    switch (cmd.type)
    {
        case StackCommand::CommandType::VMERead:
        case StackCommand::CommandType::VMEMBLTSwapped:
        case StackCommand::CommandType::WriteMarker:
        case StackCommand::CommandType::WriteSpecial:
            return true;

        case StackCommand::CommandType::Custom:
            // The number of output_words produced by the custom command is
            // stored in the 'transfers' member.
            return cmd.transfers > 0;

        default:
            break;
    }

    return false;
}

bool MESYTEC_MVLC_EXPORT produces_output(const StackCommandBuilder::Group &group)
{
    return std::any_of(
        std::begin(group.commands), std::end(group.commands),
        [] (const auto &cmd) { return produces_output(cmd); });
}

bool MESYTEC_MVLC_EXPORT produces_output(const StackCommandBuilder &stack)
{
    auto groups = stack.getGroups();

    return std::any_of(
        std::begin(groups), std::end(groups),
        [] (const auto &group) { return produces_output(group); });
}

//
// Conversion to the mvlc buffer format
//

size_t get_encoded_size(const SuperCommandType &type)
{
    using SuperCT = SuperCommandType;

    switch (type)
    {
        case SuperCT::ReferenceWord:
        case SuperCT::ReadLocal:
        case SuperCT::WriteReset:
        case SuperCT::CmdBufferStart:
        case SuperCT::CmdBufferEnd:
        case SuperCT::EthDelay:
            return 1;

        case SuperCT::ReadLocalBlock:
        case SuperCT::WriteLocal:
            return 2;
    }

    return 0u;
}

size_t get_encoded_size(const SuperCommand &cmd)
{
    return get_encoded_size(cmd.type);
}

size_t get_encoded_size(const StackCommand::CommandType &type)
{
    using StackCT = StackCommand::CommandType;

    switch (type)
    {
        case StackCT::StackStart:
        case StackCT::StackEnd:
        case StackCT::SetAddressIncMode:
        case StackCT::Wait:
        case StackCT::SignalAccu:
        case StackCT::Custom:
            return 1;

        case StackCT::VMERead:
        case StackCT::VMEMBLTSwapped:
        case StackCT::MaskShiftAccu:
        case StackCT::SetAccu:
        case StackCT::ReadToAccu:
        case StackCT::CompareLoopAccu:
            return 2;

        case StackCT::VMEWrite:
            return 3;

        case StackCT::WriteMarker:
            return 2;

        case StackCT::WriteSpecial:
            return 1;

        case StackCT::Invalid:
        case StackCT::SoftwareDelay:
            return 0;
    }

    return 0u;
}

size_t get_encoded_size(const StackCommand &cmd)
{
    return get_encoded_size(cmd.type);
}

size_t get_encoded_stack_size(const std::vector<StackCommand> &commands)
{
    size_t encodedPartSize = 2 + std::accumulate(
        std::begin(commands), std::end(commands), static_cast<size_t>(0u),
        [] (const size_t &encodedSize, const StackCommand &cmd)
        {
            return encodedSize + get_encoded_size(cmd);
        });

    return encodedPartSize;
}

std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands)
{
    return make_command_buffer(commands.getCommands());
}

std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands)
{
    return make_command_buffer(basic_string_view<SuperCommand>(commands.data(), commands.size()));
}

MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const basic_string_view<SuperCommand> &commands)
{
    using namespace super_commands;
    using SuperCT = SuperCommandType;

    std::vector<u32> result;

    // CmdBufferStart
    result.push_back(static_cast<u32>(SuperCT::CmdBufferStart) << SuperCmdShift);

    for (const auto &cmd: commands)
    {
        u32 cmdWord = (static_cast<u32>(cmd.type) << SuperCmdShift);

        switch (cmd.type)
        {
            case SuperCT::ReferenceWord:
                result.push_back(cmdWord | (cmd.value & SuperCmdArgMask));
                break;

            case SuperCT::ReadLocal:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                break;

            case SuperCT::ReadLocalBlock:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                result.push_back(cmd.value); // transfer count
                break;

            case SuperCT::WriteLocal:
                result.push_back(cmdWord | (cmd.address & SuperCmdArgMask));
                result.push_back(cmd.value);
                break;

            case SuperCT::WriteReset:
                result.push_back(cmdWord);
                break;

            // Similar to StackStart and StackEnd these should not be manually
            // added to the list of super commands but they are still handled
            // in here just in case.
            case SuperCT::CmdBufferStart:
            case SuperCT::CmdBufferEnd:
            case SuperCT::EthDelay:
                result.push_back(cmdWord);
                break;
        }
    }

    // CmdBufferEnd
    result.push_back(static_cast<u32>(SuperCT::CmdBufferEnd) << SuperCmdShift);

    return result;
}

SuperCommandBuilder super_builder_from_buffer(const std::vector<u32> &buffer)
{
    using namespace super_commands;
    using SuperCT = SuperCommandType;

    SuperCommandBuilder result;

    for (auto it = buffer.begin(); it != buffer.end(); ++it)
    {
        u16 sct = (*it >> SuperCmdShift) & SuperCmdMask;

        if (!is_super_command(sct))
            break; // TODO: error

        SuperCommand cmd = {};
        cmd.type = static_cast<SuperCT>(sct);

        switch (cmd.type)
        {
            case SuperCT::CmdBufferStart:
            case SuperCT::CmdBufferEnd:
            case SuperCT::EthDelay:
                continue;

            case SuperCT::ReferenceWord:
                cmd.value = (*it >> SuperCmdArgShift) & SuperCmdArgMask;
                break;

            case SuperCT::ReadLocal:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;
                break;

            case SuperCT::ReadLocalBlock:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case SuperCT::WriteLocal:
                cmd.address = (*it >> SuperCmdArgShift) & SuperCmdArgMask;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case SuperCT::WriteReset:
                break;
        }

        result.addCommand(cmd);
    }

    return result;
}

std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder)
{
    return make_stack_buffer(builder.getCommands());
}

std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack)
{
    std::vector<u32> result;

    for (const auto &cmd: stack)
    {
        u32 cmdWord = static_cast<u32>(cmd.type) << stack_commands::CmdShift;

        switch (cmd.type)
        {
            case CommandType::VMERead:
            case CommandType::VMEMBLTSwapped:
                if (!vme_amods::is_block_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;

                    u32 dataWidth = static_cast<u32>(cmd.dataWidth);
                    dataWidth |= static_cast<u32>(cmd.lateRead) << stack_commands::LateReadShift;

                    cmdWord |= dataWidth << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_blt_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_mblt_mode(cmd.amod))
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }
                else if (vme_amods::is_esst64_mode(cmd.amod))
                {
                    cmdWord |= (cmd.amod | (static_cast<u32>(cmd.rate) << Blk2eSSTRateShift))
                        << stack_commands::CmdArg0Shift;
                    cmdWord |= (cmd.transfers & stack_commands::CmdArg1Mask) << stack_commands::CmdArg1Shift;
                }

                result.push_back(cmdWord);
                result.push_back(cmd.address);

                break;

            case CommandType::VMEWrite:
                cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                cmdWord |= static_cast<u32>(cmd.dataWidth) << stack_commands::CmdArg1Shift;

                result.push_back(cmdWord);
                result.push_back(cmd.address);
                result.push_back(cmd.value);

                break;

            case CommandType::WriteMarker:
                result.push_back(cmdWord);
                result.push_back(cmd.value);

                break;

            case CommandType::WriteSpecial:
                cmdWord |= cmd.value & 0x00FFFFFFu; // 24 bits
                result.push_back(cmdWord);
                break;

            // Note: these two should not be manually added to the stack but
            // will be part of the command buffer used for uploading the stack.
            case CommandType::StackStart:
            case CommandType::StackEnd:
                result.push_back(cmdWord);
                break;

            case CommandType::SoftwareDelay:
                throw std::runtime_error("unsupported stack buffer command: SoftwareDelay");

            case CommandType::Invalid:
                throw std::runtime_error("unsupported stack buffer command: Invalid");

            case CommandType::Custom:
                for (u32 customValue: cmd.customValues)
                    result.push_back(customValue);
                break;

            case CommandType::SetAddressIncMode:
                cmdWord |= cmd.value & 0x00FFFFFFu; // 0: FIFO, 1: mem
                result.push_back(cmdWord);
                break;

            case CommandType::Wait:
                cmdWord |= cmd.value & 0x00FFFFFFu;
                result.push_back(cmdWord);
                break;

            case CommandType::SignalAccu:
                result.push_back(cmdWord);
                break;

            case CommandType::MaskShiftAccu:
                cmdWord |= cmd.value; // shift
                result.push_back(cmdWord);
                result.push_back(cmd.address); // mask
                break;

            case CommandType::SetAccu:
                result.push_back(cmdWord);
                result.push_back(cmd.value);
                break;

            case CommandType::ReadToAccu:
                {
                    cmdWord |= cmd.amod << stack_commands::CmdArg0Shift;
                    u32 dataWidth = static_cast<u32>(cmd.dataWidth);
                    dataWidth |= static_cast<u32>(cmd.lateRead) << stack_commands::LateReadShift;
                    cmdWord |= dataWidth << stack_commands::CmdArg1Shift;
                    result.push_back(cmdWord);
                    result.push_back(cmd.address);
                }
                break;

            case CommandType::CompareLoopAccu:
                cmdWord |= cmd.value; // comparator
                result.push_back(cmdWord);
                result.push_back(cmd.address); // compare value
                break;
        }
    }

    return result;
}

std::vector<u32> make_stack_buffer(const StackCommand &cmd)
{
    return make_stack_buffer(std::vector<StackCommand>{ cmd });
}

StackCommandBuilder stack_builder_from_buffer(const std::vector<u32> &buffer)
{
    return StackCommandBuilder(stack_commands_from_buffer(buffer));
}

// TODO: error handling (std::pair?)
std::vector<StackCommand> stack_commands_from_buffer(const std::vector<u32> &buffer)
{
    using namespace stack_commands;
    using StackCT = StackCommand::CommandType;

    std::vector<StackCommand> result;

    for (auto it = buffer.begin(); it != buffer.end(); ++it)
    {
        u8 sct = (*it >> CmdShift) & CmdMask;
        u8 arg0 = (*it >> CmdArg0Shift) & CmdArg0Mask;
        u16 arg1 = (*it >> CmdArg1Shift) & CmdArg1Mask;

        StackCommand cmd = {};

        if (!is_stack_command(sct))
        {
            // Create a single value Custom block from unknown stack data.
            cmd.type = StackCT::Custom;
            cmd.customValues.push_back(*it);
            result.emplace_back(cmd);
            continue;
        }

        cmd.type = static_cast<StackCT>(sct);

        switch (cmd.type)
        {
            case StackCT::StackStart:
            case StackCT::StackEnd:
            case StackCT::SoftwareDelay:
            case StackCT::Invalid:
            case StackCT::Custom:
                continue;

            case StackCT::VMERead:
            case StackCT::VMEMBLTSwapped:
            case StackCT::ReadToAccu:
                cmd.amod = arg0;

                if (!vme_amods::is_block_mode(cmd.amod))
                {
                    u32 dataWidth = arg1 & 0b11u;
                    bool lateRead = (arg1 >> stack_commands::LateReadShift) & 0b1u;
                    cmd.dataWidth = static_cast<VMEDataWidth>(dataWidth);
                    cmd.lateRead = lateRead;
                }
                else if (vme_amods::is_blt_mode(cmd.amod))
                {
                    cmd.transfers = arg1;
                }
                else if (vme_amods::is_mblt_mode(cmd.amod))
                {
                    cmd.transfers = arg1;
                }
                else if (vme_amods::is_esst64_mode(cmd.amod))
                {
                    cmd.rate = static_cast<Blk2eSSTRate>(cmd.amod >> Blk2eSSTRateShift);
                    cmd.transfers = arg1;
                }

                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it;

                break;

            case StackCT::VMEWrite:
                cmd.amod = arg0;
                cmd.dataWidth = static_cast<VMEDataWidth>(arg1);

                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it;

                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case StackCT::WriteMarker:
                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it;

                break;

            case StackCT::WriteSpecial:
                cmd.value = (*it & 0x00FFFFFFu);
                break;

            case StackCT::SetAddressIncMode:
                cmd.value = arg1;
                break;

            case StackCT::Wait:
                cmd.value = (*it & 0x00FFFFFFu); // clocks
                break;

            case StackCT::SignalAccu:
                break; // no args

            case StackCT::MaskShiftAccu:
                cmd.value = (*it & 0x00FFFFFFu); // shift
                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it; // mask
                break;

            case StackCT::SetAccu:
                if (++it != buffer.end()) // TODO: else error
                    cmd.value = *it; // accu value
                break;

            case StackCT::CompareLoopAccu:
                cmd.value = arg1; // comparator

                if (++it != buffer.end()) // TODO: else error
                    cmd.address = *it;
                break;
        }

        result.emplace_back(cmd);
    }

    return result;
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack)
{
    return make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, stack.getCommands());
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &stack)
{
    auto stackBuffer = make_stack_buffer(stack);

    return make_stack_upload_commands(stackOutputPipe, stackMemoryOffset, make_stack_buffer(stack));
}

std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackBuffer)
{
    SuperCommandBuilder super;

    u16 address = stacks::StackMemoryBegin + stackMemoryOffset;

    // StackStart
    super.addWriteLocal(
        address,
        (static_cast<u32>(StackCommandType::StackStart) << stack_commands::CmdShift
         | (stackOutputPipe << stack_commands::CmdArg0Shift)));

    address += AddressIncrement;

    // A write for each data word of the stack.
    for (u32 stackWord: stackBuffer)
    {
        super.addWriteLocal(address, stackWord);
        address += AddressIncrement;
    }

    // StackEnd
    super.addWriteLocal(
        address,
        static_cast<u32>(StackCommandType::StackEnd) << stack_commands::CmdShift);
    address += AddressIncrement;

    return super.getCommands();
}

}
}
