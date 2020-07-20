#include <regex>
#include <unordered_set>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <fmt/format.h>
#include <lyra/lyra.hpp>

#include "mini_daq_lib.h"

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    bool opt_printReadoutData = false;
    bool opt_printCrateConfig = false;
    std::string opt_listfileArchiveName;
    std::string opt_listfileMemberName;

    bool opt_showHelp = false;

    auto cli
        = lyra::help(opt_showHelp)

        // opt_printCrateConfig
        | lyra::opt(opt_printCrateConfig)
            ["--print-config"]("print the MVLC CrateConfig extracted from the listfile and exit")

        // logging
        | lyra::opt(opt_printReadoutData)
            ["--print-readout-data"]("log each word of readout data (very verbose!)")

        // positional args
        | lyra::arg(opt_listfileArchiveName, "listfile")
            ("listfile zip archive file").required()
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: "
            << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << cli << endl;

        cout
            << "The mini-daq-replay tool allows to replay MVLC readout data from listfiles" << endl
            << "created by the mesytec-mvlc library, e.g. the mini-daq tool or the mvme program." << endl << endl

            << "The only required argument is the name of the listfile zip archive to replay from." << endl
            << endl;
        return 0;
    }

    listfile::ZipReader zr;
    zr.openArchive(opt_listfileArchiveName);

    std::string entryName;

    if (!opt_listfileMemberName.empty())
        entryName = opt_listfileMemberName;
    else
    {
        // Try to find a listfile inside the archive.
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
    {
        cerr << "Could not find a mvlclst zip archive member to replay from." << endl;
        return 1;
    }

    auto &rh = *zr.openEntry(entryName);

    auto preamble = listfile::read_preamble(rh);

    if (!(preamble.magic == listfile::get_filemagic_eth()
          || preamble.magic == listfile::get_filemagic_usb()))
    {
        cerr << "Invalid file format (magic bytes do not match the MVLC formats)" << endl;
        return 1;
    }

    CrateConfig crateConfig = {};

    {
        auto configSection = preamble.findCrateConfig();

        if (!configSection)
        {
            cerr << "The listfile does not contain an MVLC CrateConfig (corrupted file or wrong format)" << endl;
            return 1;
        }

        try
        {
            crateConfig = crate_config_from_yaml(configSection->contentsToString());
        }
        catch (const std::runtime_error &e)
        {
            cerr << "Error parsing CrateConfig from listfile: " << e.what() << endl;
            return 1;
        }
    }

    if (opt_printCrateConfig)
    {
        cout << "CrateConfig found in " << opt_listfileArchiveName << ":"
            << entryName << ":" << endl;
        cout << to_yaml(crateConfig) << endl;
        return 0;
    }

    cout << "Starting replay from " << opt_listfileArchiveName << ":" << entryName << endl;

    //
    // readout parser
    //
    const size_t BufferSize = util::Megabytes(1);
    const size_t BufferCount = 100;
    ReadoutBufferQueues snoopQueues(BufferSize, BufferCount);

    readout_parser::ReadoutParserCallbacks parserCallbacks;

    parserCallbacks.beginEvent = [] (int eventIndex)
    {
    };

    parserCallbacks.groupPrefix = [opt_printReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
    {
        if (opt_printReadoutData)
        {
            util::log_buffer(
                std::cout, basic_string_view<u32>(data, size),
                fmt::format("prefix part: eventIndex={}, groupIndex={}", eventIndex, groupIndex));
        }
    };

    parserCallbacks.groupDynamic = [opt_printReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
    {
        if (opt_printReadoutData)
        {
            util::log_buffer(
                std::cout, basic_string_view<u32>(data, size),
                fmt::format("dynamic part: eventIndex={}, groupIndex={}", eventIndex, groupIndex));
        }
    };

    parserCallbacks.groupSuffix = [opt_printReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
    {
        if (opt_printReadoutData)
        {
            util::log_buffer(
                std::cout, basic_string_view<u32>(data, size),
                fmt::format("suffix part: eventIndex={}, groupIndex={}", eventIndex, groupIndex));
        }
    };

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
        // TODO: simplify the readout_parser stop sequence. maybe use an
        // atomic<bool> to tell the parser to quit.
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

    cout << "Replay from " << opt_listfileArchiveName << ":" << entryName << " done" << endl;

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
        readout_parser::dump_counters(cout, parserCounters.copy());
    }

    return 0;
}
