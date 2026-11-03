#include "gtest/gtest.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_mvp.h"
#include "vme_constants.h"

using namespace mesytec::mvlc;

TEST(mvlc_mvp, BootCommandBuffer)
{
    const u32 base = SelfVMEAddress;
    const u8 area = 2;
    auto buffer = mvp::make_boot_command_buffer(base, area, 0x1234u, 0xcafe0001u);

    const size_t UdpMaxPayloadSize = 1500 - 20 - 8;
    ASSERT_LE(buffer.size() * sizeof(u32), UdpMaxPayloadSize);

    auto cmds = super_builder_from_buffer(buffer).getCommands();
    ASSERT_GE(cmds.size(), 5u);

    ASSERT_EQ(cmds.front().type, SuperCommandType::ReferenceWord);
    ASSERT_EQ(cmds.front().value, 0x1234u);

    const auto &trigger = cmds[cmds.size() - 1];
    const auto &offset = cmds[cmds.size() - 2];
    ASSERT_EQ(offset.type, SuperCommandType::WriteLocal);
    ASSERT_EQ(offset.address, stacks::Stack0OffsetRegister);
    ASSERT_EQ(offset.value, stacks::ImmediateStackStartOffsetBytes);
    ASSERT_EQ(trigger.type, SuperCommandType::WriteLocal);
    ASSERT_EQ(trigger.address, stacks::Stack0TriggerRegister);
    ASSERT_EQ(trigger.value, 1u << stacks::ImmediateShift);

    // Stack upload: StackStart, stack words, StackEnd written to consecutive
    // addresses of the immediate stack.
    std::vector<u32> stackWords;
    u16 expectedAddress = stacks::StackMemoryBegin + stacks::ImmediateStackStartOffsetBytes;

    for (size_t i = 1; i < cmds.size() - 2; ++i)
    {
        ASSERT_EQ(cmds[i].type, SuperCommandType::WriteLocal);
        ASSERT_EQ(cmds[i].address, expectedAddress);
        expectedAddress += AddressIncrement;
        stackWords.push_back(cmds[i].value);
    }

    ASSERT_GE(stackWords.size(), 2u);
    ASSERT_EQ(stackWords.front() >> stack_commands::CmdShift, static_cast<u32>(StackCommandType::StackStart));
    ASSERT_EQ((stackWords.front() >> stack_commands::CmdArg0Shift) & 0xffu, static_cast<u32>(Pipe::Command));
    ASSERT_EQ(stackWords.back() >> stack_commands::CmdShift, static_cast<u32>(StackCommandType::StackEnd));

    auto stack = stack_commands_from_buffer({ stackWords.begin() + 1, stackWords.end() - 1 });
    ASSERT_EQ(stack.size(), 5u);

    ASSERT_EQ(stack[0].type, StackCommand::CommandType::WriteMarker);
    ASSERT_EQ(stack[0].value, 0xcafe0001u);

    const std::vector<u32> expectedBytes = { 0x70, 0xcd, 0xab, area };

    for (size_t i = 0; i < expectedBytes.size(); ++i)
    {
        const auto &cmd = stack[i + 1];
        ASSERT_EQ(cmd.type, StackCommand::CommandType::VMEWrite);
        ASSERT_EQ(cmd.address, base + mvp::InputFifoRegister);
        ASSERT_EQ(cmd.value, expectedBytes[i]);
        ASSERT_EQ(cmd.amod, vme_amods::A32);
        ASSERT_EQ(cmd.dataWidth, VMEDataWidth::D16);
    }
}

TEST(mvlc_mvp, CheckResponse)
{
    const std::vector<u8> nop = { 0x00 };
    const std::vector<u8> efw = { 0x80, 0xcd, 0xab };

    ASSERT_TRUE(mvp::check_response(nop, { 0x00, 0xff, 0x01 }));
    ASSERT_TRUE(mvp::check_response(efw, { 0x80, 0xcd, 0xab, 0xff, 0x01 }));

    // Extra leading byte (MVLC).
    ASSERT_TRUE(mvp::check_response(efw, { 0x01, 0x80, 0xcd, 0xab, 0xff, 0x01 }));

    // First request byte missing (MDPP-32).
    ASSERT_TRUE(mvp::check_response(efw, { 0xcd, 0xab, 0xff, 0x01 }));

    ASSERT_FALSE(mvp::check_response(nop, {}));
    ASSERT_FALSE(mvp::check_response(nop, { 0x00 }));
    ASSERT_FALSE(mvp::check_response(efw, { 0x80, 0xff, 0x01 }));
    ASSERT_FALSE(mvp::check_response(efw, { 0x80, 0xcd, 0xac, 0xff, 0x01 }));
    ASSERT_FALSE(mvp::check_response(nop, { 0x00, 0xfe, 0x01 }));
    ASSERT_FALSE(mvp::check_response(nop, { 0x00, 0xff, 0x00 }));
}
