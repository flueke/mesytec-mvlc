#include "mvlc_stack_executor.h"

#include <spdlog/spdlog.h>

#include "mvlc_constants.h"
#include "mvlc_readout_parser.h"
#include "vme_constants.h"


namespace mesytec
{
namespace mvlc
{

CommandExecResult run_command(
    MVLC &mvlc,
    const StackCommand &cmd,
    const CommandExecOptions &options)
{
    CommandExecResult result = {};
    result.cmd = cmd;

    using CT = StackCommand::CommandType;

    switch (cmd.type)
    {
        case CT::Invalid:
        case CT::StackStart:
        case CT::StackEnd:
        case CT::WriteMarker:
        case CT::WriteSpecial:
        case CT::Wait:
        case CT::SignalAccu:
        case CT::MaskShiftAccu:
        case CT::SetAccu:
        case CT::ReadToAccu:
        case CT::CompareLoopAccu:
        case CT::Custom:
            return result;

        case CT::SoftwareDelay:
            if (!options.ignoreDelays)
            {
                std::chrono::milliseconds delay(cmd.value);
                std::this_thread::sleep_for(delay);
            }
            break;

        case CT::VMERead:
        case CT::VMEReadMem:
            if (!vme_amods::is_block_mode(cmd.amod))
            {
                u32 value = 0;

                result.ec = mvlc.vmeRead(
                    cmd.address, value, cmd.amod, cmd.dataWidth);

                if (cmd.dataWidth == VMEDataWidth::D16)
                    value &= 0xffffu;

                result.response.push_back(value);
            }
            else if (vme_amods::is_esst64_mode(cmd.amod))
            {
                bool fifo = cmd.type == CT::VMERead;
                result.ec = mvlc.vmeBlockRead(
                    cmd.address, cmd.rate, cmd.transfers, result.response, fifo);
            }
            else
            {
                bool fifo = cmd.type == CT::VMERead;
                result.ec = mvlc.vmeBlockRead(
                    cmd.address, cmd.amod, cmd.transfers, result.response, fifo);
            }
            break;


        case CT::VMEReadSwapped:
        case CT::VMEReadMemSwapped:
            if (!vme_amods::is_block_mode(cmd.amod))
            {
                u32 value = 0;

                result.ec = mvlc.vmeRead(
                    cmd.address, value, cmd.amod, cmd.dataWidth);

                if (cmd.dataWidth == VMEDataWidth::D16)
                    value &= 0xffffu;

                result.response.push_back(value);
            }
            else if (vme_amods::is_esst64_mode(cmd.amod))
            {
                bool fifo = cmd.type == CT::VMEReadSwapped;
                result.ec = mvlc.vmeBlockReadSwapped(
                    cmd.address, cmd.rate, cmd.transfers, result.response, fifo);
            }
            else
            {
                bool fifo = cmd.type == CT::VMEReadSwapped;
                result.ec = mvlc.vmeBlockReadSwapped(
                    cmd.address, cmd.amod, cmd.transfers, result.response, fifo);
            }
            break;

        case CT::VMEWrite:
            result.ec = mvlc.vmeWrite(
                cmd.address, cmd.value, cmd.amod, cmd.dataWidth);
            break;
    }

    spdlog::trace("run_command: cmd={}, ec={}", to_string(cmd), result.ec.message());

    return result;
}

std::vector<CommandExecResult> run_commands(
    MVLC &mvlc,
    const std::vector<StackCommand> &commands,
    const CommandExecOptions &options)

{
    std::vector<CommandExecResult> results;
    results.reserve(commands.size());

    for (const auto &cmd: commands)
    {
        auto result = run_command(mvlc, cmd, options);

        results.push_back(result);

        if (result.ec)
        {
            if (result.ec != ErrorType::VMEError || !options.continueOnVMEError)
                break;
        }
    }

    return results;
}

} // end namespace mvlc
} // end namespace mesytec
