#include <regex>
#include <unordered_set>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

// Context structure passed to the readout parser callbacks. Stores the
// CrateConfig object read from the input listfile. Used to map event and module
// indexes to names.
struct ReadoutParserUserContext
{
    CrateConfig crateConfig;
};

int main(int argc, char *argv[])
{
    bool opt_printReadoutData = false;
    bool opt_printCrateConfig = false;
    std::string opt_listfileArchiveName;
    std::string opt_listfileMemberName;

    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;

    auto cli
        = lyra::help(opt_showHelp)

        // opt_printCrateConfig
        | lyra::opt(opt_printCrateConfig)
            ["--print-config"]("print the MVLC CrateConfig extracted from the listfile and exit")

        // logging
        | lyra::opt(opt_printReadoutData)
            ["--print-readout-data"]("log each word of readout data (very verbose!)")
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")

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

    // logging setup
    if (opt_logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (opt_logTrace)
        spdlog::set_level(spdlog::level::trace);

    // readout parser callbacks
    readout_parser::ReadoutParserCallbacks parserCallbacks;
    size_t nSystems = 0;
    size_t nReadouts = 0;

    parserCallbacks.eventData = [opt_printReadoutData, &nReadouts] (
        void *ctx_, int /*crateIndex*/, int eventIndex, const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
    {
        auto *ctx = static_cast<ReadoutParserUserContext *>(ctx_);

        if (opt_printReadoutData)
        {
            // Get the readout stack that was used for this event.
            auto eventReadoutCommands = ctx->crateConfig.stacks.at(eventIndex);

            for (u32 moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                // Get the command group for the module index. This contains the
                // module readout commands and additional meta information.
                auto moduleGroup = eventReadoutCommands.getGroup(moduleIndex);
                auto &moduleData = moduleDataList[moduleIndex];

                if (moduleData.data.size)
                    util::log_buffer(
                        std::cout, basic_string_view<u32>(moduleData.data.data, moduleData.data.size),
                        fmt::format("module data: eventIndex={}, moduleIndex={}, eventName={}, moduleName={}, moduleMeta={}",
                            eventIndex, moduleIndex, eventReadoutCommands.getName(), moduleGroup.getName(), fmt::join(moduleGroup.meta, ", ")
                        ));
            }
        }

        ++nReadouts;
    };

    parserCallbacks.systemEvent = [opt_printReadoutData, &nSystems] (void *, int /*crateIndex*/, const u32 *header, u32 size)
    {
        if (opt_printReadoutData)
        {
            std::cout
                << "SystemEvent: type=" << system_event_type_to_string(
                    system_event::extract_subtype(*header))
                << ", size=" << size << ", bytes=" << (size * sizeof(u32))
                << endl;
        }

        ++nSystems;
    };

    ReadoutParserUserContext parserUserContext;
    auto replay = make_mvlc_replay(
        opt_listfileArchiveName,
        opt_listfileMemberName,
        parserCallbacks,
        &parserUserContext);

    parserUserContext.crateConfig = replay.crateConfig();


    if (opt_printCrateConfig)
    {
        cout << "CrateConfig found in " << opt_listfileArchiveName << ":" << endl;
        cout << to_yaml(replay.crateConfig()) << endl;
        return 0;
    }

    cout << "Starting replay from " << opt_listfileArchiveName << "..." << endl;

    if (auto ec = replay.start())
    {
        cerr << "Error starting replay: " << ec.message() << endl;
        return 1;
    }

    util::Stopwatch swReport;
    while (!replay.finished())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (swReport.get_interval() > std::chrono::seconds(1))
        {
            cout << "---- readout parser stats ----" << endl;
            readout_parser::print_counters(cout, replay.parserCounters());
            swReport.interval();
        }
    }

    //
    // replay stats
    //
    {
        auto counters = replay.workerCounters();

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
        auto counters = replay.parserCounters();

        cout << endl;
        cout << "---- final readout parser stats ----" << endl;
        readout_parser::print_counters(cout, counters);
    }

    spdlog::info("nSystems={}, nReadouts={}",
                 nSystems, nReadouts);

    return 0;
}
