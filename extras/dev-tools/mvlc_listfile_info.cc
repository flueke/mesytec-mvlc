#include <iostream>
#include <lyra/lyra.hpp>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/spdlog.h>

using std::cerr;
using std::cout;
using namespace mesytec;
using mvlc::u32;

struct ProcessOptions
{
    bool printEventHeaders = false;
    bool printEventData = false;
};

bool process_listfile(const std::string &listfile, const ProcessOptions &options);

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    std::vector<std::string> arg_listfiles;
    ProcessOptions processOptions;

    auto cli
        = lyra::help(opt_showHelp)
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")
        | lyra::opt(processOptions.printEventHeaders)["--print-event-headers"]("print event headers")
        | lyra::opt(processOptions.printEventData)["--print-event-data"]("print event data (very verbose!)")
        | lyra::arg([&] (std::string arg) { arg_listfiles.emplace_back(arg); }, "zipped listfiles")
            ("zip listfiles").cardinality(1, 0xffff)
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << "\n";
        return 1;
    }

    if (opt_showHelp)
    {
        cout << "mvlc-listfile-info: Collect and display info about MVLC listfiles.\n"
             << cli << "\n";
        return 0;
    }

    mvlc::set_global_log_level(spdlog::level::info);

    if (opt_logDebug)
        mvlc::set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        mvlc::set_global_log_level(spdlog::level::trace);

    bool allGood = true;

    for (size_t i=0; i<arg_listfiles.size(); ++i)
    {
        const auto &listfile = arg_listfiles[i];
        cout << fmt::format("Processing listfile {}/{}: {}...\n", i+1, arg_listfiles.size(), listfile);
        allGood = allGood && process_listfile(listfile, processOptions);
    }

    return allGood ? 0 : 1;
}

