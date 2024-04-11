#include "gtest/gtest.h"

#include "mvlc_util.h"

using namespace mesytec::mvlc;

TEST(mvlc_util, TriggerValues)
{
    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::IRQNoIACK;
        trigger.subtype = stacks::TriggerSubtype::IRQ7;

        u16 expectedValue = ((stacks::TriggerType::IRQNoIACK << stacks::TriggerTypeShift) |
                            (stacks::TriggerSubtype::IRQ7 << stacks::TriggerBitsShift));
        ASSERT_EQ(trigger.value, expectedValue);
    }

    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::NoTrigger;
        trigger.subtype = stacks::TriggerSubtype::IRQ7;
        trigger.immediate = true;

        u16 expectedValue = ((stacks::TriggerType::NoTrigger << stacks::TriggerTypeShift) |
                             (stacks::TriggerSubtype::IRQ7 << stacks::TriggerBitsShift) |
                             (true << stacks::ImmediateShift));
        ASSERT_EQ(trigger.value, expectedValue);
    }

    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::External;
        trigger.subtype = stacks::TriggerSubtype::Timer1;

        u16 expectedValue = ((stacks::TriggerType::External << stacks::TriggerTypeShift) |
                            (stacks::TriggerSubtype::Timer1 << stacks::TriggerBitsShift));
        ASSERT_EQ(trigger.value, expectedValue);
    }
}

TEST(mvlc_util, TriggerToString)
{
    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::IRQNoIACK;
        trigger.subtype = stacks::TriggerSubtype::IRQ7;

        auto str = trigger_to_string(trigger);
        ASSERT_EQ(str, "type=IrqNoIack, subtype=IRQ7");
    }

    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::NoTrigger;
        trigger.subtype = stacks::TriggerSubtype::IRQ7;
        trigger.immediate = true;

        auto str = trigger_to_string(trigger);
        ASSERT_EQ(str, "type=NoTrigger, subtype=IRQ7, immediate=true");
    }

    {
        stacks::Trigger trigger{};
        trigger.type = stacks::TriggerType::External;
        trigger.subtype = stacks::TriggerSubtype::Timer1;

        auto str = trigger_to_string(trigger);
        ASSERT_EQ(str, "type=TriggerIO, subtype=Timer1");
    }
}
