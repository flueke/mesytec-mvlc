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
#include <fmt/format.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

void dump_counters(
    std::ostream &out,
    const ConnectionType &connectionType,
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
        for (size_t stack=0; stack<counters.stackErrors.stackErrors.size(); ++stack)
        {
            const auto &errorCounts = counters.stackErrors.stackErrors[stack];

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
        readout_parser::dump_counters(cout, parserCounters);
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
    unsigned opt_secondsToRun = 10;
    CommandExecOptions initOptions = {};
    bool opt_logReadoutData = false;
    bool opt_noPeriodicCounterDumps = false;

    bool opt_showHelp = false;

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

        // init options
        | lyra::opt(initOptions.noBatching)
            ["--init-no-batching"] ("disables command batching during the MVLC init phase")

        // logging
        | lyra::opt(opt_logReadoutData)
            ["--log-readout-data"]("log each word of readout data (very verbose!)")

        | lyra::opt(opt_noPeriodicCounterDumps)
            ["--no-periodic-counter-dumps"]("do not periodcally print readout and parser counters to stdout")

        // positional args
        | lyra::arg(opt_crateConfig, "crateConfig")
            ("crate config yaml file").required()

        | lyra::arg(opt_secondsToRun, "secondsToRun")
            ("duration the DAQ should run in seconds").required()
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
            << "The mini-daq utility is a command-line program for running an"
            << " MVLC based DAQ." << endl << endl
            << "Configuration data has to be supplied in a YAML 'CrateConfig' file." << endl
            << "Such a config file can be generated from an mvme setup using the" << endl
            << "'File -> Export VME Config' menu entry in mvme." << endl << endl
            << "Alternatively a CrateConfig object can be generated programmatically and" << endl
            << "written out using the to_yaml() free function."
            << endl;
        return 0;
    }

    std::ifstream inConfig(opt_crateConfig);

    if (!inConfig.is_open())
    {
        cerr << "Error opening crate config " << argv[1] << " for reading." << endl;
        return 1;
    }

    try
    {
        auto timeToRun = std::chrono::seconds(opt_secondsToRun);

        CrateConfig crateConfig = {};

        try
        {
            crateConfig = crate_config_from_yaml(inConfig);
        }
        catch (const std::runtime_error &e)
        {
            cerr << "Error parsing CrateConfig: " << e.what() << endl;
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

        cout << "Connected to MVLC " << mvlc.connectionInfo() << endl;

        //
        // init
        //
        {
            auto initResults = init_readout(mvlc, crateConfig, initOptions);

            cout << "Results from init_commands:" << endl << initResults.init << endl;

            if (initResults.ec)
            {
                cerr << "Error running readout init sequence: " << initResults.ec.message() << endl;
                return 1;
            }
        }

        //
        // listfile
        //
        listfile::ZipCreator zipWriter;
        listfile::WriteHandle *lfh = nullptr;

        if (!opt_noListfile)
        {
            if (opt_listfileOut.empty())
                opt_listfileOut = util::basename(opt_crateConfig) + ".zip";

            cout << "listfile filename: " << opt_listfileOut << endl;

            if (!opt_overwriteListfile && util::file_exists(opt_listfileOut))
            {
                cerr << "Error: output listfile " << opt_listfileOut << " exists."
                    " Use  --overwrite-listfile to force overwriting the file." << endl;
                return 1;
            }

            cout << "Opening output listfile " << opt_listfileOut << " for writing." << endl;

            zipWriter.createArchive(opt_listfileOut);

            if (opt_listfileCompressionType == "lz4")
                lfh = zipWriter.createLZ4Entry("listfile.mvlclst", opt_listfileCompressionLevel);
            else if (opt_listfileCompressionType == "zip")
                lfh = zipWriter.createZIPEntry("listfile.mvlclst", opt_listfileCompressionLevel);
            else
                return 1;
        }

        // Write the CrateConfig and additional meta information to the listfile.
        if (lfh)
            listfile::listfile_write_preamble(*lfh, crateConfig);

        //
        // Setup and start the readout parser.
        //
        const size_t BufferSize = util::Megabytes(1);
        const size_t BufferCount = 100;
        ReadoutBufferQueues snoopQueues(BufferSize, BufferCount);

        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.beginEvent = [] (int eventIndex)
        {
        };

        parserCallbacks.groupPrefix = [opt_logReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
        {
            if (opt_logReadoutData)
            {
                util::log_buffer(
                    std::cout, basic_string_view<u32>(data, size),
                    fmt::format("prefix part: eventIndex={}, groupIndex={}", eventIndex, groupIndex));
            }
        };

        parserCallbacks.groupDynamic = [opt_logReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
        {
            if (opt_logReadoutData)
            {
                util::log_buffer(
                    std::cout, basic_string_view<u32>(data, size),
                    fmt::format("dynamic part: eventIndex={}, groupIndex={}", eventIndex, groupIndex));
            }
        };

        parserCallbacks.groupSuffix = [opt_logReadoutData] (int eventIndex, int groupIndex, const u32 *data, u32 size)
        {
            if (opt_logReadoutData)
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

        //
        // Create a ReadoutWorker and start the readout.
        //
        ReadoutWorker readoutWorker(mvlc, crateConfig.triggers, snoopQueues, lfh);

        cout << "Starting readout worker. Running for " << timeToRun.count() << " seconds." << endl;

        auto f = readoutWorker.start(timeToRun);

        if (auto ec = f.get())
        {
            cerr << "Error starting readout worker: " << ec.message() << endl;
            throw std::runtime_error("ReadoutWorker error");
        }

        // wait until readout done, dumping counter stats periodically
        while (readoutWorker.state() != ReadoutWorker::State::Idle)
        {
            readoutWorker.waitableState().wait_for(
                std::chrono::milliseconds(1000),
                [] (const ReadoutWorker::State &state)
                {
                    return state == ReadoutWorker::State::Idle;
                });

            if (!opt_noPeriodicCounterDumps)
            {
                dump_counters(
                    cout,
                    crateConfig.connectionType,
                    readoutWorker.counters(),
                    parserCounters.copy());
            }
        }

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

        int retval = 0;

        if (auto ec = disable_all_triggers(mvlc))
        {
            cerr << "Error disabling MVLC triggers: " << ec.message() << endl;
            retval = 1;
        }

        mvlc.disconnect();

        cout << endl << "Final stats dump:" << endl;

        dump_counters(
            cout,
            crateConfig.connectionType,
            readoutWorker.counters(),
            parserCounters.copy());

        return retval;
    }
    catch (const std::runtime_error &e)
    {
        cerr << "mini-daq caught an exception: " << e.what() << endl;
        return 1;
    }
    catch (...)
    {
        cerr << "mini-daq caught an unknown exception" << endl;
        return 1;
    }

    return 0;
}
