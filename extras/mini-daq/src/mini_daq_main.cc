#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <fstream>

#ifdef __WIN32
#include <stdlib.h> // system()
#endif

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

struct MiniDaqCountersSnapshot
{
    StackErrorCounters mvlcStackErrors;
    ReadoutWorker::Counters readoutWorkerCounters;
    readout_parser::ReadoutParserCounters parserCounters;
};

StackErrorCounters delta_counters(const StackErrorCounters &prev, const StackErrorCounters &curr)
{
    StackErrorCounters result;

    std::transform(std::begin(prev.stackErrors), std::end(prev.stackErrors),
                   std::begin(curr.stackErrors),
                   std::begin(result.stackErrors),
                   util::delta_map<ErrorInfoCounts>);

    result.nonErrorFrames = util::calc_delta0(curr.nonErrorFrames, prev.nonErrorFrames);
    result.nonErrorHeaderCounts = util::delta_map(prev.nonErrorHeaderCounts, curr.nonErrorHeaderCounts);

    return result;
}

#define CALC_DELTA0(member) result.member = util::calc_delta0(curr.member, prev.member)

ReadoutWorker::Counters delta_counters(const ReadoutWorker::Counters &prev, const ReadoutWorker::Counters &curr)
{
    ReadoutWorker::Counters result;

    CALC_DELTA0(buffersRead);
    CALC_DELTA0(buffersFlushed);
    CALC_DELTA0(bytesRead);
    CALC_DELTA0(snoopMissedBuffers);
    CALC_DELTA0(usbFramingErrors);
    CALC_DELTA0(usbTempMovedBytes);
    CALC_DELTA0(ethShortReads);
    CALC_DELTA0(readTimeouts);

    std::transform(std::begin(prev.stackHits), std::end(prev.stackHits),
                   std::begin(curr.stackHits),
                   std::begin(result.stackHits),
                   util::calc_delta0<size_t>);

    // TODO eth::PipeStats
    // TODO ListfileWriterCounters

    return result;
}

MiniDaqCountersSnapshot delta_counters(const MiniDaqCountersSnapshot &prev, const MiniDaqCountersSnapshot &curr)
{
    MiniDaqCountersSnapshot result;

    result.mvlcStackErrors = delta_counters(prev.mvlcStackErrors, curr.mvlcStackErrors);
    result.readoutWorkerCounters = delta_counters(prev.readoutWorkerCounters, curr.readoutWorkerCounters);
    // TODO: result.parserCounters = delta_counters(prev.parserCounters, curr.parserCounters);

    return result;
}

#undef CALC_DELTA0


struct MiniDaqCountersUpdate
{
    MiniDaqCountersSnapshot prev;
    MiniDaqCountersSnapshot curr;
    std::chrono::milliseconds dt;
};

void dump_counters2(
    std::ostream &out,
    const MiniDaqCountersSnapshot &prev,
    const MiniDaqCountersSnapshot &curr,
    const std::chrono::milliseconds &dt)
{
    auto delta = delta_counters(prev, curr);

    out << fmt::format("dt={} ms, dBytesRead={} B, {} MiB, readRate={} B/s, {}MiB/s\n",
                       dt.count(),
                       delta.readoutWorkerCounters.bytesRead,
                       delta.readoutWorkerCounters.bytesRead / 1024.0 / 1024.0,
                       static_cast<double>(delta.readoutWorkerCounters.bytesRead) / (dt.count() / 1000.0),
                       static_cast<double>(delta.readoutWorkerCounters.bytesRead) / 1024.0 / 1024.0 / (dt.count() / 1000.0));
}

void update_counters(MiniDaqCountersUpdate &counters, MVLCReadout &rdo, std::chrono::milliseconds dt)
{
    counters.prev = counters.curr;
    auto &mvlc = rdo.readoutWorker().mvlc();
    counters.curr.mvlcStackErrors = mvlc.getStackErrorCounters();
    counters.curr.readoutWorkerCounters = rdo.workerCounters();
    counters.curr.parserCounters = rdo.parserCounters();
    counters.dt = dt;
}

