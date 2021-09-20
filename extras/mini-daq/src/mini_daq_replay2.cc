#include "mesytec-mvlc/mvlc_constants.h"
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
    auto libLoggers = mesytec::mvlc::setup_loggers();

    if (opt_logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (opt_logTrace)
        spdlog::set_level(spdlog::level::trace);

    // readout parser callbacks
    readout_parser::ReadoutParserCallbacks parserCallbacks;

    parserCallbacks.eventData = [opt_printReadoutData] (
        int eventIndex, const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
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

    parserCallbacks.systemEvent = [opt_printReadoutData] (const u32 *header, u32 size)
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

    auto replay = make_mvlc_replay(
        opt_listfileArchiveName,
        opt_listfileMemberName,
        parserCallbacks);


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

    while (replay.state() != ReplayWorker::State::Idle)
    {
        replay.waitableState().wait_for(
            std::chrono::milliseconds(1000),
            [] (const ReplayWorker::State &state)
            {
                return state == ReplayWorker::State::Idle;
            });
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
        cout << "---- readout parser stats ----" << endl;
        readout_parser::print_counters(cout, counters);
    }

    return 0;
}
