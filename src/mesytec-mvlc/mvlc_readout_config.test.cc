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

    //cout << yamlText << endl;

    auto sb1 = stack_command_builder_from_yaml(yamlText);

    ASSERT_EQ(sb, sb1); // depends on StackCommandBuilder::operator==() being correct!
}

TEST(mvlc_readout_config, StackCommandBuilderJson)
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

    auto jsonText = to_json(sb);

    //cout << jsonText << endl;

    auto sb1 = stack_command_builder_from_json(jsonText);

    ASSERT_EQ(sb, sb1); // depends on StackCommandBuilder::operator==() being correct!
}

TEST(mvlc_readout_config, CrateConfigYaml)
{
    CrateConfig cc;

    {
        cc.connectionType = ConnectionType::USB;
        cc.usbIndex = 42;
        cc.usbSerial = "1234";
        cc.ethHost = "example.com";
        cc.initRegisters.emplace_back(std::make_pair(0x1180, 500));

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
            stacks::Trigger trigger{};
            trigger.type = stacks::TriggerType::IRQNoIACK;
            trigger.subtype = stacks::TriggerSubtype::IRQ1;
            cc.triggers.push_back(trigger.value);
        }

        ASSERT_EQ(cc.stacks.size(), 1u);
        ASSERT_EQ(cc.stacks.size(), cc.triggers.size());
        //cout << to_yaml(cc) << endl;
    }

    {
        auto yString = to_yaml(cc);
        auto cc2 = crate_config_from_yaml(yString);

        //cout << to_yaml(cc2) << endl;

        ASSERT_EQ(cc, cc2); // depends on CrateConfig::operator==() being correct!
    }
}

TEST(mvlc_readout_config, CrateConfigJson)
{
    CrateConfig cc;

    {
        cc.connectionType = ConnectionType::USB;
        cc.usbIndex = 42;
        cc.usbSerial = "1234";
        cc.ethHost = "example.com";
        cc.initRegisters.emplace_back(std::make_pair(0x1180, 500));

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
            stacks::Trigger trigger{};
            trigger.type = stacks::TriggerType::IRQNoIACK;
            trigger.subtype = stacks::TriggerSubtype::IRQ1;
            cc.triggers.push_back(trigger.value);
        }

        ASSERT_EQ(cc.stacks.size(), 1u);
        ASSERT_EQ(cc.stacks.size(), cc.triggers.size());
        //cout << to_json(cc) << endl;
    }

    {
        auto yString = to_json(cc);
        auto cc2 = crate_config_from_json(yString);

        //cout << to_json(cc2) << endl;

        ASSERT_EQ(cc, cc2); // depends on CrateConfig::operator==() being correct!
    }
}