void dump_counters(
    std::ostream &out,
    const ConnectionType &connectionType,
    const StackErrorCounters &stackErrors,
    const ReadoutWorker::Counters &readoutWorkerCounters,
    const readout_parser::ReadoutParserCounters &parserCounters)
{
#if 0
// clear screen hacks
#ifdef __WIN32
    system("cls");
#else
    out << "\e[1;1H\e[2J";
#endif
#endif
    //
    // readout stats
    //
    {
        auto &counters = readoutWorkerCounters;

        auto tStart = counters.tStart;
        // Note: if idle this uses the tTerminateStart time point. This means
        // the duration from counters.tStart to counters.tTerminateStart is
        // used as the whole duration of the DAQ. This still does not correctly
        // reflect the actual data rate because it does contains the
        // bytes/packets/etc that where read during the terminate phase. It
        // should be closer than using tEnd though as that will include at
        // least one read timeout from the terminate procedure.
        auto tEnd = (counters.state != ReadoutWorker::State::Idle
                     ?  std::chrono::steady_clock::now()
                     : counters.tTerminateStart);
        auto runDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
        double runSeconds = runDuration.count() / 1000.0;
        double megaBytes = counters.bytesRead * 1.0 / util::Megabytes(1);
        double mbs = megaBytes / runSeconds;

        cout << endl;
        cout << "---- readout stats ----" << endl;
        cout << "buffersRead=" << counters.buffersRead << endl;
        cout << "buffersFlushed=" << counters.buffersFlushed << endl;
        cout << "snoopMissedBuffers=" << counters.snoopMissedBuffers << endl;
        cout << "usbFramingErrors=" << counters.usbFramingErrors << endl;
        cout << "usbTempMovedBytes=" << counters.usbTempMovedBytes << endl;
        cout << "ethShortReads=" << counters.ethShortReads << endl;
        cout << "readTimeouts=" << counters.readTimeouts << endl;
        cout << "totalBytesTransferred=" << counters.bytesRead << endl;
        cout << "duration=" << runDuration.count() << " ms" << endl;

        cout << "stackHits: ";
        for (size_t stack=0; stack<counters.stackHits.size(); ++stack)
        {
            size_t hits = counters.stackHits[stack];

            if (hits)
                cout << stack << ": " << hits << " ";
        }
        cout << endl;

        cout << "stackErrors:";
        for (size_t stack=0; stack<stackErrors.stackErrors.size(); ++stack)
        {
            const auto &errorCounts = stackErrors.stackErrors[stack];

            for (auto it=errorCounts.begin(); it!=errorCounts.end(); ++it)
            {
                cout << fmt::format("stack={}, line={}, flags={}, count={}",
                                    stack, it->first.line, it->first.flags,
                                    it->second)
                    << endl;
            }
        }

        cout << endl;

        if (connectionType == ConnectionType::ETH)
        {
            auto pipeCounters = counters.ethStats[DataPipe];
            cout << endl;
            cout << "  -- eth data pipe receive stats --" << endl;
            cout << "  receiveAttempts=" << pipeCounters.receiveAttempts << endl;
            cout << "  receivedPackets=" << pipeCounters.receivedPackets << endl;
            cout << "  receivedBytes=" << pipeCounters.receivedBytes << endl;
            cout << "  shortPackets=" << pipeCounters.shortPackets << endl;
            cout << "  packetsWithResidue=" << pipeCounters.packetsWithResidue << endl;
            cout << "  noHeader=" << pipeCounters.noHeader << endl;
            cout << "  headerOutOfRange=" << pipeCounters.headerOutOfRange << endl;
            cout << "  lostPackets=" << pipeCounters.lostPackets << endl;
        }

        cout << endl;

        // listfile writer counters
        {
            auto writerCounters = counters.listfileWriterCounters;
            auto tStart = writerCounters.tStart;
            auto tEnd = (writerCounters.state != ListfileWriterCounters::Idle
                         ? std::chrono::steady_clock::now()
                         : writerCounters.tEnd);
            auto writerElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
            auto writerSeconds = writerElapsed.count() / 1000.0;
            double megaBytes = writerCounters.bytesWritten * 1.0 / util::Megabytes(1);
            double mbs = megaBytes / writerSeconds;

            cout << "  -- listfile writer counters --" << endl;
            cout << "  writes=" << writerCounters.writes << endl;
            cout << "  bytesWritten=" << writerCounters.bytesWritten << endl;
            cout << "  exception=";
            try
            {
                if (counters.eptr)
                    std::rethrow_exception(counters.eptr);
                cout << "none" << endl;
            }
            catch (const std::runtime_error &e)
            {
                cout << e.what() << endl;
            }
            cout << "  duration=" << writerSeconds << " s" << endl;
            cout << "  rate=" << mbs << " MB/s" << endl;
        }

        cout << endl;

        cout << "Ran for " << runSeconds << " seconds, transferred a total of " << megaBytes
            << " MB, resulting data rate: " << mbs << "MB/s"
            << endl;
    }

    //
    // parser stats
    //
    {
        cout << endl << "---- readout parser stats ----" << endl;
        readout_parser::print_counters(cout, parserCounters);
    }
}