bool process_listfile(const std::string &listfile, const ProcessOptions &options)
{
    mvlc::listfile::ZipReader zipReader;
    zipReader.openArchive(listfile);
    auto listfileEntryName = zipReader.firstListfileEntryName();

    if (listfileEntryName.empty())
    {
        std::cout << "Error: no listfile entry found in " << listfile << "\n";
        return false;
    }

    mvlc::listfile::ReadHandle *listfileReadHandle = {};

    try
    {
        listfileReadHandle = zipReader.openEntry(listfileEntryName);
    }
    catch (const std::exception &e)
    {
        std::cout << fmt::format("Error: could not open listfile entry {} for reading: {}\n", listfileEntryName, e.what());
        return false;
    }

    auto readerHelper = mvlc::listfile::make_listfile_reader_helper(listfileReadHandle);
    std::optional<mvlc::CrateConfig> crateConfig;
    std::optional<mvlc::readout_parser::ReadoutParserState> parserState;

    if (auto configEvent = readerHelper.preamble.findCrateConfig())
    {
        try
        {
            crateConfig = mvlc::crate_config_from_yaml(configEvent->contentsToString());
        }
        catch (const std::exception &e)
        {
            std::cout << fmt::format("  Error parsing MVLC CrateConfig from listfile: {}\n", e.what());
            return false;
        }

        try
        {
            parserState = mvlc::readout_parser::make_readout_parser(crateConfig.value().stacks);
        }
        catch (const std::exception &e)
        {
            std::cout << fmt::format("  Error creating readout_parser from MVLC CrateConfig: {}\n", e.what());
            return false;
        }
    }

    if (crateConfig && parserState)
    {
        std::cout << "Found MVLC CrateConfig containing the following readout structure:\n";

        for (size_t eventIndex=0; eventIndex<crateConfig.value().stacks.size(); ++eventIndex)
        {
            mvlc::stacks::Trigger trigger{};
            trigger.value = eventIndex < crateConfig->triggers.size() ? crateConfig->triggers[eventIndex] : 0;
            std::cout << fmt::format("  stack[{}], name={}, trigger={{{}}}:\n", eventIndex+1,
                crateConfig->stacks.at(eventIndex).getName(), mvlc::trigger_to_string(trigger));

            const auto &stack = crateConfig->stacks[eventIndex];
            const auto &eventStructure = parserState->readoutStructure[eventIndex];
            const auto stackGroups = stack.getGroups();


            for (size_t moduleIndex=0; moduleIndex<stackGroups.size(); ++moduleIndex)
            {
                auto &group = stackGroups[moduleIndex];
                auto &moduleStructure = eventStructure[moduleIndex];

                std::cout << fmt::format("    {}, moduleIndex={}, prefixLen={}, hasDynamic={:5}, suffixLen={}\n",
                    group.name, moduleIndex, moduleStructure.prefixLen, moduleStructure.hasDynamic, moduleStructure.suffixLen);

                for (const auto &cmd: group.commands)
                {
                    std::cout << "      " << mvlc::to_string(cmd) << "\n";
                }
            }
        }
        std::cout << "\n";
    }

    mvlc::readout_parser::ReadoutParserCounters parserCounters;
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks;

    if (parserState)
    {
        parserCallbacks.systemEvent = [&] (void *, int crateId, const u32 *header, u32 size)
        {
            assert(header);
            if (options.printEventHeaders)
                std::cout << fmt::format("    SystemEvent: crateId={}, header={:#010x}, {}\n", crateId, *header, mvlc::decode_frame_header(*header));
        };

        parserCallbacks.eventData = [&] (void *, int crateId, int eventIndex,
            const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            assert(moduleDataList);

            if (options.printEventHeaders || options.printEventData)
            {
                auto eventName = crateConfig->getEventName(eventIndex);
                std::cout << fmt::format("    ReadoutEvent: crateId={}, eventIndex={}, eventName={}, moduleCount={}\n", crateId, eventIndex, eventName, moduleCount);
            }

            if (options.printEventData)
            {
                for (size_t moduleIndex=0; moduleIndex<moduleCount; ++moduleIndex)
                {
                    auto moduleName = crateConfig->getModuleName(eventIndex, moduleIndex);
                    auto dataView = data_view(moduleDataList[moduleIndex]);
                    std::cout << fmt::format("        ModuleData: moduleIndex={}, moduleName={}, dataLen={}, data={:#010x}\n",
                        moduleIndex, moduleName, dataView.size(), fmt::join(dataView, ", "));
                }
            }
        };
    }

    std::cout << "  Processing listfile data...\n\n";
    auto tStart = std::chrono::steady_clock::now();
    size_t totalBytesRead = 0;
    size_t totalBuffersRead = 0;

    while (true)
    {
        readerHelper.destBuf().clear();
        auto buffer = read_next_buffer(readerHelper);

        if (!buffer->used())
            break;

        totalBytesRead += buffer->used();
        ++totalBuffersRead;

        if (parserState)
        {
            auto bufferView = buffer->viewU32();

            mvlc::readout_parser::parse_readout_buffer(
                readerHelper.bufferFormat,
                *parserState,
                parserCallbacks,
                parserCounters,
                totalBuffersRead,
                bufferView.data(), bufferView.size());
        }
    }

    auto elapsed = std::chrono::steady_clock::now() - tStart;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    auto mbPerSecond = (totalBytesRead / (1u << 20)) / (ms.count() / 1000.0);

    std::cout << fmt::format("  Read {} buffers, {} bytes in {} ms, {:.2f}MiB/s\n",
        totalBuffersRead, totalBytesRead, ms.count(), mbPerSecond
        );

    std::cout << fmt::format("  Final readout_parser counters:\n");
    std::cout << fmt::format("    buffersProcessed={}, unusedBytes={}, parserExceptions={}\n",
        parserCounters.buffersProcessed, parserCounters.unusedBytes, parserCounters.parserExceptions);

    auto eventIndexes = std::accumulate(std::begin(parserCounters.eventHits), std::end(parserCounters.eventHits),
        std::vector<int>(),
        [] (auto &&accu, const auto &iter) { accu.push_back(iter.first); return accu; });
    std::sort(std::begin(eventIndexes), std::end(eventIndexes));
    std::cout << fmt::format("    eventHits:\n");
    for (auto ei: eventIndexes)
    {
        auto eventName = crateConfig->getEventName(ei);
        std::cout << fmt::format("      eventIndex={}, eventName={}, hits={}\n", ei, eventName, parserCounters.eventHits[ei]);
    }

    auto moduleIndexPairs = std::accumulate(std::begin(parserCounters.groupHits), std::end(parserCounters.groupHits),
        std::vector<std::pair<int, int>>(),
        [] (auto &&accu, const auto &iter) { accu.push_back(iter.first); return accu; });
    std::sort(std::begin(moduleIndexPairs), std::end(moduleIndexPairs));
    std::cout << fmt::format("    moduleHits:\n");
    for (auto ip: moduleIndexPairs)
    {
        auto [eventIndex, moduleIndex] = ip;
        auto moduleHits = parserCounters.groupHits[ip];
        auto moduleSizes = parserCounters.groupSizes[ip];
        auto eventName = crateConfig->getEventName(eventIndex);
        auto moduleName = crateConfig->getModuleName(eventIndex, moduleIndex);

        std::cout << fmt::format("      eventIndex={}, moduleIndex={}, eventName={}, moduleName={}, hits={}, minSize={}, maxSize={}, avgSize={:.2f}\n",
            eventIndex, moduleIndex, eventName, moduleName, moduleHits, moduleSizes.min, moduleSizes.max,
            moduleSizes.sum / static_cast<double>(moduleHits));
    }

    return true;
}
