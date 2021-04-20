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

using namespace nonstd;

struct MESYTEC_MVLC_EXPORT Result
{
    StackCommand cmd;
    std::error_code ec;
    std::vector<u32> response;
    bool isValid = false;

    explicit Result() {};

    explicit Result(const StackCommand &cmd_)
        : cmd(cmd_)
        , isValid(true)
    {}

    explicit Result(const std::error_code &ec_)
        : ec(ec_)
        , isValid(true)
    {}

    void clear() { isValid = false; }
    bool valid() const { return isValid; }
};

struct MESYTEC_MVLC_EXPORT GroupedStackResults
{
    struct Group
    {
        std::string name;
        std::vector<Result> results;
    };

    std::vector<Group> groups;
};

inline std::error_code get_first_error(const GroupedStackResults &groupResults)
{
    for (const auto &group: groupResults.groups)
    {
        for (const auto &result: group.results)
            if (result.ec)
                return result.ec;
    }

    return {};
}

inline std::error_code get_first_error(const std::vector<Result> &results)
{
    for (const auto &result: results)
        if (result.ec)
            return result.ec;
    return {};
}

template<typename C>
std::error_code get_first_error(const C &errors)
{
    for (auto ec: errors)
        if (ec)
            return ec;

    return {};
}

inline bool has_error(const GroupedStackResults &groupResults)
{
    return !get_first_error(groupResults);
}

struct CommandExecOptions
{
    // Set to true to ignore any SoftwareDelay commands.
    bool ignoreDelays = false;

    // Set to true to disable the command batching logic. Commands will be run
    // one at a time.
    bool noBatching  = true;

    // If disabled command execution will be aborted when a VME bus error is
    // encountered.
    bool continueOnVMEError = false;
};

namespace detail
{

template<typename DIALOG_API>
    std::error_code stack_transaction(
        DIALOG_API &mvlc, const std::vector<StackCommand> &commands,
        std::vector<u32> &responseDest)
{
#if 0
    if (auto ec = mvlc.uploadStack(CommandPipe, 0, commands, responseDest))
        return ec;

    return mvlc.execImmediateStack(0, responseDest);
#else
    return mvlc.stackTransaction(StackCommandBuilder(commands), responseDest);
#endif
}

inline bool is_sw_delay (const StackCommand &cmd)
{
    return cmd.type == StackCommand::CommandType::SoftwareDelay;
};

MESYTEC_MVLC_EXPORT std::vector<std::vector<StackCommand>> split_commands(
    const std::vector<StackCommand> &commands,
    const CommandExecOptions &options = {},
    const u16 immediateStackMaxSize = stacks::ImmediateStackReservedWords);

template<typename DIALOG_API>
std::error_code run_part(
    DIALOG_API &mvlc,
    const std::vector<StackCommand> &part,
    const CommandExecOptions &options,
    std::vector<u32> &responseDest)
{
    if (part.empty())
        throw std::runtime_error("empty command stack part");

    if (is_sw_delay(part[0]))
    {
        if (!options.ignoreDelays)
        {
            assert(part.size() == 1);

            std::cout << "run_part: delaying for " << part[0].value << " ms" << std::endl;
            std::chrono::milliseconds delay(part[0].value);
            std::this_thread::sleep_for(delay);
        }

        responseDest.resize(0);

        return {};
    }

    return stack_transaction(mvlc, part, responseDest);
}

/* Uploads and executes each of the stack command parts.
 *
 * This function assumes that each of the parts is small enough to not overflow
 * the reserved stack memory area (or the whole stack memory if it is
 * available). Use detail::split_commands() which performs the proper stack command
 * splitting.
 *
 * Returns a list of error codes, one for each part. Note that the return value
 * can be smaller that the number of parts if options.continueOnVMEError is
 * false.
 */
template<typename DIALOG_API>
std::vector<std::error_code> run_parts(
    DIALOG_API &mvlc,
    const std::vector<std::vector<StackCommand>> &parts,
    const CommandExecOptions &options,
    std::vector<u32> &combinedResponses)
{
    std::vector<std::error_code> ret;
    ret.reserve(parts.size());
    std::vector<u32> responseBuffer;

    for (const auto &part: parts)
    {
        auto ec = run_part(mvlc, part, options, responseBuffer);

        std::copy(std::begin(responseBuffer), std::end(responseBuffer),
                  std::back_inserter(combinedResponses));

        ret.push_back(ec);

        if (!options.continueOnVMEError && ec && ec == ErrorType::VMEError)
            break;
    }

    return ret;
}

template<typename Out>
Out &output_hex_value(Out &out, u32 value)
{
    out << "0x" << std::hex << std::setw(8) << std::setfill('0')
        << value
        << std::dec << std::setw(0) << std::setfill(' ');

    return out;
}

} // end namespace detail

