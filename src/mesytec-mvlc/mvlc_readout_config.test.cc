#include <iostream>

#include "gtest/gtest.h"
#include "mvlc_readout_config.h"
#include "vme_constants.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;

TEST(mvlc_readout_config, StackCommandBuilderYaml)
{
    StackCommandBuilder sb("myStack");
    sb.beginGroup("module0");
    sb.addVMEBlockRead(0x00000000u, vme_amods::MBLT64, (1u << 16)-1);
    sb.beginGroup("module1");
    sb.addVMEBlockRead(0x10000000u, vme_amods::MBLT64, (1u << 16)-1);
    sb.beginGroup("module2");
    sb.addVMEBlockReadSwapped(0x20000000u, vme_amods::MBLT64, (1u << 16)-1);
    sb.beginGroup("reset");
    sb.addVMEWrite(0xbb006070u, 1, vme_amods::A32, VMEDataWidth::D32);

    auto yamlText = to_yaml(sb);

    cout << yamlText << endl;

    auto sb1 = stack_command_builder_from_yaml(yamlText);

    ASSERT_EQ(sb, sb1);
}

TEST(mvlc_readout_config, CrateConfigYaml)
{
    CrateConfig cc;

    {
        cc.connectionType = ConnectionType::USB;
        cc.usbIndex = 42;
        cc.usbSerial = "1234";
        cc.ethHost = "example.com";
        cc.initRegisters.emplace_back(std::make_pair(0x1180, 500)); // TODO: add tests for this

        {
            StackCommandBuilder sb("event0");
            sb.beginGroup("module0");
            sb.addVMEBlockRead(0x00000000u, vme_amods::MBLT64, (1u << 16)-1);
            sb.beginGroup("module1");
            sb.addVMEBlockRead(0x10000000u, vme_amods::MBLT64, (1u << 16)-1);
            sb.beginGroup("module2");
            sb.addVMEBlockReadSwapped(0x20000000u, vme_amods::MBLT64, (1u << 16)-1);
            sb.beginGroup("reset");
            sb.addVMEWrite(0xbb006070u, 1, vme_amods::A32, VMEDataWidth::D32);

            cc.stacks.emplace_back(sb);
        }

        {
            u8 irqLevel = 1;
            u32 triggerVal = stacks::TriggerType::IRQNoIACK << stacks::TriggerTypeShift;
            triggerVal |= (irqLevel - 1) & stacks::TriggerBitsMask;

            cc.triggers.push_back(triggerVal);
        }

        ASSERT_EQ(cc.stacks.size(), 1u);
        ASSERT_EQ(cc.stacks.size(), cc.triggers.size());
        cout << to_yaml(cc) << endl;
    }

    {
        auto yString = to_yaml(cc);
        auto cc2 = crate_config_from_yaml(yString);

        cout << to_yaml(cc2) << endl;

        ASSERT_EQ(cc, cc2);
    }
}