int main(int argc, char *argv[])
{
    // MVLC connection overrides
    std::string opt_mvlcEthHost;
    bool opt_mvlcUseFirstUSBDevice = false;
    int opt_mvlcUSBIndex = -1;
    std::string opt_mvlcUSBSerial;

    // listfile and run options
    bool opt_noListfile = false;
    bool opt_overwriteListfile = false;
    std::string opt_listfileOut;
    std::string opt_listfileCompressionType = "lz4";
    int opt_listfileCompressionLevel = 0;
    std::string opt_crateConfig;
    unsigned opt_secondsToRun = 0;
    bool opt_printReadoutData = false;
    bool opt_noPeriodicCounterDumps = false;

    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    bool opt_initOnly = false;
    bool opt_ignoreInitErrors = false;

    auto cli
        = lyra::help(opt_showHelp)

        // mvlc overrides
        | lyra::opt(opt_mvlcEthHost, "hostname")
            ["--mvlc-eth"] ("mvlc ethernet hostname (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUseFirstUSBDevice)
            ["--mvlc-usb"] ("connect to the first mvlc usb device (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUSBIndex, "index")
            ["--mvlc-usb-index"] ("connect to the mvlc with the given usb device index (overrides CrateConfig)")

        | lyra::opt(opt_mvlcUSBSerial, "serial")
            ["--mvlc-usb-serial"] ("connect to the mvlc with the given usb serial number (overrides CrateConfig)")

        // listfile
        | lyra::opt(opt_noListfile)
            ["--no-listfile"] ("do not write readout data to a listfile (data will not be recorded)")

        | lyra::opt(opt_overwriteListfile)
            ["--overwrite-listfile"] ("overwrite an existing listfile")

        | lyra::opt(opt_listfileOut, "listfileName")
            ["--listfile"] ("filename of the output listfile (e.g. run001.zip)")

        | lyra::opt(opt_listfileCompressionType, "type")
            ["--listfile-compression-type"].choices("zip", "lz4") ("'zip' or 'lz4'")

        | lyra::opt(opt_listfileCompressionLevel, "level")
            ["--listfile-compression-level"] ("compression level to use (for zip 0 means no compression)")

        // logging
        | lyra::opt(opt_printReadoutData)
            ["--print-readout-data"]("log each word of readout data (very verbose!)")

        | lyra::opt(opt_noPeriodicCounterDumps)
            ["--no-periodic-counter-dumps"]("do not periodcally print readout and parser counters to stdout")

        | lyra::opt(opt_initOnly)
            ["--init-only"]("run the DAQ init sequence and exit")

        | lyra::opt(opt_ignoreInitErrors)
            ["--ignore-vme-init-errors"]("ignore VME errors during the DAQ init sequence")

        // logging
        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")
        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")

        // positional args
        | lyra::arg(opt_crateConfig, "crateConfig")
            ("crate config YAML or JSON file").required()

        | lyra::arg(opt_secondsToRun, "secondsToRun")
            ("duration the DAQ should run in seconds").optional()
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << cli << endl;

        cout
            << "The mini-daq utility is a command-line program for running a"
            << " MVLC based DAQ." << endl << endl
            << "Configuration data has to be supplied in a YAML 'CrateConfig' file." << endl
            << "Such a config file can be generated from an mvme setup using the" << endl
            << "'File -> Export VME Config' menu entry in mvme." << endl << endl
            << "Alternatively a CrateConfig object can be generated programmatically and" << endl
            << "written out using the to_yaml() free function."
            << endl;
        return 0;
    }

    // logging setup
    if (opt_logDebug)
        set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        set_global_log_level(spdlog::level::trace);

    try
    {
        auto timeToRun = std::chrono::seconds(opt_secondsToRun);

        CrateConfig crateConfig = {};

        try
        {
            crateConfig = crate_config_from_file(opt_crateConfig);
        }
        catch (const std::runtime_error &e)
        {
            cerr << fmt::format("Error loading CrateConfig from '{}': {}\n", opt_crateConfig, e.what());
            return 1;
        }

        MVLC mvlc;

        if (!opt_mvlcEthHost.empty())
            mvlc = make_mvlc_eth(opt_mvlcEthHost);
        else if (opt_mvlcUseFirstUSBDevice)
            mvlc = make_mvlc_usb();
        else if (opt_mvlcUSBIndex >= 0)
            mvlc = make_mvlc_usb(opt_mvlcUSBIndex);
        else if (!opt_mvlcUSBSerial.empty())
            mvlc = make_mvlc_usb(opt_mvlcUSBSerial);
        else
            mvlc = make_mvlc(crateConfig);

        // Cancel any possibly running readout when connecting.
        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            cerr << "Error connecting to MVLC: " << ec.message() << endl;
            return 1;
        }

        CommandExecOptions initOptions{};
        initOptions.continueOnVMEError = opt_ignoreInitErrors;

        if (opt_initOnly)
        {
            cout << "Running DAQ init sequence and exiting.\n";
            auto initResults = init_readout(mvlc, crateConfig, initOptions);

            if (initResults.ec)
                std::cerr << fmt::format("  Error during DAQ init sequence: {}\n", initResults.ec.message());

            for (const auto &cmdResult: initResults.init)
            {
                if (cmdResult.ec)
                {
                    std::cerr << fmt::format("  Error during DAQ init sequence: cmd={}, ec={}\n",
                        to_string(cmdResult.cmd), cmdResult.ec.message());
                }
            }

            if (initResults.ec)
                return opt_ignoreInitErrors ? 0 : 1;
            return 0;
        }

        //cout << "Connected to MVLC " << mvlc.connectionInfo() << endl;

        //
        // Listfile setup
        //
        if (opt_listfileOut.empty())
            opt_listfileOut = util::basename(opt_crateConfig) + ".zip";

        ListfileParams listfileParams =
        {
            .writeListfile = !opt_noListfile,
            .filepath = opt_listfileOut,
            .overwrite = opt_overwriteListfile,

            .compression = (opt_listfileCompressionType == "lz4"
                            ? ListfileParams::Compression::LZ4
                            : ListfileParams::Compression::ZIP),

            .compressionLevel = opt_listfileCompressionLevel,

        };

        //
        // readout parser callbacks
        //

        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.eventData = [opt_printReadoutData] (
            void *, int crateId, int eventIndex, const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            if (opt_printReadoutData)
            {
                for (u32 moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
                {
                    auto &moduleData = moduleDataList[moduleIndex];

                    if (moduleData.data.size)
                        util::log_buffer(
                            std::cout, basic_string_view<u32>(moduleData.data.data, moduleData.data.size),
                            fmt::format("module data: crateId={} eventIndex={}, moduleIndex={}", crateId, eventIndex, moduleIndex));
                }
            }
        };

        parserCallbacks.systemEvent = [opt_printReadoutData] (
            void *, int crateId, const u32 *header, u32 size)
        {
            if (opt_printReadoutData)
            {
                fmt::print("SystemEvent: crateId={}, header=0x{:08x}, type={}, size={}, bytes={}\n",
                     crateId, *header, system_event_type_to_string(system_event::extract_subtype(*header)),
                        size, size * sizeof(u32)
                );
            }
        };

        //
        // readout object
        //

        auto rdo = make_mvlc_readout(mvlc, crateConfig, listfileParams, parserCallbacks);

        // This optional callback can be used to do custom initialization during
        // the start sequence. See mvlc_readout_worker.h for how this works.
        auto initCallback = [](void *userContext, const std::string &initStage, MVLC &mvlc,
                                   const CrateConfig &crateConfig,
                                   const CommandExecOptions &execOptions)
        {
            spdlog::trace("called with initStage={}, userContext={}", initStage, fmt::ptr(userContext));
            return std::error_code();
        };

        rdo.setInitCallback(initCallback, reinterpret_cast<void *>(0x1337));

        spdlog::info("Starting readout. Running for {} seconds.", timeToRun.count());

        if (auto ec = rdo.start(timeToRun, initOptions))
        {
            cerr << "Error starting readout: " << ec.message() << endl;

            auto initResults = rdo.getInitResults();

            for (const auto &cmdResult: initResults.init)
            {
                if (cmdResult.ec)
                {
                    std::cerr << fmt::format("  Error during DAQ init sequence: cmd={}, ec={}\n",
                        to_string(cmdResult.cmd), cmdResult.ec.message());
                }
            }

            throw std::runtime_error("DAQ startup start error");
        }

        MiniDaqCountersUpdate counters;
        util::Stopwatch sw;

        while (!rdo.finished())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            if (!opt_noPeriodicCounterDumps)
            {
                dump_counters(
                    cout,
                    crateConfig.connectionType,
                    mvlc.getStackErrorCounters(),
                    rdo.workerCounters(),
                    rdo.parserCounters());

                update_counters(counters, rdo, std::chrono::duration_cast<std::chrono::milliseconds>(sw.interval()));
                dump_counters2(cout, counters.prev, counters.curr, counters.dt);

            }
        }

        mvlc.disconnect();

        cout << endl << "Final stats dump:" << endl;

        dump_counters(
            cout,
            crateConfig.connectionType,
            mvlc.getStackErrorCounters(),
            rdo.workerCounters(),
            rdo.parserCounters());

        update_counters(counters, rdo, std::chrono::duration_cast<std::chrono::milliseconds>(sw.interval()));
        dump_counters2(cout, counters.prev, counters.curr, counters.dt);


        auto cmdPipeCounters = mvlc.getCmdPipeCounters();

        spdlog::debug("CmdPipeCounters:\n"
                      "    reads={}, bytesRead={}, timeouts={}, invalidHeaders={}, wordsSkipped={}\n"
                      "    errorBuffers={}, superBuffer={}, stackBuffers={}, dsoBuffers={}\n"
                      "    shortSupers={}, superFormatErrors={}, superRefMismatches={}, stackRefMismatches={}",

                      cmdPipeCounters.reads,
                      cmdPipeCounters.bytesRead,
                      cmdPipeCounters.timeouts,
                      cmdPipeCounters.invalidHeaders,
                      cmdPipeCounters.wordsSkipped,
                      cmdPipeCounters.errorBuffers,
                      cmdPipeCounters.superBuffers,
                      cmdPipeCounters.stackBuffers,
                      cmdPipeCounters.dsoBuffers,

                      cmdPipeCounters.shortSuperBuffers,
                      cmdPipeCounters.superFormatErrors,
                      cmdPipeCounters.superRefMismatches,
                      cmdPipeCounters.stackRefMismatches);

        return 0;
    }
    catch (const std::runtime_error &e)
    {
        cerr << "mini-daq caught an exception: " << e.what() << endl;
        return 1;
    }
    /*
    catch (...)
    {
        cerr << "mini-daq caught an unknown exception" << endl;
        return 1;
    }
    */

    return 0;
}