template<typename DIALOG_API>
std::vector<std::error_code> execute_commands(
    DIALOG_API &mvlc,
    const std::vector<StackCommand> &commands,
    u16 immediateStackMaxSize,
    const CommandExecOptions &options,
    std::vector<u32> &responseBuffer)
{
    responseBuffer.clear();

    auto parts = detail::split_commands(commands, options, immediateStackMaxSize);

    if (parts.empty())
        return {};

    return detail::run_parts(mvlc, parts, options, responseBuffer);
}

template<typename DIALOG_API>
std::vector<std::error_code> execute_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stack,
    u16 immediateStackMaxSize,
    const CommandExecOptions &options,
    std::vector<u32> &responseBuffer)
{
    return execute_commands(mvlc, stack.getCommands(), immediateStackMaxSize, options, responseBuffer);
}

// Runs the stack using only the stack memory reserved for immediate stack
// execution and the default CommandExecOptions.
template<typename DIALOG_API>
std::vector<std::error_code> execute_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stack,
    std::vector<u32> &responseBuffer)
{
    return execute_stack(mvlc, stack, stacks::ImmediateStackReservedWords, CommandExecOptions{}, responseBuffer);
}

std::vector<Result> MESYTEC_MVLC_EXPORT parse_response_list(
    const std::vector<StackCommand> &commands, const std::vector<u32> &responseBuffer);

GroupedStackResults MESYTEC_MVLC_EXPORT parse_stack_exec_response(
    const StackCommandBuilder &stack,
    const std::vector<u32> &responseBuffer,
    const std::vector<std::error_code> &execErrors);

template<typename DIALOG_API>
GroupedStackResults execute_stack_and_parse_results(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stack,
    u16 immediateStackMaxSize = stacks::ImmediateStackReservedWords,
    const CommandExecOptions &options = {})
{
    std::vector<u32> responseBuffer;
    auto errors = execute_stack(mvlc, stack, immediateStackMaxSize, options, responseBuffer);
    return parse_stack_exec_response(stack, responseBuffer, errors);
}

template<typename Out>
Out &operator<<(Out &out, const GroupedStackResults &groupedResults)
{
    for (const auto &group: groupedResults.groups)
    {
        out << group.name << ":" << std::endl;

        for (const auto &result: group.results)
        {
            out << "  " << to_string(result.cmd) << " -> ";

            if (result.ec)
            {
                out << "Error: " << result.ec.message() << std::endl;
            }
            else if (result.cmd.type == StackCommand::CommandType::VMERead)
            {
                if (!vme_amods::is_block_mode(result.cmd.amod)
                    && !result.response.empty())
                {
                    detail::output_hex_value(out, result.response[0]) << std::endl;
                }
                else
                {
                    out << std::endl;

                    for (const u32 value: result.response)
                    {
                        out << "    ";
                        detail::output_hex_value(out, value) << std::endl;
                    }
                }
            }
            else
                out << "Ok" << std::endl;
        }
    }

    return out;
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_STACK_EXECUTOR_H__ */
