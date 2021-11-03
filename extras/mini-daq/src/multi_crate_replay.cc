/*
 * - 1 thread for the eventbuilder and the (non-existent) analysis
 * - 1 thread per input listfile. Each runs the readout_parser and passes the
 *   event data to the eventbuilder
 * - The eventbuilder should know how many buffers arrived from each crate.
 *   Once a buffer for each crate has arrived attempt event building.
 */

#include <map>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::warn);
    bool opt_showHelp = false;
    std::string opt_listfilePath;
    bool opt_printCrateConfig = false;
    std::vector<std::string> listfilePaths;
    std::vector<int> multicrateEvents;

    auto cli
        = lyra::help(opt_showHelp)

        | lyra::opt(opt_printCrateConfig)
            ["--print-config"]("Print the MVLC CrateConfig extracted from the listfile and exit")

        | lyra::opt(listfilePaths, "listfile")
            .name("-l")
            ["--listfile"]("Listfiles to replay from. The first is assumed to be the main crate")
            .cardinality(1, 100)
        | lyra::opt(multicrateEvents, "event index")
            .name("--eb")
            ["--enable-eventbuilder"]("Enable the EventBuilder for the given zero based event index."
                                      "Required for events that span multiple crates. "
                                      "If not given event 0 is assumed as the only multicrate event"
                                      )
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

        cout << "write me!" << endl;
        return 0;
    }

    if (multicrateEvents.empty())
        multicrateEvents = { 0 };

    std::sort(std::begin(multicrateEvents), std::end(multicrateEvents));

    std::vector<CrateConfig> crateConfigs;

    for (const auto &listfilePath: listfilePaths)
    {
        listfile::ZipReader zr;
        zr.openArchive(listfilePath);
        auto lfh = zr.openEntry(zr.firstListfileEntryName());
        auto preamble = listfile::read_preamble(*lfh);
        auto crateConfig = crate_config_from_yaml(preamble.findCrateConfig()->contentsToString());
        crateConfigs.emplace_back(crateConfig);
    }

    std::vector<EventSetup> eventSetups;

    for (size_t ei = 0; ei < multicrateEvents.size(); ++ei)
    {
        EventSetup eventSetup;

        eventSetup.enabled = std::find(
            std::begin(multicrateEvents), std::end(multicrateEvents), ei)
            != std::end(multicrateEvents);

        if (!eventSetup.enabled)
            continue;

        //eventSetup.mainModule = std::make_pair(0, 0); // FIXME

        for (size_t ci = 0; ci < crateConfigs.size(); ++ci)
        {
            const auto &crateConfig = crateConfigs.at(ci);
            auto readoutStructure = readout_parser::build_readout_structure(
                crateConfig.stacks).at(ei);
            const int moduleCount = readoutStructure.size();
            cout << fmt::format("ei={}, ci={}, moduleCount={}", ei, ci, moduleCount) << endl << endl;
            EventSetup::CrateSetup crateSetup;

            for (int mi = 0; mi < moduleCount; ++mi)
            {
                crateSetup.moduleTimestampExtractors.emplace_back(
                    make_mesytec_default_timestamp_extractor());

                // FIXME
                crateSetup.moduleMatchWindows.emplace_back(event_builder::DefaultMatchWindow);
                //crateSetup.moduleMatchWindows.emplace_back(std::make_pair(0, 0));
            }

            eventSetup.crateSetups.emplace_back(crateSetup);
        }

        eventSetups.emplace_back(eventSetup);
    }

    EventBuilderConfig cfg;
    cfg.setups = eventSetups;
    EventBuilder eventBuilder(cfg);

	std::vector<MVLCReplay> replays;
    int crateIndex = 0;
    std::map<int, size_t> recordedSystemEvents;

	for (const auto &listfilePath: listfilePaths)
	{
        readout_parser::ReadoutParserCallbacks parserCallbacks;
        parserCallbacks.eventData = [crateIndex, &eventBuilder] (
            void *, int /*crateIndex*/, int eventIndex, const ModuleData *moduleData, size_t moduleCount)
        {
            // FIXME: swallows none eventbuilder events
            if (eventBuilder.isEnabledFor(eventIndex))
                eventBuilder.recordEventData(crateIndex, eventIndex, moduleData, moduleCount);
        };
        parserCallbacks.systemEvent = [crateIndex, &eventBuilder, &recordedSystemEvents] (
            void *, int /*crateIndex*/, const u32 *data, u32 size)
        {
            eventBuilder.recordSystemEvent(crateIndex, data, size);
            ++recordedSystemEvents[crateIndex]; // FIXME: this is not thread save and will be executd in parallel by each of the readout parsers
        };
        auto replay = make_mvlc_replay(listfilePath, parserCallbacks);
        replays.emplace_back(std::move(replay));
        ++crateIndex;
	}

    std::map<int, size_t> eventCounts;
    std::map<int, std::map<int, size_t>> moduleCounts;
    size_t systemEvents = 0;

    readout_parser::ReadoutParserCallbacks eventBuilderCallbacks;
    eventBuilderCallbacks.eventData = [&eventCounts, &moduleCounts] (
        void *, int /*crateIndex*/, int eventIndex, const ModuleData *moduleData, size_t moduleCount)
    {
        //cout << fmt::format("eb.eventData: ei={}, moduleCount={}", eventIndex, moduleCount) << endl;
        ++eventCounts[eventIndex];
        for (size_t mi=0; mi<moduleCount; ++mi)
        {
            if (moduleData[mi].data.size)
                ++moduleCounts[eventIndex][mi];
        }
    };
    eventBuilderCallbacks.systemEvent = [&eventBuilder, &systemEvents] (
        void *, int /*crateIndex*/, const u32 *data, u32 size)
    {
        //cout << fmt::format("eb.systemEvent: size={}", size) << endl;
        ++systemEvents;
    };

    auto run_event_builder = [] (
        EventBuilder &eventBuilder,
        readout_parser::ReadoutParserCallbacks &callbacks,
        std::atomic<bool> &quit)
    {
        while (!quit)
            eventBuilder.buildEvents(callbacks);

        // flush
        eventBuilder.buildEvents(callbacks, true);
    };

    std::atomic<bool> quitEventBuilder(false);

    std::thread eventBuilderThread(
        run_event_builder, std::ref(eventBuilder),
        std::ref(eventBuilderCallbacks),
        std::ref(quitEventBuilder));

    for (auto &replay: replays)
    {
        if (auto ec = replay.start())
            throw ec;
    }

    for (auto &replay: replays)
    {
        while (!replay.finished())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // FIXME: ugly threading problems with the MVLCReplay design/implementation:
    // MVLCReplay does not join() its internal thread when finished/stopped.
    // Instead the thread is joined when the object is destroyed.
    replays.clear();

    quitEventBuilder = true;

    eventBuilderThread.join();

    for (auto it=std::begin(eventCounts); it!=std::end(eventCounts); ++it)
        cout << fmt::format("ei={}, hits={}", it->first, it->second) << endl;

    cout << endl;

    for (auto it=std::begin(moduleCounts); it!=std::end(moduleCounts); ++it)
    {
        auto ei = it->first;
        auto modCounts = it->second;

        for (auto itt=std::begin(modCounts); itt!=std::end(modCounts); ++itt)
        {
            cout << fmt::format("ei={}, mi={}, hits={}", ei, itt->first, itt->second) << endl;
        }
    }

    cout << endl;

    for (auto it=std::begin(recordedSystemEvents); it!=std::end(recordedSystemEvents); ++it)
    {
        auto ci = it->first;
        cout << fmt::format("ci={}, systemEvents={}", ci, recordedSystemEvents[ci]) << endl;
    }

    cout << "yieldedSystemEvents=" << systemEvents << endl;

    cout << endl;

    auto ebCounters = eventBuilder.getCounters();

    for (size_t ei=0; ei<ebCounters.size(); ++ei)
    {
        auto &modCounters = ebCounters.at(ei);
        for (size_t mi=0; mi<modCounters.discardedEvents.size(); ++mi)
        {
            cout << fmt::format(
                "ei={}, mi={}, discard={}",
                ei, mi, modCounters.discardedEvents.at(mi)) << endl;
        }
    }

    return 0;
}
