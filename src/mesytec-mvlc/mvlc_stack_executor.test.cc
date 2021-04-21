#include <chrono>
#include <iostream>
#include <limits>
#include <ratio>

#include "gtest/gtest.h"
#include "mesytec-mvlc/mesytec-mvlc.h"

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using SuperCT = SuperCommandType;
using StackCT = StackCommand::CommandType;

template<typename Out>
Out &log_buffer(Out &out, const std::vector<u32> &buffer, const std::string &header = {})
{
    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << endl;

    for (const auto &value: buffer)
        out << fmt::format("  {:#010x}", value) << endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << endl;

    return out;
}


#if 0

TEST(mvlc_stack_executor, SplitCommandsOptions)
{
    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);

    // reserved stack size is too small -> not advancing
    {
        const u16 StackReservedWords = 1;
        auto commands = stack.getCommands();
        CommandExecOptions options { .noBatching = false };
        ASSERT_THROW(detail::split_commands(commands, options, StackReservedWords), std::runtime_error);
    }

    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        CommandExecOptions options { .ignoreDelays = false, .noBatching = false };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 3);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][1].type, StackCommand::CommandType::VMERead);
    }

    // ignoreDelays = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        CommandExecOptions options { .ignoreDelays = true, .noBatching = false };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 1);
        ASSERT_EQ(parts[0].size(), 5);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][2].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[0][3].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][4].type, StackCommand::CommandType::VMERead);
    }

    // noBatching = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        CommandExecOptions options { .ignoreDelays = false, .noBatching = true };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 5);

        ASSERT_EQ(parts[0].size(), 1);
        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[2].size(), 1);
        ASSERT_EQ(parts[3].size(), 1);
        ASSERT_EQ(parts[4].size(), 1);

        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[3][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[4][0].type, StackCommand::CommandType::VMERead);
    }

    // ignoreDelays=true, noBatching = true
    {
        const u16 StackReservedWords = stacks::ImmediateStackReservedWords;
        auto commands = stack.getCommands();
        CommandExecOptions options { .ignoreDelays = false, .noBatching = true };
        auto parts = detail::split_commands(commands, options, StackReservedWords);
        ASSERT_EQ(parts.size(), 5);

        ASSERT_EQ(parts[0].size(), 1);
        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[2].size(), 1);
        ASSERT_EQ(parts[3].size(), 1);
        ASSERT_EQ(parts[4].size(), 1);

        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[2][0].type, StackCommand::CommandType::SoftwareDelay);
        ASSERT_EQ(parts[3][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[4][0].type, StackCommand::CommandType::VMERead);
    }
}

TEST(mvlc_stack_executor, SplitCommandsStackSizes)
{
    // Build a stack with a known start sequence, then add more vme read
    // commands. Split the stack using various immediateStackMaxSize values.

    const u32 vmeBase = 0x0;
    StackCommandBuilder stack;
    stack.addVMERead(vmeBase + 0x1000, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1002, vme_amods::A32, VMEDataWidth::D16);
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addVMERead(vmeBase + 0x1004, vme_amods::A32, VMEDataWidth::D16);
    stack.addVMERead(vmeBase + 0x1008, vme_amods::A32, VMEDataWidth::D16);

    for (int i=0; i<2000; i++)
        stack.addVMERead(vmeBase + 0x100a + 2 * i, vme_amods::A32, VMEDataWidth::D16);

    const std::vector<u16> StackReservedWords =
    {
        stacks::ImmediateStackReservedWords / 2,
        stacks::ImmediateStackReservedWords,
        stacks::ImmediateStackReservedWords * 2,
        stacks::StackMemoryWords / 2,
        stacks::StackMemoryWords,
        // Would overflow MVLCs stack memory.
        stacks::StackMemoryWords * 2,
        // Would overflow MVLCs stack memory. Still results in at least three
        // parts because the 3rd command is a SoftwareDelay.
        stacks::StackMemoryWords * 4,
    };

    const auto commands = stack.getCommands();
    //std::cout << "commandCount=" << commands.size() << std::endl;

    for (auto reservedWords: StackReservedWords)
    {
        CommandExecOptions options { .noBatching = false };
        auto parts = detail::split_commands(commands, options, reservedWords);

        //std::cout << "reservedWords=" << reservedWords
        //    << " -> partCount=" << parts.size() << std::endl;

        ASSERT_TRUE(parts.size() > 2);

        ASSERT_EQ(parts[0].size(), 2);
        ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::VMERead);
        ASSERT_EQ(parts[0][1].type, StackCommand::CommandType::VMERead);

        ASSERT_EQ(parts[1].size(), 1);
        ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);

        ASSERT_TRUE(parts[2][0].type !=  StackCommand::CommandType::SoftwareDelay);

        for (const auto &part: parts)
        {
            ASSERT_TRUE(get_encoded_stack_size(part) <= reservedWords);
            //std::cout << "  part commandCount=" << part.size() << ", encodedSize=" << detail::get_encoded_stack_size(part) << std::endl;
        }
    }
}

TEST(mvlc_stack_executor, SplitCommandsSoftwareDelays)
{
    StackCommandBuilder stack;
    stack.addSoftwareDelay(std::chrono::milliseconds(100));
    stack.addSoftwareDelay(std::chrono::milliseconds(100));

    auto commands = stack.getCommands();
    auto parts = detail::split_commands(commands);

    ASSERT_EQ(parts.size(), 2);
    ASSERT_EQ(parts[0][0].type, StackCommand::CommandType::SoftwareDelay);
    ASSERT_EQ(parts[1][0].type, StackCommand::CommandType::SoftwareDelay);
}
#endif
