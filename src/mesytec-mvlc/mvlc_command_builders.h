#ifndef __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__
#define __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <version>

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
        SuperCommandBuilder &addVMERead(u32 address, u8 amod, VMEDataWidth dataWidth, bool lateRead = false, bool fifo = true);

        SuperCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers, bool fifo = true);
        SuperCommandBuilder &addVMEBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, bool fifo = true);

        SuperCommandBuilder &addVMEBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, bool fifo = true);
        SuperCommandBuilder &addVMEBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, bool fifo = true);

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
        Invalid                 = static_cast<u8>(0x0u),

        StackStart              = static_cast<u8>(StackCommandType::StackStart),
        StackEnd                = static_cast<u8>(StackCommandType::StackEnd),
        VMEWrite                = static_cast<u8>(StackCommandType::VMEWrite),
        VMERead                 = static_cast<u8>(StackCommandType::VMERead),
        VMEReadSwapped          = static_cast<u8>(StackCommandType::VMEReadSwapped),
        VMEReadMem              = static_cast<u8>(StackCommandType::VMEReadMem),
        VMEReadMemSwapped       = static_cast<u8>(StackCommandType::VMEReadMemSwapped),
        WriteMarker             = static_cast<u8>(StackCommandType::WriteMarker),
        WriteSpecial            = static_cast<u8>(StackCommandType::WriteSpecial),
        Wait                    = static_cast<u8>(StackCommandType::Wait),
        SignalAccu              = static_cast<u8>(StackCommandType::SignalAccu),
        MaskShiftAccu           = static_cast<u8>(StackCommandType::MaskShiftAccu),
        SetAccu                 = static_cast<u8>(StackCommandType::SetAccu),
        ReadToAccu              = static_cast<u8>(StackCommandType::ReadToAccu),
        CompareLoopAccu         = static_cast<u8>(StackCommandType::CompareLoopAccu),

        // Software side accumulator available when executing command lists. Not
        // available in readout stacks uploaded to the MVLC.
        // TODO (maybe): implement the software accu
        #if 0
        SoftwareAccuSet         = 0xD0u,
        SoftwareAccuMaskShift   = 0xD1u,
        SoftwareAccuTest        = 0xD2u,
        #endif

        // A value not in use by the MVLC protocol is used for the
        // SoftwareDelay command.
        SoftwareDelay           = 0xEDu,

        // Special value for custom (binary) stack data. The stack data words are
        // stored in the 'customValues' member.
        Custom                  = 0xEEu,
    };

    CommandType type = CommandType::Invalid;
    u32 address = {};
    u32 value = {};
    u8 amod = {};
    VMEDataWidth dataWidth = VMEDataWidth::D16;
    u16 transfers; // max number of transfers for block read commands / number of produced data words for custom commands
    Blk2eSSTRate rate = {};
    std::vector<u32> customValues;
    bool lateRead = false;

    bool operator==(const StackCommand &o) const noexcept
    {
        return (type == o.type
                && address == o.address
                && value == o.value
                && amod == o.amod
                && dataWidth == o.dataWidth
                && transfers == o.transfers
                && rate == o.rate
                && customValues == o.customValues
                && lateRead == o.lateRead
               );
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

// throws std::runtime_error if dw is invalid
std::string MESYTEC_MVLC_EXPORT to_string(const VMEDataWidth &dw);

// throws std::runtime_error if str cannot be converted
VMEDataWidth MESYTEC_MVLC_EXPORT vme_data_width_from_string(const std::string &str);

#ifdef __cpp_lib_optional
// version of the above that does not throw
std::optional<VMEDataWidth> MESYTEC_MVLC_EXPORT parse_vme_datawidth(const std::string &str);
#endif

std::string MESYTEC_MVLC_EXPORT to_string(const StackCommand &cmd);
StackCommand MESYTEC_MVLC_EXPORT stack_command_from_string(const std::string &str);

// True for output producing read commands.
inline bool is_read_command(const StackCommand &cmd)
{
    using StackCT = StackCommand::CommandType;

    switch (cmd.type)
    {
    case StackCT::VMERead:
    case StackCT::VMEReadSwapped:
    case StackCT::VMEReadMem:
    case StackCT::VMEReadMemSwapped:
        return true;
    default:
        break;
    }

    return false;
}

class MESYTEC_MVLC_EXPORT StackCommandBuilder
{
    public:
        // The commands are organized into groups to hold the readout commands
        // for a single VME module. This is required for e.g. the readout parser
        // to work as it needs information about the readout commands for each
        // specific module read out by the stack.
        struct Group
        {
            // Name of the group. mvme fills it with the VME module name.
            std::string name;

            // Readout commands for the module.
            std::vector<StackCommand> commands;

            // Optional meta info. mvme stores the VME module type name (the one
            // defined by the mvme templates) under 'vme_module_type'.
            std::map<std::string, std::string> meta;

            bool operator==(const Group &o) const
            {
                return name == o.name
                    && commands == o.commands;
            }

            bool operator!=(const Group &o) const { return !(*this == o); }

            bool empty() const { return commands.empty(); }
            size_t size() const { return commands.size(); }
        };

        StackCommandBuilder() {}
        explicit StackCommandBuilder(const std::vector<StackCommand> &commands);
        explicit StackCommandBuilder(const std::string &name);
        StackCommandBuilder(const std::string &name, const std::vector<StackCommand> &commands);

        bool operator==(const StackCommandBuilder &o) const;
        bool operator!=(const StackCommandBuilder &o) const { return !(*this == o); }

        // Note: these methods each add a single command to the currently open
        // group. If there exists no open group a new group with an empty name
        // will be created.

        // Note: this is for single value VME reads but still has the FIFO flag
        // like the block reads below. The reason is that the MVLC stack
        // accumulator can turn the VME read into a block transfer, which means
        // there must be a way to control if the read address should be
        // incremented or not.
        StackCommandBuilder &addVMERead(
            u32 address, u8 amod, VMEDataWidth dataWidth,
            bool lateRead = false, bool fifo = true);

        StackCommandBuilder &addVMEBlockRead(u32 address, u8 amod, u16 maxTransfers, bool fifo = true); // BLT, MBLT
        StackCommandBuilder &addVMEBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, bool fifo = true); // 2eSST

        StackCommandBuilder &addVMEBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers, bool fifo = true); // MBLT
        StackCommandBuilder &addVMEBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, bool fifo = true); // 2eSST

        StackCommandBuilder &addVMEWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);
        StackCommandBuilder &addWriteMarker(u32 value);

        StackCommandBuilder &addWait(u32 clocks);
        StackCommandBuilder &addSignalAccu();
        StackCommandBuilder &addMaskShiftAccu(u32 mask, u8 shift);
        StackCommandBuilder &addSetAccu(u32 value);
        StackCommandBuilder &addReadToAccu(
            u32 address, u8 amod, VMEDataWidth dataWidth, bool lateRead = false);
        StackCommandBuilder &addCompareLoopAccu(AccuComparator comp, u32 value);
        StackCommandBuilder &addWriteSpecial(u32 specialValue);

        // Intended for direct stack execution. Suspends further command
        // execution for the given duration.
        // Is not supported for stacks uploaded to the MVLC for autonomous
        // execution.
        StackCommandBuilder &addSoftwareDelay(const std::chrono::milliseconds &ms);

        // Add a manually created StackCommand object.
        StackCommandBuilder &addCommand(const StackCommand &cmd);

        // Begins a new group using the supplied name.
        StackCommandBuilder &beginGroup(
            const std::string &name = {},
            const std::map<std::string, std::string> & meta = {});

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

        StackCommandBuilder &addGroup(
            const std::string &name,
            const std::vector<StackCommand> &commands,
            const std::map<std::string, std::string> & meta = {});

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

        StackCommandBuilder &setSuppressPipeOutput(bool suppress)
        {
            m_suppressPipeOutput = suppress; return *this;
        }

        bool empty() const
        {
            return (m_groups.empty()
                    || std::all_of(
                        std::begin(m_groups), std::end(m_groups),
                        [] (const auto &group) { return group.empty(); }));
        }

        const StackCommand operator[](size_t i) const
        {
            return getCommands()[i]; // expensive!
        }

        size_t commandCount() const;

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
MESYTEC_MVLC_EXPORT std::vector<u32> make_stack_buffer(const StackCommand &cmd);

// Note: these do not work if the stack contains custom/arbitrary data.
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

// Command parsing utilities
MESYTEC_MVLC_EXPORT std::string accu_comparator_to_string(AccuComparator comp);
MESYTEC_MVLC_EXPORT AccuComparator accu_comparator_from_string(const std::string &comparator);

}
}

#endif /* __MESYTEC_MVLC_MVLC_COMMAND_BUILDERS_H__ */
