#ifndef __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__
#define __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__

#include <chrono>
#include <string>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_constants.h"
#include "util/string_view.hpp"

namespace mesytec
{
namespace mvlc
{

using namespace nonstd;

//
// SuperCommands for direct communication with the MVLC
//

struct MESYTEC_MVLC_EXPORT SuperCommand
{
    SuperCommandType type;
    u16 address;
    u32 value;

    bool operator==(const SuperCommand &o) const noexcept
    {
        return (type == o.type
                && address == o.address
                && value == o.value);
    }

    bool operator!=(const SuperCommand &o) const noexcept
    {
        return !(*this == o);
    }
};

class StackCommandBuilder;

class MESYTEC_MVLC_EXPORT SuperCommandBuilder
{
    public:
        SuperCommandBuilder &addReferenceWord(u16 refValue);
        SuperCommandBuilder &addReadLocal(u16 address);
        SuperCommandBuilder &addReadLocalBlock(u16 address, u16 words);
        SuperCommandBuilder &addWriteLocal(u16 address, u32 value);
        SuperCommandBuilder &addWriteReset();
        SuperCommandBuilder &addCommand(const SuperCommand &cmd);
        SuperCommandBuilder &addCommands(const std::vector<SuperCommand> &commands);

        // Below are shortcut methods which internally create a stack using
        // outputPipe=CommandPipe(=0) and stackMemoryOffset=0
        SuperCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        SuperCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        SuperCommandBuilder &addVMEMBLTSwapped(u32 address, u8 amod, u16 maxTransfers);
        SuperCommandBuilder &addVMEMBLTSwapped(u32 address, u16 maxTransfers);
        SuperCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        SuperCommandBuilder &addStackUpload(
            const StackCommandBuilder &stackBuilder,
            u8 stackOutputPipe, u16 stackMemoryOffset);

        SuperCommandBuilder &addStackUpload(
            const std::vector<u32> &stackBuffer,
            u8 stackOutputPipe, u16 stackMemoryOffset);

        std::vector<SuperCommand> getCommands() const;

        bool empty() const { return m_commands.empty(); };

        const SuperCommand &operator[](size_t i) const { return m_commands[i]; }

    private:
        std::vector<SuperCommand> m_commands;
};

//
// StackCommands for direct execution and VME readout
//

struct MESYTEC_MVLC_EXPORT StackCommand
{
    // A crude way of extending the StackCommandType enum.
    enum class CommandType: u8
    {
        Invalid             = static_cast<u8>(0x0u),

        StackStart          = static_cast<u8>(StackCommandType::StackStart),
        StackEnd            = static_cast<u8>(StackCommandType::StackEnd),
        VMERead             = static_cast<u8>(StackCommandType::VMERead),
        VMEWrite            = static_cast<u8>(StackCommandType::VMEWrite),
        VMEMBLTSwapped      = static_cast<u8>(StackCommandType::VMEMBLTSwapped),
        SignallingVMERead   = static_cast<u8>(StackCommandType::SignallingVMERead),
        WriteMarker         = static_cast<u8>(StackCommandType::WriteMarker),
        WriteSpecial        = static_cast<u8>(StackCommandType::WriteSpecial),
        WriteSignalWord     = static_cast<u8>(StackCommandType::WriteSignalWord),
        // A value not in use by the MVLC protocol is used for the
        // SoftwareDelay command.
        SoftwareDelay       = static_cast<u8>(0xEDu),
    };

    CommandType type = CommandType::Invalid;
    u32 address;
    u32 value;
    u8 amod;
    VMEDataWidth dataWidth = VMEDataWidth::D16;
    u16 transfers;
    Blk2eSSTRate rate;

    bool operator==(const StackCommand &o) const noexcept
    {
        return (type == o.type
                && address == o.address
                && value == o.value
                && amod == o.amod
                && dataWidth == o.dataWidth
                && transfers == o.transfers
                && rate == o.rate);
    }

    bool operator!=(const StackCommand &o) const noexcept
    {
        return !(*this == o);
    }

    explicit operator bool() const
    {
        return type != CommandType::Invalid;
    }
};

std::string MESYTEC_MVLC_EXPORT to_string(const StackCommand &cmd);
StackCommand MESYTEC_MVLC_EXPORT stack_command_from_string(const std::string &str);

class MESYTEC_MVLC_EXPORT StackCommandBuilder
{
    public:
        struct Group
        {
            std::string name;
            std::vector<StackCommand> commands;

            bool operator==(const Group &o) const
            {
                return name == o.name
                    && commands == o.commands;
            }

            bool operator!=(const Group &o) const { return !(*this == o); }

            bool empty() const { return commands.empty(); }
        };

        StackCommandBuilder() {}
        explicit StackCommandBuilder(const std::vector<StackCommand> &commands);
        explicit StackCommandBuilder(const std::string &name);
        StackCommandBuilder(const std::string &name, const std::vector<StackCommand> &commands);

        bool operator==(const StackCommandBuilder &o) const;
        bool operator!=(const StackCommandBuilder &o) const { return !(*this == o); }

