#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <fstream>
#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <mesytec-mvlc/mesytec_mvlc.h>
#include <mesytec-mvlc/mvlc_dialog_util.h>
#include <mesytec-mvlc/mvlc_eth_interface.h>
#include <mesytec-mvlc/mvlc_readout.h>
#include <mesytec-mvlc/mvlc_stack_executor.h>
#include <mesytec-mvlc/mvlc_usb_interface.h>
#include <mesytec-mvlc/util/perf.h>
#include <mesytec-mvlc/util/readout_buffer_queues.h>
#include <mesytec-mvlc/util/storage_sizes.h>
#include <mesytec-mvlc/util/string_view.hpp>

#include <fmt/format.h>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;
using namespace nonstd;

#if 0
template<typename Out>
Out &log_buffer(Out &out, const std::vector<u32> &buffer, const std::string &header = {})
{
    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << endl;

    for (const auto &value: buffer)
        out << fmt::format("  {:#010x}", value) << endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << endl;

    return out;
}
#endif

template<typename Out, typename View>
Out &log_buffer(Out &out, const View &buffer, const std::string &header = {})
{
    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << endl;

    for (const auto &value: buffer)
        out << fmt::format("  {:#010x}", value) << endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << endl;

    return out;
}

void listfile_writer(listfile::WriteHandle *lfh, ReadoutBufferQueues &bufferQueues)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"listfile_writer",0,0,0);
#endif

    auto &filled = bufferQueues.filledBufferQueue();
    auto &empty = bufferQueues.emptyBufferQueue();

    cout << "listfile_writer entering loop" << endl;
    size_t bytesWritten = 0u;
    size_t writes = 0u;

    while (true)
    {
        auto buffer = filled.dequeue_blocking();

        if (buffer->empty()) // sentinel
        {
            empty.enqueue(buffer);
            break;
        }

        auto bufferView = buffer->viewU8();
        bytesWritten += lfh->write(bufferView.data(), bufferView.size());
        ++writes;
        empty.enqueue(buffer);
    }

    cout << "listfile_writer left loop, writes=" << writes << ", bytesWritten=" << bytesWritten << endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Error: no crate config file specified." << endl;
        return 1;
    }

    std::ifstream inConfig(argv[1]);
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

#if 0
    if (auto ec = disable_all_triggers(mvlc))
    {
        cerr << "Error disabling MVLC triggers: " << ec.message() << endl;
        return 1;
    }
#endif

#if 0
    // exec init_commands using all of the stack memory
    {
        CommandExecOptions options;

        std::vector<u32> response;

        auto ec = execute_stack(
            mvlc, crateConfig.initCommands,
            stacks::StackMemoryWords, options, response);

        //log_buffer(cout, response, "init_commands response");

        if (ec)
        {
            cerr << "Error running init_commands: " << ec.message() << endl;
            return 1;
        }

        auto parsedResults = parse_stack_exec_response(crateConfig.initCommands, response);

        cout << parsedResults << endl;


        //for (const auto &resultGroup: parsedResults.groups)
        //{
        //    cout << "Stack Group " << resultGroup.name << ":" << endl;

        //    for (const auto &pr: resultGroup.results)
        //        cout << "  " << to_string(pr.cmd) << endl;
        //}
    }

    // stack uploads
    if (auto ec = setup_readout_stacks(mvlc, crateConfig.stacks))
    {
        cerr << "Error uploading readout stacks: " << ec.message() << endl;
        return 1;
    }

    // trigger io here (after stacks are in place)
    // uses only the immediate stack reserved stack memory to not overwrite the
    // readout stacks.
    {
        CommandExecOptions options;

        std::vector<u32> response;

        auto ec = execute_stack(
            mvlc, crateConfig.initTriggerIO,
            stacks::ImmediateStackReservedWords, options, response);

        log_buffer(cout, response, "init_trigger_io response");

        if (ec)
        {
            cerr << "Error running init_trigger_io: " << ec.message() << endl;
            return 1;
        }
    }

    // Both readout stacks and the trigger io system are setup. Now activate the triggers.
    if (auto ec = setup_readout_triggers(mvlc, crateConfig.triggers))
    {
        cerr << "Error setting up readout triggers: " << ec.message() << endl;
        return 1;
    }
#else
    {
        auto initResults = init_readout(
            mvlc, crateConfig);

        cout << "Results from init_commands:" << endl << initResults.init << endl;
        // cout << "Result from init_trigger_io:" << endl << initResults.triggerIO << endl;

        if (initResults.ec)
        {
            cerr << "Error running readout init sequence: " << initResults.ec.message() << endl;
            return 1;
        }
    }
#endif

    //
    // readout
    //

    ZipCreator zipWriter;
    zipWriter.createArchive("mini-daq.zip");
    auto lfh = zipWriter.createLZ4Entry("listfile.mvlclst", 0);
    //auto lfh = zipWriter.createZIPEntry("listfile.mvlclst", 0);

    const size_t BufferSize = Megabytes(1);
    const size_t BufferCount = 10;
    ReadoutBufferQueues bufferQueues(BufferSize, BufferCount);
    auto &filled = bufferQueues.filledBufferQueue();
    auto &empty = bufferQueues.emptyBufferQueue();

    auto writerThread = std::thread(listfile_writer, lfh, std::ref(bufferQueues));

    size_t totalBytesTransferred = 0u;
    size_t totalPacketsReceived = 0u;
    auto tStart = std::chrono::steady_clock::now();

    // TODO: FlushBufferTimeout, consumer buffer queue

    if (crateConfig.connectionType == ConnectionType::ETH)
    {
        auto mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
        assert(mvlcETH);

        std::error_code ec;

        while (ec != ErrorType::ConnectionError)
        {
            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= timeToRun)
                break;

            auto readBuffer = empty.dequeue_blocking();
            readBuffer->clear();
            assert(readBuffer->free() == BufferSize);
            assert(readBuffer->used() == 0);
            assert(readBuffer->capacity() == BufferSize);

            // hold the lock for multiple packet reads
            auto dataGuard = mvlc.getLocks().lockData();

            while (readBuffer->free() >= eth::JumboFrameMaxSize)
            {
                auto result = mvlcETH->read_packet(
                    Pipe::Data, readBuffer->data() + readBuffer->used(), readBuffer->free());

                //cout << "bytesTransferred=" << result.bytesTransferred;
                //cout << "pre  free: " << readBuffer->free() << endl;
                readBuffer->use(result.bytesTransferred);
                //cout << "post free: " << readBuffer->free() << endl;

                totalBytesTransferred += result.bytesTransferred;
                ec = result.ec;

                if (result.bytesTransferred > 0)
                    ++totalPacketsReceived;

                if (ec == ErrorType::ConnectionError)
                    break;

                // A crude way of handling packets with residual bytes at the end. Just
                // subtract the residue from buffer->used which means the residual
                // bytes will be overwritten by the next packets data. This will at
                // least keep the structure somewhat intact assuming that the
                // dataWordCount in header0 is correct. Note that this case does not
                // happen, the MVLC never generates packets with residual bytes.
                if (unlikely(result.leftoverBytes()))
                    readBuffer->setUsed(readBuffer->used() - result.leftoverBytes());
            }

            dataGuard.unlock();

            filled.enqueue(readBuffer);
        }

        assert(mvlcETH);
    }
    else if (crateConfig.connectionType == ConnectionType::USB)
    {
        auto mvlcUSB = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
        assert(mvlcUSB);

        std::error_code ec;

        while (ec != ErrorType::ConnectionError)
        {
            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= timeToRun)
                break;

            auto readBuffer = empty.dequeue_blocking();
            readBuffer->clear();

            size_t bytesTransferred = 0u;
            auto dataGuard = mvlc.getLocks().lockData();
            ec = mvlcUSB->read_unbuffered(
                Pipe::Data, readBuffer->data(), readBuffer->capacity(), bytesTransferred);
            dataGuard.unlock();
            readBuffer->use(bytesTransferred);
            totalBytesTransferred += bytesTransferred;

            //log_buffer(cout, readBuffer->viewU32(), "readout buffer");

            // FIXME: if the buffer is empty this will cause the listfile_writer to quit
            filled.enqueue(readBuffer);

            if (ec == ErrorType::ConnectionError)
            {
                cerr << "Lost connection to MVLC, leaving readout loop: " << ec.message() << endl;
                break;
            }

            // TODO: follow usb buffer framing, fixup_usb_buffer, store leftover data in tempBuffer,
            // pass buffers to consumers, track buffer numbers
        }

    }
    else
        throw std::runtime_error("invalid connectionType");

    // stop the listfile writer
    {
        auto sentinel = empty.dequeue_blocking();
        sentinel->clear();
        filled.enqueue(sentinel);
    }

    writerThread.join();

    auto tEnd = std::chrono::steady_clock::now();
    auto runDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
    double runSeconds = runDuration.count() / 1000.0;
    double megaBytes = totalBytesTransferred * 1.0 / Megabytes(1);
    double mbs = megaBytes / runSeconds;

    cout << "totalBytesTransferred=" << totalBytesTransferred << endl;
    cout << "duration=" << runDuration.count() << endl;
    cout << "Ran for " << runSeconds << " seconds, transferred a total of " << megaBytes
        << " MB, resulting data rate: " << mbs << "MB/s"
        << endl;

    cout << "totalPacketsReceived=" << totalPacketsReceived << endl;

    if (auto ec = disable_all_triggers(mvlc))
    {
        cerr << "Error disabling MVLC triggers: " << ec.message() << endl;
        return 1;
    }

    mvlc.disconnect();

    return 0;
}
