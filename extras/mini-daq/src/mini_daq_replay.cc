#include <regex>
#include <unordered_set>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

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

    parserCallbacks.eventData = [opt_printReadoutData] (
        void *, int eventIndex, const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
    {
        if (opt_printReadoutData)
        {
            for (u32 moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto &moduleData = moduleDataList[moduleIndex];

                if (moduleData.prefix.size)
                    util::log_buffer(
                        std::cout, basic_string_view<u32>(moduleData.prefix.data, moduleData.prefix.size),
                        fmt::format("prefix part: eventIndex={}, moduleIndex={}", eventIndex, moduleIndex));

                if (moduleData.dynamic.size)
                    util::log_buffer(
                        std::cout, basic_string_view<u32>(moduleData.dynamic.data, moduleData.dynamic.size),
                        fmt::format("dynamic part: eventIndex={}, moduleIndex={}", eventIndex, moduleIndex));

                if (moduleData.suffix.size)
                    util::log_buffer(
                        std::cout, basic_string_view<u32>(moduleData.suffix.data, moduleData.suffix.size),
                        fmt::format("suffix part: eventIndex={}, moduleIndex={}", eventIndex, moduleIndex));
            }
        }
    };

    parserCallbacks.systemEvent = [opt_printReadoutData] (void *, const u32 *header, u32 size)
    {
        if (opt_printReadoutData)
        {
            std::cout
                << "SystemEvent: type=" << system_event_type_to_string(
                    system_event::extract_subtype(*header))
                << ", size=" << size << ", bytes=" << (size * sizeof(u32))
                << endl;
        }
    };

    auto parserState = readout_parser::make_readout_parser(crateConfig.stacks);
    Protected<readout_parser::ReadoutParserCounters> parserCounters({});

    std::thread parserThread;
    std::atomic<bool> parserQuit(false);

    parserThread = std::thread(
        readout_parser::run_readout_parser,
        std::ref(parserState),
        std::ref(parserCounters),
        std::ref(snoopQueues),
        std::ref(parserCallbacks),
        std::ref(parserQuit)
        );

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
        parserQuit = true;
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
        readout_parser::print_counters(cout, parserCounters.copy());
    }

    return 0;
}
