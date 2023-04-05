#include <iostream>
#include <lyra/lyra.hpp>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/spdlog.h>

using std::cerr;
using std::cout;
using namespace mesytec;
using mvlc::u32;

bool process_listfile(const std::string &listfile);

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    std::vector<std::string> arg_listfiles;

    auto cli
        = lyra::help(opt_showHelp)
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")
        | lyra::arg([&] (std::string arg) { arg_listfiles.emplace_back(arg); }, "listfiles")
            ("zip listfiles").cardinality(1, 1000)
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
        allGood = allGood && process_listfile(listfile);
    }

    return allGood ? 0 : 1;
}

bool process_listfile(const std::string &listfile)
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

    if (auto configEvent = readerHelper.preamble.findCrateConfig())
    {
        try
        {
            crateConfig = mvlc::crate_config_from_yaml(configEvent->contentsToString());
            std::cout << "  Found CrateConfig containing the following readout stacks:\n";
            for (size_t i=0; i<crateConfig.value().stacks.size(); ++i)
            {
                const auto trigval = i < crateConfig.value().triggers.size() ? crateConfig.value().triggers[i] : 0;
                std::cout << fmt::format("    stack[{}], trigger={}:\n", i+1, mvlc::trigger_value_to_string(trigval));
                const auto &stack = crateConfig.value().stacks[i];
                for (const auto &group: stack.getGroups())
                {
                    std::cout << "      " << group.name << "\n";
                    for (const auto &cmd: group.commands)
                    {
                        std::cout << "        " << mvlc::to_string(cmd) << "\n";
                    }
                }
            }
            std::cout << "\n";
        }
        catch (const std::exception &e)
        {
            std::cout << fmt::format("  Error parsing MVLC CrateConfig found in listfile: {}\n", e.what());
        }
    }

    std::optional<mvlc::readout_parser::ReadoutParserState> parserState;
    mvlc::readout_parser::ReadoutParserCounters parserCounters;
    mvlc::readout_parser::ReadoutParserCallbacks parserCallbacks;

    if (crateConfig)
    {
        try
        {
            parserState = mvlc::readout_parser::make_readout_parser(crateConfig.value().stacks);
        }
        catch (const std::exception &e)
        {
            std::cout << fmt::format("  Error creating readout_parser from MVLC CrateConfig: {}\n", e.what());
        }
    }

    if (parserState)
    {
        std::cout << "  Readout structure parsed from CrateConfig:\n";
        for (size_t ei=0; ei<parserState->readoutStructure.size(); ++ei)
        {
            if (const auto &eventStructure = parserState->readoutStructure[ei];
                eventStructure.size())
            {
                std::cout << fmt::format("    eventIndex={}\n", ei);
                for (size_t mi=0; mi<eventStructure.size(); ++mi)
                {
                    const auto &moduleStructure = eventStructure[mi];
                    std::cout << fmt::format("      moduleIndex={}, prefixLen={}, hasDynamic={:5}, suffixLen={}, name={}\n",
                        mi, moduleStructure.prefixLen, moduleStructure.hasDynamic, moduleStructure.suffixLen,
                        moduleStructure.name);
                }
            }
        }
        std::cout << "\n";

        parserCallbacks.systemEvent = [] (void *, int, const u32 *header, u32 size)
        {
            assert(header);
            //std::cout << fmt::format("    SystemEvent: header={:#010x}, {}\n", *header, mvlc::decode_frame_header(*header));
        };

        parserCallbacks.eventData = [] (void *, int, int ei,
            const mvlc::readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            assert(moduleDataList);
            //std::cout << fmt::format("    ReadoutEvent: eventIndex={}, moduleCount={}\n", ei, moduleCount);
        };
    }

    std::cout << "  Processing listfile data...\n\n";
    auto tStart = std::chrono::steady_clock::now();
    size_t totalBytesRead = 0;
    size_t totalBuffersRead = 0;

    while (true)
    {
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
        std::cout << fmt::format("      eventIndex={}, hits={}\n", ei, parserCounters.eventHits[ei]);
    }

    auto moduleIndexPairs = std::accumulate(std::begin(parserCounters.groupHits), std::end(parserCounters.groupHits),
        std::vector<std::pair<int, int>>(),
        [] (auto &&accu, const auto &iter) { accu.push_back(iter.first); return accu; });
    std::sort(std::begin(moduleIndexPairs), std::end(moduleIndexPairs));
    std::cout << fmt::format("    moduleHits:\n");
    for (auto ip: moduleIndexPairs)
    {
        auto eventIndex = ip.first;
        auto moduleIndex = ip.second;
        auto moduleHits = parserCounters.groupHits[ip];
        auto moduleSizes = parserCounters.groupSizes[ip];

        std::cout << fmt::format("      eventIndex={}, moduleIndex={}, hits={}, minSize={}, maxSize={}, avgSize={:.2f}\n",
            eventIndex, moduleIndex, moduleHits, moduleSizes.min, moduleSizes.max,
            moduleSizes.sum / static_cast<double>(moduleHits));
    }

    return true;
}
