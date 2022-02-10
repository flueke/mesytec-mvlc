#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using namespace mesytec::mvlc;
using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    bool opt_printReadoutData = false;
    bool opt_printCrateConfig = false;
    std::string opt_listfileArchiveName;
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

    auto replay = make_mvlc_replay_blocking(opt_listfileArchiveName);

    if (opt_printCrateConfig)
    {
        cout << "CrateConfig found in " << opt_listfileArchiveName << ":" << endl;
        cout << to_yaml(replay.crateConfig()) << endl;
        return 0;
    }

    if (auto ec = replay.start())
        return 1;

    size_t numSystemEvents = 0;
    size_t numReadoutEvents = 0;
    std::unordered_map<u8, size_t> sysEventTypes;
    std::unordered_map<int, size_t> eventHits;

    while (auto event = next_event(replay))
    {
        if (event.type == EventContainer::Type::Readout)
        {
            if (opt_printReadoutData)
            {
                int eventIndex = event.readout.eventIndex;

                for (u32 moduleIndex = 0; moduleIndex < event.readout.moduleCount; ++moduleIndex)
                {
                    auto &moduleData = event.readout.moduleDataList[moduleIndex];

                    // moduleData.data.data points to the start of the modules readout data.
                    // moduleData.data.size specifies the length of the modules readout data.

                    if (moduleData.data.size)
                    {
                        util::log_buffer(
                            std::cout, basic_string_view<u32>(moduleData.data.data, moduleData.data.size),
                            fmt::format("module data: eventIndex={}, moduleIndex={}", eventIndex, moduleIndex));
                    }
                }
            }

            numReadoutEvents++;
            eventHits[event.readout.eventIndex] += 1;
        }
        else if (event.type == EventContainer::Type::System)
        {
            numSystemEvents++;
            u8 t = system_event::extract_subtype(*event.system.header);
            sysEventTypes[t] += 1u;
        }
    }

    spdlog::info("#systemEvents={}, #readoutEvents={}",
                 numSystemEvents, numReadoutEvents);

    for (const auto &kv: sysEventTypes)
    {
        u8 sysEvent = kv.first;
        size_t count = kv.second;
        auto sysEventName = system_event_type_to_string(sysEvent);
        spdlog::info("system event {}: {}", sysEventName, count);
    }

    for (const auto &kv: eventHits)
    {
        int eventIndex = kv.first;
        size_t count = kv.second;

        spdlog::info("hits for event {}: {}", eventIndex, count);
    }

    return 0;
}
