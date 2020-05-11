#include "mesytec-mvlc/mvlc_listfile.h"
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <fstream>

#include <mesytec-mvlc/mesytec_mvlc.h>
#include <mesytec-mvlc/mvlc_dialog_util.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>
#include <mesytec-mvlc/mvlc_readout.h>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/mvlc_stack_executor.h>
#include <mesytec-mvlc/mvlc_usb_interface.h>
#include <mesytec-mvlc/util/io_util.h>
#include <mesytec-mvlc/util/perf.h>
#include <mesytec-mvlc/util/readout_buffer_queues.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/util/string_view.hpp>
#include <fmt/format.h>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;
using namespace nonstd;

void run_readout_parser(
    readout_parser::ReadoutParserState &state,
    ReadoutBufferQueues &snoopQueues)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"readout_parser",0,0,0);
#endif

    readout_parser::ReadoutParserCallbacks callbacks;

    callbacks.beginEvent = [] (int eventIndex)
    {
        //cout << "beginEvent " + std::to_string(eventIndex) << endl;
    };

    callbacks.moduleDynamic = [] (int ei, int mi,  const u32 *data, u32 size)
    {
        //cout << "ei=" << ei << ", mi=" << mi << ", data=" << data << ", size=" << size << endl;
    };

    try
    {
        cerr << "run_readout_parser entering loop" << endl;

        auto &filled = snoopQueues.filledBufferQueue();
        auto &empty = snoopQueues.emptyBufferQueue();

        while (true)
        {
            auto buffer = filled.dequeue(std::chrono::milliseconds(100));

            if (buffer && buffer->empty()) // sentinel
                break;

            //if (!buffer)
            //    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!buffer) continue;

            try
            {
                //cout << "buffer #" << buffer->bufferNumber() << endl;

                auto bufferView = buffer->viewU32();

                readout_parser::parse_readout_buffer(
                    buffer->type(),
                    state,
                    callbacks,
                    buffer->bufferNumber(),
                    bufferView.data(),
                    bufferView.size());

                empty.enqueue(buffer);
            }
            catch (...)
            {
                empty.enqueue(buffer);
                throw;
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        {
            //auto state = protectedState.access();
            //state->exception = std::current_exception();
        }

        cerr << "readout_parser caught a std::runtime_error: " << e.what() << endl;
    }
    catch (...)
    {
        {
            //auto state = protectedState.access();
            //state->exception = std::current_exception();
        }

        cerr << "readout_parser caught an unknown exception." << endl;
    }

    cerr << "run_readout_parser left loop" << endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Error: no crate config file specified." << endl;
        return 1;
    }

    std::ifstream inConfig(argv[1]);

    if (!inConfig.is_open())
    {
        cerr << "Error opening crate config " << argv[1] << " for reading." << endl;
        return 1;
    }

    auto timeToRun = std::chrono::seconds(10);

    if (argc > 2)
        timeToRun = std::chrono::seconds(std::atol(argv[2]));

    auto crateConfig = crate_config_from_yaml(inConfig);

    auto mvlc = make_mvlc(crateConfig);

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
        auto initResults = init_readout(mvlc, crateConfig);

        cout << "Results from init_commands:" << endl << initResults.init << endl;
        // cout << "Result from init_trigger_io:" << endl << initResults.triggerIO << endl;

        if (initResults.ec)
        {
            cerr << "Error running readout init sequence: " << initResults.ec.message() << endl;
            return 1;
        }
    }

    //
    // readout parser
    //
    const size_t BufferSize = util::Megabytes(1);
    const size_t BufferCount = 10;
    ReadoutBufferQueues snoopQueues(BufferSize, BufferCount);

    auto parserState = readout_parser::make_readout_parser(crateConfig.stacks);
    std::thread parserThread;
    parserThread = std::thread(run_readout_parser, std::ref(parserState), std::ref(snoopQueues));

    //
    // readout
    //
    ZipCreator zipWriter;
    zipWriter.createArchive("mini-daq.zip");
    listfile::WriteHandle *lfh = nullptr;
    lfh = zipWriter.createZIPEntry("listfile.mvlclst", 0);
    listfile::listfile_write_preamble(*lfh, crateConfig);

    ReadoutWorker readoutWorker(mvlc, crateConfig.triggers, snoopQueues, lfh);

    auto f = readoutWorker.start(timeToRun);
    cout << "f.get(): " << f.get() << endl;

    // wait until readout done
    readoutWorker.waitableState().wait(
        [] (const ReadoutWorker::State &state)
        {
            return state == ReadoutWorker::State::Idle;
        });

    cout << "stopping readout_parser" << endl;

    // stop the readout parser
    if (parserThread.joinable())
    {
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

    //
    // readout stats
    //
    {
        // TODO: stack errors
        // TODO: eth packets received
        auto counters = readoutWorker.counters();

        auto tStart = counters.tStart;
        // Note: if idle this used the tTerminateStart time point. This means
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

        if (mvlc.connectionType() == ConnectionType::ETH)
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
        cout << "Ran for " << runSeconds << " seconds, transferred a total of " << megaBytes
            << " MB, resulting data rate: " << mbs << "MB/s"
            << endl;
    }

    //
    // parser stats
    //
    {
        auto counters = parserState.counters;

        cout << endl;
        cout << "---- parser stats ----" << endl;
        cout << "internalBufferLoss=" << counters.internalBufferLoss << endl;
        cout << "buffersProcessed=" << counters.buffersProcessed << endl;
        cout << "unusedBytes=" << counters.unusedBytes << endl;
        cout << "ethPacketLoss=" << counters.ethPacketLoss << endl;
        cout << "ethPacketsProcessed=" << counters.ethPacketsProcessed << endl;

        for (size_t sysEvent = 0; sysEvent < counters.systemEventTypes.size(); ++sysEvent)
        {
            if (counters.systemEventTypes[sysEvent])
            {
                cout << fmt::format("systemEventType 0x{:002x}, count={}",
                                    sysEvent, counters.systemEventTypes[sysEvent])
                    << endl;
            }
        }

        for (size_t pr=0; pr < counters.parseResults.size(); ++pr)
        {
            if (counters.parseResults[pr])
            {
                auto name = get_parse_result_name(static_cast<const readout_parser::ParseResult>(pr));

                cout << fmt::format("parseResult={}, count={}",
                                    name, counters.parseResults[pr])
                    << endl;
            }
        }

        cout << "parserExceptions=" << counters.parserExceptions << endl;
    }

    return retval;
}