        // These methods each add a single command to the currently open group.
        // If there exists no open group a new group with an empty name will be
        // created.
        StackCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addSignallingVMERead(u32 address, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers);
        StackCommandBuilder &addVMEMBLTSwapped(u32 address, u8 amod, u16 maxTransfers);
        // Overload of addVMEMBLTSwapped() using vme_amods::MBLT64 as the VME address modifier.
        StackCommandBuilder &addVMEMBLTSwapped(u32 address, u16 maxTransfers);
        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addWriteMarker(u32 value);
        StackCommandBuilder &addWriteSignalWord(u32 value);

        // Intended for direct stack execution. Suspends further command
        // execution for the given duration.
        // Is not supported for stacks uploaded to the MVLC for autonomous
        // execution.
        StackCommandBuilder &addSoftwareDelay(const std::chrono::milliseconds &ms);

        StackCommandBuilder &addCommand(const StackCommand &cmd);

        // Begins a new group using the supplied name.
        StackCommandBuilder &beginGroup(const std::string &name = {});

        // Returns true if at least one group exists in this StackCommandBuilder.
        bool hasOpenGroup() const { return !m_groups.empty(); }

        // Returns the number of groups in this StackCommandBuilder.
        size_t getGroupCount() const { return m_groups.size(); }

        // Returns the list of groups forming the stack.
        std::vector<Group> getGroups() const { return m_groups; }

        // Returns the group with the given groupIndex or a default constructed
        // group if the index is out of range.
        const Group &getGroup(size_t groupIndex) const;

        // Returns the group with the given groupName or a default constructed
        // group if the index is out of range.
        Group getGroup(const std::string &groupName) const;

        StackCommandBuilder &addGroup(const std::string &name, const std::vector<StackCommand> &commands);
        StackCommandBuilder &addGroup(const Group &group);

        // Returns a flattened list of the commands of all groups.
        std::vector<StackCommand> getCommands() const;

        // Returns the list of commands for the group with the given groupIndex
        // or an empty list if the index is out of range.
        std::vector<StackCommand> getCommands(size_t groupIndex) const;

        // Returns the list of commands for the group with the given groupName
        // or an empty list if no such group exists.
        std::vector<StackCommand> getCommands(const std::string &groupName) const;

        std::string getName() const { return m_name; }
        StackCommandBuilder &setName(const std::string &name) { m_name = name; return *this; }

        bool suppressPipeOutput() const { return m_suppressPipeOutput; }
        StackCommandBuilder &setSuppressPipeOutput(bool suppress) { m_suppressPipeOutput = suppress; return *this; }

        bool empty() const
        {
            return (m_groups.empty()
                    || std::all_of(
                        std::begin(m_groups), std::end(m_groups),
                        [] (const auto &group) { return group.empty(); }));
        }

        const StackCommand operator[](size_t i) const
        {
            return getCommands()[i];
        }

    private:
        std::string m_name;
        std::vector<Group> m_groups;
        bool m_suppressPipeOutput = false;
};

bool MESYTEC_MVLC_EXPORT produces_output(const StackCommand &cmd);
bool MESYTEC_MVLC_EXPORT produces_output(const StackCommandBuilder::Group &group);
bool MESYTEC_MVLC_EXPORT produces_output(const StackCommandBuilder &stack);

//
// Conversion to the mvlc buffer format
//
MESYTEC_MVLC_EXPORT size_t get_encoded_size(const SuperCommandType &type);
MESYTEC_MVLC_EXPORT size_t get_encoded_size(const SuperCommand &command);

MESYTEC_MVLC_EXPORT size_t get_encoded_size(const StackCommand::CommandType &type);
MESYTEC_MVLC_EXPORT size_t get_encoded_size(const StackCommand &command);

// Returns the sum of the sizes of the encoded commands plus 2 for StackStart and StackEnd.
MESYTEC_MVLC_EXPORT size_t get_encoded_stack_size(const std::vector<StackCommand> &commands);
inline size_t get_encoded_stack_size(const StackCommandBuilder &sb)
{
    return get_encoded_stack_size(sb.getCommands());
}

MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const SuperCommandBuilder &commands);
MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const std::vector<SuperCommand> &commands);
MESYTEC_MVLC_EXPORT std::vector<u32> make_command_buffer(const basic_string_view<SuperCommand> &commands);

MESYTEC_MVLC_EXPORT SuperCommandBuilder super_builder_from_buffer(const std::vector<u32> &buffer);

// Stack to raw stack commands. Not enclosed between StackStart and StackEnd,
// not interleaved with the write commands for uploading.
MESYTEC_MVLC_EXPORT std::vector<u32> make_stack_buffer(const StackCommandBuilder &builder);
MESYTEC_MVLC_EXPORT std::vector<u32> make_stack_buffer(const std::vector<StackCommand> &stack);

MESYTEC_MVLC_EXPORT StackCommandBuilder stack_builder_from_buffer(const std::vector<u32> &buffer);
MESYTEC_MVLC_EXPORT std::vector<StackCommand> stack_commands_from_buffer(const std::vector<u32> &buffer);

// Enclosed between StackStart and StackEnd, interleaved with WriteLocal
// commands for uploading.
MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const StackCommandBuilder &stack);

MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<StackCommand> &stack);

MESYTEC_MVLC_EXPORT std::vector<SuperCommand> make_stack_upload_commands(
    u8 stackOutputPipe, u16 StackMemoryOffset, const std::vector<u32> &stackBuffer);

}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__ */
