#include <gtest/gtest.h>
#include "mvlc_constants.h"
#include "mvlc_stack_errors.h"

using namespace mesytec::mvlc;

TEST(stack_errors, StackErrorsSystemEventEmpty)
{
    StackErrors inputStackErrors = {};

    auto sysEventData = stack_errors_to_sysevent_data(inputStackErrors);
    auto parsedStackErrors = decode_stack_errors_sysevent_data(sysEventData);

    ASSERT_TRUE(sysEventData.empty());
    ASSERT_EQ(inputStackErrors, parsedStackErrors);
}

TEST(stack_errors, StackErrorsSystemEventData)
{
    // single error, no special case
    {
        StackErrors inputStackErrors = {};

        // Stack 0, line 23, flags=timeout, count=42
        StackErrorInfo errorInfo{23, frame_flags::Timeout};
        inputStackErrors[0][errorInfo] = 42;

        auto sysEventData = stack_errors_to_sysevent_data(inputStackErrors);
        auto parsedStackErrors = decode_stack_errors_sysevent_data(sysEventData);

        ASSERT_EQ(sysEventData.size(), 1u);
        ASSERT_EQ(parsedStackErrors[0][errorInfo], 42);
        ASSERT_EQ(inputStackErrors, parsedStackErrors);
    }

    // two errors, same stack and location but differing flags
    // additionally an entry with a zero error count
    {
        StackErrors inputStackErrors = {};

        // Stack 1, line 23, flags=timeout, count=42
        StackErrorInfo errorInfo{23, frame_flags::Timeout};
        inputStackErrors[1][errorInfo] = 42;

        // Stack 1, line 23, flags=timeout|berr, count=111
        errorInfo = {23, frame_flags::Timeout | frame_flags::BusError };
        inputStackErrors[1][errorInfo] = 111;

        // Stack 2, line 23, flags=timeout|berr, count=0
        errorInfo = {23, frame_flags::Timeout | frame_flags::BusError };
        inputStackErrors[2][errorInfo] = 0;

        auto sysEventData = stack_errors_to_sysevent_data(inputStackErrors);
        auto parsedStackErrors = decode_stack_errors_sysevent_data(sysEventData);

        ASSERT_EQ(sysEventData.size(), 2u);

        errorInfo = {23, frame_flags::Timeout};
        ASSERT_EQ(parsedStackErrors[1][errorInfo], 42);

        errorInfo = {23, frame_flags::Timeout | frame_flags::BusError};
        ASSERT_EQ(parsedStackErrors[1][errorInfo], 111);

        // The zero error count entry is not present in the parsed structure.
        ASSERT_NE(inputStackErrors, parsedStackErrors);
    }

    // error count overflow handling
    {
        StackErrors inputStackErrors = {};

        // Stack 7, line 255, flags=timeout|berr|syntax, count=150000
        StackErrorInfo errorInfo{255,
            frame_flags::Timeout|frame_flags::BusError|frame_flags::SyntaxError};
        inputStackErrors[7][errorInfo] = 150000;

        auto sysEventData = stack_errors_to_sysevent_data(inputStackErrors);
        auto parsedStackErrors = decode_stack_errors_sysevent_data(sysEventData);

        ASSERT_EQ(sysEventData.size(), 1);

        ASSERT_EQ(parsedStackErrors[7][errorInfo], 0xffff);
    }
}
