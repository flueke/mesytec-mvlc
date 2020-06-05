#include <regex>
#include <unordered_set>

#include <fmt/format.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "mini_daq_lib.h"

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    listfile::ZipReader zr;
    zr.openArchive(argv[1]);

    std::string entryName;

    if (argc >= 3)
        entryName = argv[2];
    else
    {
        auto entryNames = zr.entryNameList();

        auto it = std::find_if(
            std::begin(entryNames), std::end(entryNames),
            [] (const std::string &entryName)
            {
                static const std::regex re(R"foo(.+\.mvlclst(\.lz4)?)foo");
                return std::regex_search(entryName, re);
            });

        if (it != std::end(entryNames))
            entryName = *it;
    }

    if (entryName.empty())
        return 2;

    auto &rh = *zr.openEntry(entryName);

    auto preamble = listfile::read_preamble(rh);

    if (!(preamble.magic == listfile::get_filemagic_eth()
          || preamble.magic == listfile::get_filemagic_usb()))
        return 3;

    CrateConfig crateConfig = {};

    {
        auto it = std::find_if(
            std::begin(preamble.systemEvents), std::end(preamble.systemEvents),
            [] (const auto &sysEvent)
            {
                return sysEvent.type == system_event::subtype::MVLCCrateConfig;
            });

        if (it == std::end(preamble.systemEvents))
            return 4;

        crateConfig = crate_config_from_yaml(it->contentsToString());
    }

    //cout << "Read a CrateConfig from the listfile: " << endl;
    //cout << to_yaml(crateConfig) << endl;

    cout << "Preamble SystemEvent types:" << endl;
    for (const auto &systemEvent: preamble.systemEvents)
    {
        cout << "  " << system_event_type_to_string(systemEvent.type) << endl;
    }

    //cout << "Press the AnyKey to start the replay" << endl;
    //std::getc(stdin);

    //
    // readout parser
    //
    const size_t BufferSize = util::Megabytes(1);
    const size_t BufferCount = 100;
    ReadoutBufferQueues snoopQueues(BufferSize, BufferCount);

    mini_daq::MiniDAQStats stats = {};
    auto parserCallbacks = make_mini_daq_callbacks(stats);

    auto parserState = readout_parser::make_readout_parser(crateConfig.stacks);
    Protected<readout_parser::ReadoutParserCounters> parserCounters({});

    std::thread parserThread;

    parserThread = std::thread(
        readout_parser::run_readout_parser,
        std::ref(parserState),
        std::ref(parserCounters),
        std::ref(snoopQueues),
        std::ref(parserCallbacks));

    ReplayWorker replayWorker(snoopQueues, &rh);

    auto f = replayWorker.start();

    // wait until replay done
    replayWorker.waitableState().wait(
        [] (const ReplayWorker::State &state)
        {
            return state == ReplayWorker::State::Idle;
        });

    cout << "stopping readout_parser" << endl;

    // stop the readout parser
    if (parserThread.joinable())
    {
        cout << "waiting for empty snoopQueue buffer" << endl;
        auto sentinel = snoopQueues.emptyBufferQueue().dequeue(std::chrono::seconds(1));

        if (sentinel)
        {
            cout << "got a sentinel buffer for the readout parser" << endl;
            sentinel->clear();
            snoopQueues.filledBufferQueue().enqueue(sentinel);
            cout << "enqueued the sentinel buffer for the readout parser" << endl;
        }
        else
        {
            cout << "did not get an empty buffer from the snoopQueues" << endl;
        }

        parserThread.join();
    }

    //
    // replay stats
    //
    {
        auto counters = replayWorker.counters();

        auto tStart = counters.tStart;
        auto tEnd = (counters.state != ReplayWorker::State::Idle
                     ?  std::chrono::steady_clock::now()
                     : counters.tEnd);
        auto runDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
        double runSeconds = runDuration.count() / 1000.0;
        double megaBytes = counters.bytesRead * 1.0 / util::Megabytes(1);
        double mbs = megaBytes / runSeconds;

        cout << endl;
        cout << "---- replay stats ----" << endl;
        cout << "buffersRead=" << counters.buffersRead << endl;
        cout << "buffersFlushed=" << counters.buffersFlushed << endl;
        cout << "totalBytesTransferred=" << counters.bytesRead << endl;
        cout << "duration=" << runDuration.count() << " ms" << endl;
        cout << "rate=" << mbs << " MB/s" << endl;
    }

    //
    // parser stats
    //
    {
        auto counters = parserCounters.copy();

        cout << endl;
        cout << "---- readout parser stats ----" << endl;
        cout << "internalBufferLoss=" << counters.internalBufferLoss << endl;
        cout << "buffersProcessed=" << counters.buffersProcessed << endl;
        cout << "unusedBytes=" << counters.unusedBytes << endl;
        cout << "ethPacketLoss=" << counters.ethPacketLoss << endl;
        cout << "ethPacketsProcessed=" << counters.ethPacketsProcessed << endl;

        for (size_t sysEvent = 0; sysEvent < counters.systemEventTypes.size(); ++sysEvent)
        {
            if (counters.systemEventTypes[sysEvent])
            {
                auto sysEventName = system_event_type_to_string(sysEvent);

                cout << fmt::format("systemEventType {}, count={}",
                                    sysEventName, counters.systemEventTypes[sysEvent])
                    << endl;
            }
        }

        for (size_t pr=0; pr < counters.parseResults.size(); ++pr)
        {
            if (counters.parseResults[pr])
            {
                auto name = get_parse_result_name(static_cast<const readout_parser::ParseResult>(pr));

                cout << fmt::format("parseResult={}, count={}",
                                    name, counters.parseResults[pr])
                    << endl;
            }
        }

        cout << "parserExceptions=" << counters.parserExceptions << endl;

        dump_mini_daq_parser_stats(cout, stats);
    }

    return 0;
}
