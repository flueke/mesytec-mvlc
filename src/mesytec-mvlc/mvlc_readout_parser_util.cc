#include "mvlc_readout_parser_util.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <iostream>
#include <fmt/format.h>

#include "util/logging.h"

using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

void run_readout_parser(
    readout_parser::ReadoutParserState &state,
    Protected<readout_parser::ReadoutParserCounters> &counters,
    ReadoutBufferQueues &bufferQueues,
    readout_parser::ReadoutParserCallbacks &parserCallbacks,
    std::atomic<bool> &quit)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"readout_parser",0,0,0);
#endif

    auto logger = get_logger("readout_parser");

    auto &filled = bufferQueues.filledBufferQueue();
    auto &empty = bufferQueues.emptyBufferQueue();

#ifndef NDEBUG
    const size_t countFilled = filled.size();
    const size_t countEmpty = empty.size();
#endif

    try
    {
        logger->debug("run_readout_parser() entering loop");

        while (true)
        {
            if (quit)
            {
                logger->info("run_readout_parser(): quit is set, leaving loop");
                break;
            }

            auto buffer = filled.dequeue(std::chrono::milliseconds(100), nullptr);

            if (!buffer)
            {
                logger->trace("run_readout_parser(): got a null buffer, skipping");
                continue;
            }

            if (buffer->empty())
            {
                logger->warn("run_readout_parser(): got an empty buffer, skipping");
                empty.enqueue(buffer);
                continue;
            }

            try
            {
                auto bufferView = buffer->viewU32();

                readout_parser::parse_readout_buffer(
                    static_cast<ConnectionType>(buffer->type()),
                    state,
                    parserCallbacks,
                    counters.access().ref(),
                    buffer->bufferNumber(),
                    bufferView.data(),
                    bufferView.size());

                empty.enqueue(buffer);
            }
            catch (...)
            {
                logger->warn("run_readout_parser(): caught an exception; rethrowing");
                empty.enqueue(buffer);
                throw;
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        state.eptr = std::current_exception();
        logger->error("run_readout_parser() caught a std::runtime_error: {}", e.what());
    }
    catch (...)
    {
        state.eptr = std::current_exception();
        logger->error("run_readout_parser caught an unknown exception");
    }

#ifndef NDEBUG
    assert(filled.size() + empty.size() == countFilled + countEmpty);
#endif

    logger->debug("run_readout_parser() left loop");
}

std::ostream &print_counters(std::ostream &out, const ReadoutParserCounters &counters)
{
    auto print_hits_and_sizes = [&out] (
        const std::string &partTitle,
        const ReadoutParserCounters::GroupPartHits &hits,
        const ReadoutParserCounters::GroupPartSizes &sizes)
    {
        if (!hits.empty())
        {
            out << "group " + partTitle + " hits: ";

            for (const auto &kv: hits)
            {
                out << fmt::format(
                    "eventIndex={}, groupIndex={}, hits={}; ",
                    kv.first.first, kv.first.second, kv.second);
            }

            out << endl;
        }

        if (!sizes.empty())
        {
            out << "group " + partTitle + " sizes: ";

            for (const auto &kv: sizes)
            {
                out << fmt::format(
                    "eventIndex={}, groupIndex={}, min={}, max={}, avg={}; ",
                    kv.first.first, kv.first.second,
                    kv.second.min,
                    kv.second.max,
                    kv.second.sum / static_cast<double>(hits.at(kv.first)));
            }

            out << endl;
        }
    };

    out << "internalBufferLoss=" << counters.internalBufferLoss << endl;
    out << "buffersProcessed=" << counters.buffersProcessed << endl;
    out << "unusedBytes=" << counters.unusedBytes << endl;
    out << "ethPacketsProcessed=" << counters.ethPacketsProcessed << endl;
    out << "ethPacketLoss=" << counters.ethPacketLoss << endl;

    for (size_t sysEvent = 0; sysEvent < counters.systemEvents.size(); ++sysEvent)
    {
        if (counters.systemEvents[sysEvent])
        {
            auto sysEventName = system_event_type_to_string(sysEvent);

            out << fmt::format("systemEventType {}, count={}",
                               sysEventName, counters.systemEvents[sysEvent])
                << endl;
        }
    }

    for (size_t pr=0; pr < counters.parseResults.size(); ++pr)
    {
        if (counters.parseResults[pr])
        {
            auto name = get_parse_result_name(static_cast<readout_parser::ParseResult>(pr));

            out << fmt::format("parseResult={}, count={}",
                               name, counters.parseResults[pr])
                << endl;
        }
    }

    out << "parserExceptions=" << counters.parserExceptions << endl;
    out << "emptyStackFrames=" << counters.emptyStackFrames << endl;

    out << "eventHits: ";
    for (const auto &kv: counters.eventHits)
        out << fmt::format("ei={}, hits={}, ", kv.first, kv.second);
    out << endl;

    print_hits_and_sizes("prefix", counters.groupHits, counters.groupSizes);

    return out;
}

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec
