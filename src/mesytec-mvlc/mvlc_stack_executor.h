#ifndef __MVLC_STACK_EXECUTOR_H__
#define __MVLC_STACK_EXECUTOR_H__

#include <cassert>
#include <chrono>
#include <iostream>
#include <functional>
#include <iterator>
#include <numeric>
#include <system_error>
#include <thread>
#include <iomanip>

#include "mvlc.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/string_view.hpp"
#include "vme_constants.h"

/* Utilities for direct command stack execution.
 *
 * execute_commands() and execute_stack() can make optimal use of the available
 * stack memory to execute an unlimited number of VME commands.
 */

namespace mesytec
{
namespace mvlc
{

struct MESYTEC_MVLC_EXPORT CommandExecResult
{
    StackCommand cmd;
    std::error_code ec;
    std::vector<u32> response;
};

struct MESYTEC_MVLC_EXPORT CommandExecOptions
{
    // Set to true to ignore any SoftwareDelay commands.
    bool ignoreDelays = false;

    // If disabled command execution will be aborted when a VME bus error is
    // encountered.
    bool continueOnVMEError = false;
};

MESYTEC_MVLC_EXPORT CommandExecResult run_command(
    MVLC &mvlc,
    const StackCommand &cmd,
    const CommandExecOptions &options = {});

MESYTEC_MVLC_EXPORT std::vector<CommandExecResult> run_commands(
    MVLC &mvlc,
    const std::vector<StackCommand> &commands,
    const CommandExecOptions &options = {});


inline std::vector<CommandExecResult> run_commands(
    MVLC &mvlc,
    const StackCommandBuilder &stackBuilder,
    const CommandExecOptions &options = {})
{
    return run_commands(mvlc, stackBuilder.getCommands(), options);
}


inline std::error_code get_first_error(const std::vector<CommandExecResult> results)
{
    auto it = std::find_if(
        std::begin(results), std::end(results),
        [] (const auto &result) { return static_cast<bool>(result.ec); });

    if (it != std::end(results))
        return it->ec;

    return {};
}

inline CommandExecResult get_first_error_result(const std::vector<CommandExecResult> results)
{
    auto it = std::find_if(
        std::begin(results), std::end(results),
        [] (const auto &result) { return static_cast<bool>(result.ec); });

    if (it != std::end(results))
        return *it;

    return {};
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_STACK_EXECUTOR_H__ */
