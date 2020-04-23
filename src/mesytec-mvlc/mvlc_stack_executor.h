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

namespace mesytec
{
namespace mvlc
{

using namespace nonstd;

struct Result
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

struct CommandExecOptions
{
    bool ignoreDelays = false;
    bool noBatching  = false;
    bool contineOnVMEError = false;
};

//using AbortPredicate = std::function<bool (const std::error_code &ec)>;

namespace detail
{

template<typename DIALOG_API>
    std::error_code stack_transaction(
        DIALOG_API &mvlc, const std::vector<StackCommand> &commands,
        std::vector<u32> &responseDest)
{
    if (auto ec = mvlc.uploadStack(CommandPipe, 0, commands, responseDest))
        return ec;

    return mvlc.execImmediateStack(0, responseDest);
}

//template<typename DIALOG_API>
//    std::error_code stack_transaction(
//        DIALOG_API &mvlc, const StackCommandBuilder &stack,
//        std::vector<u32> &responseDest)
//{
//    return stack_transaction(mvlc, stack.getCommands(), responseDest);
//}

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
            std::cout << "run_part: delaying for " << part[0].value << " ms" << std::endl;
            std::chrono::milliseconds delay(part[0].value);
            std::this_thread::sleep_for(delay);
        }

        responseDest.resize(0);

        return {};
    }

    return stack_transaction(mvlc, part, responseDest);
}

template<typename DIALOG_API>
std::error_code run_parts(
    DIALOG_API &mvlc,
    const std::vector<std::vector<StackCommand>> &parts,
    const CommandExecOptions &options,
    //AbortPredicate abortPredicate,
    std::vector<u32> &combinedResponses)
{
    std::error_code ret;
    std::vector<u32> responseBuffer;

    for (const auto &part: parts)
    {
        auto ec = run_part(mvlc, part, options, responseBuffer);

        std::copy(std::begin(responseBuffer), std::end(responseBuffer),
                  std::back_inserter(combinedResponses));

        if (ec && !ret)
            ret = ec;

        //if (abortPredicate && abortPredicate(ec))
        //    break;
    }

    return ret;
}

#if 0
struct ParsedResponse
{
    std::error_code ec;
    std::vector<Result> results;
};

template<typename DIALOG_API>
    ParsedResponse execute_commands(
        DIALOG_API &mvlc, const std::vector<StackCommand> &commands)
{
    ParsedResponse pr;
    std::vector<u32> responseBuffer;

    pr.ec = stack_transaction(mvlc, commands, responseBuffer);

    if (pr.ec && pr.ec != ErrorType::VMEError)
        return pr;

    basic_string_view<u32> response(responseBuffer.data(), responseBuffer.size());

    for (const auto &cmd: commands)
    {
        using CT = StackCommand::CommandType;

        Result result(cmd);

        switch (cmd.type)
        {
            case CT::StackStart:
            case CT::StackEnd:
            case CT::SoftwareDelay:
                break;

            case CT::VMERead:
            case CT::VMEWrite:
            case CT::WriteSpecial:
            case CT::WriteMarker:
                break;
        }
    }

    return pr;
}
#endif

} // end namespace detail

template<typename DIALOG_API>
std::error_code execute_commands(
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
std::error_code execute_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stack,
    u16 immediateStackMaxSize,
    const CommandExecOptions &options,
    std::vector<u32> &responseBuffer)
{
    return execute_commands(mvlc, stack.getCommands(), immediateStackMaxSize, options, responseBuffer);
}

std::vector<Result> MESYTEC_MVLC_EXPORT parse_response_list(
    const std::vector<StackCommand> &commands, const std::vector<u32> &responseBuffer);

struct MESYTEC_MVLC_EXPORT StackGroupResults
{
    struct Group
    {
        std::string name;
        std::vector<Result> results;
    };

    std::vector<Group> groups;
};

StackGroupResults MESYTEC_MVLC_EXPORT parse_stack_exec_response(
    const StackCommandBuilder &stack, const std::vector<u32> &responseBuffer);

template<typename Out>
Out &output_hex_value(Out &out, u32 value)
{
    out << "0x" << std::hex << std::setw(8) << std::setfill('0')
        << value
        << std::dec << std::setw(0) << std::setfill(' ');

    return out;
}

template<typename Out>
Out &operator<<(Out &out, const StackGroupResults &groupedResults)
{
    for (const auto &group: groupedResults.groups)
    {
        out << group.name << ":" << std::endl;

        for (const auto &result: group.results)
        {
            out << "  " << to_string(result.cmd) << " -> ";

            if (result.ec)
            {
                out << "Error: " << result.ec.message();
            }
            else if (result.cmd.type == StackCommand::CommandType::VMERead)
            {
                if (!vme_amods::is_block_mode(result.cmd.amod)
                    && !result.response.empty())
                {
                    output_hex_value(out, result.response[0]) << std::endl;
                }
                else
                {
                    out << std::endl;

                    for (const u32 value: result.response)
                    {
                        out << "    ";
                        output_hex_value(out, value) << std::endl;
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
