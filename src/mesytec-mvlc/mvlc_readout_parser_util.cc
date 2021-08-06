#include "mvlc_readout_parser_util.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <iostream>
#include <fmt/format.h>

using std::cerr;
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
    ReadoutBufferQueues &snoopQueues,
    readout_parser::ReadoutParserCallbacks &parserCallbacks)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"readout_parser",0,0,0);
#endif

    try
    {
        cerr << "run_readout_parser entering loop" << endl;

        auto &filled = snoopQueues.filledBufferQueue();
        auto &empty = snoopQueues.emptyBufferQueue();

        while (true)
        {
            auto buffer = filled.dequeue(std::chrono::milliseconds(100));

            if (buffer && buffer->empty()) // sentinel
                break;
            else if (!buffer)
                continue;

            try
            {
                //cout << "buffer #" << buffer->bufferNumber() << endl;

                auto bufferView = buffer->viewU32();

                readout_parser::parse_readout_buffer(
                    buffer->type(),
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
                empty.enqueue(buffer);
                throw;
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        {
            //auto state = protectedState.access();
            //state->eptr = std::current_exception();
        }

        cerr << "readout_parser caught a std::runtime_error: " << e.what() << endl;
    }
    catch (...)
    {
        {
            //auto state = protectedState.access();
            //state->eptr = std::current_exception();
        }

        cerr << "readout_parser caught an unknown exception." << endl;
    }

    cerr << "run_readout_parser left loop" << endl;
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

    print_hits_and_sizes("prefix", counters.groupPrefixHits, counters.groupPrefixSizes);
    print_hits_and_sizes("dynamic", counters.groupDynamicHits, counters.groupDynamicSizes);
    print_hits_and_sizes("suffix", counters.groupSuffixHits, counters.groupSuffixSizes);

    return out;
}

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec
