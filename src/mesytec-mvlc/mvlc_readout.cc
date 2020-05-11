#include "mvlc_readout.h"
#include "mvlc_constants.h"
#include "mvlc_listfile.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <fmt/format.h>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include "mvlc_dialog_util.h"
#include "mvlc_eth_interface.h"
#include "mvlc_factory.h"
#include "mvlc_listfile_util.h"
#include "mvlc_usb_interface.h"
#include "util/io_util.h"
#include "util/perf.h"
#include "util/storage_sizes.h"

using std::cerr;
using std::cout;
using std::endl;

namespace mesytec
{
namespace mvlc
{

MVLC MESYTEC_MVLC_EXPORT make_mvlc(const CrateConfig &crateConfig)
{
    switch (crateConfig.connectionType)
    {
        case ConnectionType::USB:
            if (crateConfig.usbIndex >= 0)
                return make_mvlc_usb(crateConfig.usbIndex);

            if (!crateConfig.usbSerial.empty())
                return make_mvlc_usb(crateConfig.usbSerial);

            return make_mvlc_usb();

        case ConnectionType::ETH:
            return make_mvlc_eth(crateConfig.ethHost);
    }

    throw std::runtime_error("unknown CrateConfig::connectionType");
}

ReadoutInitResults MESYTEC_MVLC_EXPORT init_readout(
    MVLC &mvlc, const CrateConfig &crateConfig,
    const CommandExecOptions stackExecOptions)
{
    ReadoutInitResults ret;

    // exec init_commands using all of the available stack memory
    {
        std::vector<u32> response;

        ret.ec = execute_stack(
            mvlc, crateConfig.initCommands,
            stacks::StackMemoryWords, stackExecOptions,
            response);

        ret.init = parse_stack_exec_response(
            crateConfig.initCommands, response);

        if (ret.ec)
        {
            cerr << "Error running init_commands: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // upload the readout stacks
    {
        ret.ec = setup_readout_stacks(mvlc, crateConfig.stacks);

        if (ret.ec)
        {
            cerr << "Error uploading readout stacks: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // Run the trigger_io init sequence after the stacks are in place.
    // Uses only the immediate stack reserved stack memory area to not
    // overwrite the readout stacks.
    {
        std::vector<u32> response;

        ret.ec = execute_stack(
            mvlc, crateConfig.initTriggerIO,
            stacks::ImmediateStackReservedWords, stackExecOptions,
            response);

        ret.triggerIO = parse_stack_exec_response(
            crateConfig.initTriggerIO, response);

        if (ret.ec)
        {
            cerr << "Error running init_trigger_io: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // Both readout stacks and the trigger io system are setup. Now activate the triggers.
    {
        ret.ec = setup_readout_triggers(mvlc, crateConfig.triggers);

        if (ret.ec)
        {
            cerr << "Error setting up readout triggers: " << ret.ec.message() << endl;
            return ret;
        }
    }

    return ret;
}

void MESYTEC_MVLC_EXPORT listfile_buffer_writer(
    listfile::WriteHandle *lfh,
    ReadoutBufferQueues &bufferQueues,
    Protected<ListfileBufferWriterState> &protectedState)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"listfile_writer",0,0,0);
#endif

    auto &filled = bufferQueues.filledBufferQueue();
    auto &empty = bufferQueues.emptyBufferQueue();

    cerr << "listfile_writer entering write loop" << endl;

    size_t bytesWritten = 0u;
    size_t writes = 0u;

    {
        auto state = protectedState.access();
        state->tStart = ListfileBufferWriterState::Clock::now();
        state->state = ListfileBufferWriterState::Running;
    }

    try
    {
        while (true)
        {
            auto buffer = filled.dequeue_blocking();

            assert(buffer);

            // should not happen
            if (unlikely(!buffer))
                break;

            // sentinel check
            if (unlikely(buffer->empty()))
            {
                empty.enqueue(buffer);
                break;
            }

            try
            {
                auto bufferView = buffer->viewU8();

                if (lfh)
                {
                    bytesWritten += lfh->write(bufferView.data(), bufferView.size());
                    ++writes;

                    auto state = protectedState.access();
                    state->bytesWritten = bytesWritten;
                    state->writes = writes;
                }

                empty.enqueue(buffer);
            } catch (...)
            {
                empty.enqueue(buffer);
                throw;
            }
        }
    }
    catch (const std::runtime_error &e)
    {
        {
            auto state = protectedState.access();
            state->exception = std::current_exception();
        }

        cerr << "listfile_writer caught a std::runtime_error: " << e.what() << endl;
    }
    catch (...)
    {
        {
            auto state = protectedState.access();
            state->exception = std::current_exception();
        }

        cerr << "listfile_writer caught an unknown exception." << endl;
    }

    {
        auto state = protectedState.access();
        state->state = ListfileBufferWriterState::Idle;
        state->tEnd = ListfileBufferWriterState::Clock::now();
    }

    cerr << "listfile_writer left write loop, writes=" << writes << ", bytesWritten=" << bytesWritten << endl;
}


void stack_error_notification_poller(
    MVLC mvlc,
    Protected<ReadoutWorker::Counters> &counters,
    std::atomic<bool> &quit)
{
    static constexpr auto Default_PollInterval = std::chrono::milliseconds(1000);

#ifndef __WIN32
    prctl(PR_SET_NAME,"error_poller",0,0,0);
#endif

    std::vector<u32> buffer;
    buffer.reserve(util::Megabytes(1));

    std::cout << "stack_error_notification_poller entering loop" << std::endl;

    // FIXME: this cannot handle high-rate error notifications as it will only
    // yield one error buffer per read, then sleep for Default_PollInterval.
    while (!quit)
    {
        //std::cout << "stack_error_notification_poller begin read" << std::endl;
        auto ec = mvlc.readKnownBuffer(buffer);
        //std::cout << "stack_error_notification_poller read done" << std::endl;

        if (!buffer.empty())
        {
            std::cout << "stack_error_notification_poller updating counters" << std::endl;
            update_stack_error_counters(counters.access()->stackErrors, buffer);
        }

        if (ec == ErrorType::ConnectionError)
            break;

        if (buffer.empty())
        {
            //std::cout << "stack_error_notification_poller sleeping" << std::endl;
            std::this_thread::sleep_for(Default_PollInterval);
            //std::cout << "stack_error_notification_poller waking" << std::endl;
        }
    }

    std::cout << "stack_error_notification_poller exiting" << std::endl;
}

namespace
{

class ReadoutErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "readout_error";
    }

    std::string message(int ev) const override
    {
        return std::to_string(ev); // TODO: strings here
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        (void) ev; // TODO: define and use error conditions
        return {};
    }
};

const ReadoutErrorCategory theReadoutErrorCateogry {};

} // end anon namespace

std::error_code make_error_code(ReadoutWorkerError error)
{
    return { static_cast<int>(error), theReadoutErrorCateogry };
}

struct ReadoutWorker::Private
{
    static constexpr size_t BufferSize = util::Megabytes(1);
    static constexpr size_t BufferCount = 10;
    static constexpr std::chrono::seconds ShutdownReadoutMaxWait = std::chrono::seconds(10);

    WaitableProtected<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;

    MVLC mvlc;
    eth::MVLC_ETH_Interface *mvlcETH = nullptr;
    usb::MVLC_USB_Interface *mvlcUSB = nullptr;
    ReadoutBufferQueues &snoopQueues;
    std::vector<u32> stackTriggers;
    std::chrono::seconds timeToRun;
    Protected<Counters> counters;
    std::thread readoutThread;
    ReadoutBufferQueues listfileQueues;
    listfile::WriteHandle *lfh;
    ReadoutBuffer localBuffer;
    ReadoutBuffer previousData;
    ReadoutBuffer *outputBuffer_ = nullptr;
    u32 nextOutputBufferNumber = 1u;

    Private(MVLC &mvlc_, ReadoutBufferQueues &snoopQueues_)
        : state({})
        , mvlc(mvlc_)
        , snoopQueues(snoopQueues_)
        , counters({})
        , listfileQueues(BufferSize, BufferCount)
        , localBuffer(BufferSize)
        , previousData(BufferSize)
    {}

    ~Private()
    {
        if (readoutThread.joinable())
            readoutThread.join();
    }

    void setState(const ReadoutWorker::State &state_)
    {
        state.access().ref() = state_;
        desiredState = state_;
        counters.access()->state = state_;
    }

    ReadoutBuffer *getOutputBuffer()
    {
        if (!outputBuffer_)
        {
            outputBuffer_ = snoopQueues.emptyBufferQueue().dequeue();

            if (!outputBuffer_)
                outputBuffer_ = &localBuffer;

            outputBuffer_->clear();
            outputBuffer_->setBufferNumber(nextOutputBufferNumber++);
            outputBuffer_->setType(mvlc.connectionType());
        }

        return outputBuffer_;
    }

    void maybePutBackSnoopBuffer()
    {
        if (outputBuffer_ && outputBuffer_ != &localBuffer)
            snoopQueues.emptyBufferQueue().enqueue(outputBuffer_);

        outputBuffer_ = nullptr;
    }

    void flushCurrentOutputBuffer()
    {
        if (outputBuffer_ && outputBuffer_->used() > 0)
        {
            auto listfileBuffer = listfileQueues.emptyBufferQueue().dequeue_blocking();
            // copy the data and queue it up for the writer thread
            *listfileBuffer = *outputBuffer_;
            listfileQueues.filledBufferQueue().enqueue(listfileBuffer);

            if (outputBuffer_ != &localBuffer)
                snoopQueues.filledBufferQueue().enqueue(outputBuffer_);
            else
                counters.access()->snoopMissedBuffers++;

            counters.access()->buffersFlushed++;
            outputBuffer_ = nullptr;
        }
    }

    void loop(std::promise<std::error_code> promise);
    void terminateReadout();

    std::error_code readout(size_t &bytesTransferred);
    std::error_code readout_usb(usb::MVLC_USB_Interface *mvlcUSB, size_t &bytesTransferred);
    std::error_code readout_eth(eth::MVLC_ETH_Interface *mvlcETH, size_t &bytesTransferred);
};

constexpr std::chrono::seconds ReadoutWorker::Private::ShutdownReadoutMaxWait;

ReadoutWorker::ReadoutWorker(
    MVLC &mvlc,
    const std::vector<u32> &stackTriggers,
    ReadoutBufferQueues &snoopQueues,
    listfile::WriteHandle *lfh
    )
    : d(std::make_unique<Private>(mvlc, snoopQueues))
{
    d->state.access().ref() = State::Idle;
    d->desiredState = State::Idle;
    d->stackTriggers = stackTriggers;
    d->lfh = lfh;
}

// FIXME: exceptions
void ReadoutWorker::Private::loop(std::promise<std::error_code> promise)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"readout_worker",0,0,0);
#endif

    std::cout << "readout_worker thread starting" << std::endl;

    counters.access().ref() = {}; // reset the counters

    // listfile writer thread
    Protected<ListfileBufferWriterState> writerState({});

    auto writerThread = std::thread(
        listfile_buffer_writer,
        lfh,
        std::ref(listfileQueues),
        std::ref(writerState));

    // stack error polling thread
    std::atomic<bool> quitErrorPoller(false);

    std::thread pollerThread;
    pollerThread = std::thread(
        stack_error_notification_poller,
        mvlc,
        std::ref(counters),
        std::ref(quitErrorPoller));

    switch (mvlc.connectionType())
    {
        case ConnectionType::ETH:
            this->mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
            mvlcETH->resetPipeAndChannelStats();
            assert(mvlcETH);
            break;

        case ConnectionType::USB:
            this->mvlcUSB = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
            assert(mvlcUSB);
            break;
    }

    assert(mvlcETH || mvlcUSB);

    const auto TimestampInterval = std::chrono::seconds(1);

    auto tStart = std::chrono::steady_clock::now();
    counters.access()->tStart = tStart;
    setState(State::Running);

    // Grab an output buffer and write an initial timestamp into it.
    {
        listfile::BufferWriteHandle wh(*getOutputBuffer());
        listfile_write_timestamp_section(wh, system_event::subtype::BeginRun);
    }
    auto tTimestamp = tStart;

    // No errors and we're about to enter the readout loop -> set the value of
    // the start() promise.
    promise.set_value({});

    std::error_code ec;

    while (ec != ErrorType::ConnectionError)
    {
        const auto now = std::chrono::steady_clock::now();

        // check if timeToRun has elapsed
        {
            auto totalElapsed = now - tStart;

            if (timeToRun.count() != 0 && totalElapsed >= timeToRun)
            {
                std::cout << "MVLC readout timeToRun reached" << std::endl;
                break;
            }
        }

        // check if we need to write a timestamp
        {
            auto elapsed = now - tTimestamp;

            if (elapsed >= TimestampInterval)
            {
                listfile::BufferWriteHandle wh(*getOutputBuffer());
                listfile_write_timestamp_section(wh, system_event::subtype::UnixTimetick);
                tTimestamp = now;
            }
        }

        auto state_ = state.access().copy();

        // stay in running state
        if (likely(state_ == State::Running && desiredState == State::Running))
        {
            size_t bytesTransferred = 0u;
            auto ec = this->readout(bytesTransferred);

            if (ec == ErrorType::ConnectionError)
            {
                std::cout << "Lost connection to MVLC, leaving readout loop. Error=" << ec.message() << std::endl;
                this->mvlc.disconnect();
                break;
            }
        }
        // pause
        else if (state_ == State::Running && desiredState == State::Paused)
        {
            terminateReadout();
            listfile::BufferWriteHandle wh(*getOutputBuffer());
            listfile_write_timestamp_section(wh, system_event::subtype::Pause);
            setState(State::Paused);
            std::cout << "MVLC readout paused" << std::endl;
        }
        // resume
        else if (state_ == State::Paused && desiredState == State::Running)
        {
            if (auto ec = setup_readout_triggers(mvlc, stackTriggers))
            {
                std::cout << "Error resuming MVLC readout: " << ec.message() << endl;
                break;
            }
            listfile::BufferWriteHandle wh(*getOutputBuffer());
            listfile_write_timestamp_section(wh, system_event::subtype::Resume);
            setState(State::Running);
            std::cout << "MVLC readout paused" << std::endl;
        }
        // stop
        else if (desiredState == State::Stopping)
        {
            std::cout << "MVLC readout requested to stop" << std::endl;
            break;
        }
        // paused
        else if (state_ == State::Paused)
        {
            std::cout << "MVLC readout paused" << std::endl;
            constexpr auto PauseSleepDuration = std::chrono::milliseconds(100);
            std::this_thread::sleep_for(PauseSleepDuration);
        }
        else
        {
            assert(!"invalid code path");
        }
    }

    std::cout << "MVLC readout stopping" << std::endl;
    setState(State::Stopping);

    auto tTerminateStart = std::chrono::steady_clock::now();
    terminateReadout();
    auto tTerminateEnd = std::chrono::steady_clock::now();
    auto terminateDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tTerminateEnd - tTerminateStart);

    {
        auto c = counters.access();
        c->tTerminateStart = tTerminateStart;
        c->tTerminateEnd = tTerminateEnd;
    }

    std::cout << "terminateReadout() took " << terminateDuration.count() << " ms to complete" << std::endl;

    // Write EndRun and EndOfFile system event sections into a ReadoutBuffer
    // and immediately flush the buffer.
    {
        listfile::BufferWriteHandle wh(*getOutputBuffer());
        listfile_write_timestamp_section(wh, system_event::subtype::EndRun);
        listfile_write_system_event(wh, system_event::subtype::EndOfFile);
        flushCurrentOutputBuffer();
    }

    maybePutBackSnoopBuffer();

    // stop the listfile writer
    {
        auto sentinel = listfileQueues.emptyBufferQueue().dequeue_blocking();
        sentinel->clear();
        listfileQueues.filledBufferQueue().enqueue(sentinel);
    }

    if (writerThread.joinable())
        writerThread.join();

    // Record the tEnd before waiting for the error poller to stop. This way
    // the final stats more closely reflects the actual data rate while the DAQ
    // was active. Note that the time taken in terminateReadout() is still
    // counted into the duration which means there's at least one read timeout
    // at the end. For long runs this doesn't really have an effect on the rate
    // but for short runs the final rate is noticably lower due to that last
    // read timeout.
    {
        auto tEnd = std::chrono::steady_clock::now();
        auto c = counters.access();
        c->tEnd = tEnd;
        c->ec = ec;
    }

    // stop the stack error poller
    quitErrorPoller = true;

    if (pollerThread.joinable())
        pollerThread.join();

    // Check that all buffers from the listfile writer queue have been returned.
    assert(listfileQueues.emptyBufferQueue().size() == BufferCount);

    setState(State::Idle);
}

// Cleanly end a running readout session. The code disables all triggers by
// writing to the trigger registers via the command pipe while in parallel
// reading and processing data from the data pipe until no more data
// arrives. These things have to be done in parallel as otherwise in the
// case of USB the data from the data pipe could clog the bus and no
// replies could be received on the command pipe.
void ReadoutWorker::Private::terminateReadout()
{
    auto f = std::async(
        std::launch::async,
        [this] ()
        {
            static const int DisableTriggerRetryCount = 5;

            for (int try_ = 0; try_ < DisableTriggerRetryCount; try_++)
            {
                if (auto ec = disable_all_triggers(this->mvlc))
                {
                    if (ec == ErrorType::ConnectionError)
                        return ec;
                }
                else break;
            }

            return std::error_code{};
        });

    using Clock = std::chrono::high_resolution_clock;
    auto tStart = Clock::now();
    size_t bytesTransferred = 0u;
    // The loop could hang forever if disabling the readout triggers does
    // not work for some reason. To circumvent this the total time spent in
    // the loop is limited to ShutdownReadoutMaxWait.
    do
    {
        bytesTransferred = 0u;
        this->readout(bytesTransferred);

        auto elapsed = Clock::now() - tStart;

        if (elapsed > ShutdownReadoutMaxWait)
            break;

    } while (bytesTransferred > 0);

    if (auto ec = f.get())
    {
        cerr << "terminateReadout: error disabling triggers: " << ec.message() << endl;
    }
}

// Note: in addition to stack frames this includes SystemEvent frames. These
// are written into the readout buffers by the listfile_write_* functions.
inline bool is_valid_readout_frame(const FrameInfo &frameInfo)
{
    return (frameInfo.type == frame_headers::StackFrame
            || frameInfo.type == frame_headers::StackContinuation
            || frameInfo.type == frame_headers::SystemEvent);
}

inline void fixup_usb_buffer(
    ReadoutBuffer &readBuffer,
    ReadoutBuffer &tempBuffer,
    Protected<ReadoutWorker::Counters> &counters)
{
    auto view = readBuffer.viewU8();

    while (!view.empty())
    {
        if (view.size() >= sizeof(u32))
        {
            FrameInfo frameInfo = {};

            while (view.size() >= sizeof(u32))
            {
                u32 frameHeader = *reinterpret_cast<const u32 *>(&view[0]);
                // Can peek and check the next frame header
                frameInfo = extract_frame_info(frameHeader);

                if (is_valid_readout_frame(frameInfo))
                    break;

#if 0
                auto offset = &view[0] - readBuffer.data();
                auto wordOffset = offset / sizeof(u32);

                std::cout << fmt::format(
                    "!is_valid_readout_frame: buffer #{},  byteOffset={}, "
                    "wordOffset={}, frameHeader=0x{:008x}",
                    readBuffer.bufferNumber(),
                    offset, wordOffset, frameHeader) << std::endl;
#endif
                counters.access()->usbFramingErrors++;

                // Unexpected or invalid frame type. This should not happen
                // if the incoming MVLC data and the readout code are
                // correct.
                // Consume the invalid frame header word and try again with the
                // next word.
                view.remove_prefix(sizeof(u32));
            }

            if (!is_valid_readout_frame(frameInfo))
            {
                // The above loop was not able to find a valid readout frame.
                // Go to the top of the outer loop and let that handle any
                // possible leftover bytes on the next iteration.
                continue;
            }

            // Check if the full frame including the header is in the
            // readBuffer. If not move the trailing data to the tempBuffer.
            if ((frameInfo.len + 1u) * sizeof(u32) > view.size())
            {
                std::memcpy(
                    tempBuffer.data(),
                    view.data(),
                    view.size());
                tempBuffer.setUsed(view.size());
                readBuffer.setUsed(readBuffer.used() - view.size());
                counters.access()->usbTempMovedBytes += view.size();
                return;
            }

            // Skip over the frameHeader and the frame contents.
            view.remove_prefix((frameInfo.len + 1) * sizeof(u32));
        }
    }
}

static const std::chrono::milliseconds FlushBufferTimeout(500);
static const size_t USBReadMinBytes = mesytec::mvlc::usb::USBSingleTransferMaxBytes;

std::error_code ReadoutWorker::Private::readout(size_t &bytesTransferred)
{
    assert(this->mvlcETH || this->mvlcUSB);

    std::error_code ec = {};

    if (mvlcUSB)
        ec = readout_usb(mvlcUSB, bytesTransferred);
    else
        ec = readout_eth(mvlcETH, bytesTransferred);

    {
        auto c = counters.access();

        if (bytesTransferred)
        {
            c->buffersRead++;
            c->bytesRead += bytesTransferred;
        }

        if (ec == ErrorType::Timeout)
            c->readTimeouts++;
    }

    flushCurrentOutputBuffer();

    return ec;
}

std::error_code ReadoutWorker::Private::readout_usb(
    usb::MVLC_USB_Interface *mvlcUSB,
    size_t &totalBytesTransferred)
{
    auto tStart = std::chrono::steady_clock::now();
    totalBytesTransferred = 0u;
    auto destBuffer = getOutputBuffer();
    std::error_code ec;

    if (previousData.used())
    {
        // move bytes from previousData into destBuffer
        destBuffer->ensureFreeSpace(previousData.used());
        std::memcpy(destBuffer->data() + destBuffer->used(),
                    previousData.data(), previousData.used());
        destBuffer->use(previousData.used());
        previousData.clear();
    }

    destBuffer->ensureFreeSpace(USBReadMinBytes);

    {
        auto dataGuard = mvlc.getLocks().lockData();

        while (destBuffer->free() >= USBReadMinBytes)
        {
            size_t bytesTransferred = 0u;

            ec = mvlcUSB->read_unbuffered(
                Pipe::Data,
                destBuffer->data() + destBuffer->used(),
                destBuffer->free(),
                bytesTransferred);

            destBuffer->use(bytesTransferred);
            totalBytesTransferred += bytesTransferred;

            if (ec == ErrorType::ConnectionError)
                break;

            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= FlushBufferTimeout)
                break;
        }
    } // with dataGuard

    //util::log_buffer(std::cout, destBuffer->viewU32(),
    //                 fmt::format("usb buffer#{} pre fixup", destBuffer->bufferNumber()),
    //                 3*60, 3*60);

    //auto preSize = destBuffer->used();

    fixup_usb_buffer(*destBuffer, previousData, counters);

    //auto postSize = destBuffer->used();

    //util::log_buffer(std::cout, destBuffer->viewU32(),
    //                 fmt::format("usb buffer#{} post fixup", destBuffer->bufferNumber()),
    //                 3*60, 3*60);

    return ec;
}

std::error_code ReadoutWorker::Private::readout_eth(
    eth::MVLC_ETH_Interface *mvlcETH,
    size_t &totalBytesTransferred)
{
    auto tStart = std::chrono::steady_clock::now();
    totalBytesTransferred = 0u;
    auto destBuffer = getOutputBuffer();
    std::error_code ec;

    {
        auto dataGuard = mvlc.getLocks().lockData();

        while (destBuffer->free() >= eth::JumboFrameMaxSize)
        {
            auto result = mvlcETH->read_packet(
                Pipe::Data,
                destBuffer->data() + destBuffer->used(),
                destBuffer->free());

            ec = result.ec;
            destBuffer->use(result.bytesTransferred);
            totalBytesTransferred += result.bytesTransferred;

#if 0
            if (this->firstPacketDebugDump)
            {
                cout << "first received readout eth packet:" << endl;
                cout << fmt::format("header0=0x{:08x}", result.header0()) << endl;
                cout << fmt::format("header1=0x{:08x}", result.header1()) << endl;
                cout << "  packetNumber=" << result.packetNumber() << endl;
                cout << "  dataWordCount=" << result.dataWordCount() << endl;
                cout << "  lostPackets=" << result.lostPackets << endl;
                cout << "  nextHeaderPointer=" << result.nextHeaderPointer() << endl;
                this->firstPacketDebugDump = false;
            }
#endif

            if (result.ec == ErrorType::ConnectionError)
                return result.ec;

            if (result.ec == MVLCErrorCode::ShortRead)
            {
                counters.access()->ethShortReads++;
                continue;
            }

            // A crude way of handling packets with residual bytes at the end. Just
            // subtract the residue from buffer->used which means the residual
            // bytes will be overwritten by the next packets data. This will at
            // least keep the structure somewhat intact assuming that the
            // dataWordCount in header0 is correct. Note that this case does not
            // happen, the MVLC never generates packets with residual bytes.
            if (unlikely(result.leftoverBytes()))
            {
                std::cout << "Oi! There's residue here!" << std::endl;
                destBuffer->setUsed(destBuffer->used() - result.leftoverBytes());
            }

            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= FlushBufferTimeout)
                break;
        }
    } // with dataGuard

    counters.access()->ethStats = mvlcETH->getPipeStats();

    return ec;
}

ReadoutWorker::~ReadoutWorker()
{
}

ReadoutWorker::State ReadoutWorker::state() const
{
    return d->state.access().copy();
}

WaitableProtected<ReadoutWorker::State> &ReadoutWorker::waitableState()
{
    return d->state;
}

ReadoutWorker::Counters ReadoutWorker::counters()
{
    auto counters = d->counters.access();
    return counters.copy();
}

std::future<std::error_code> ReadoutWorker::start(const std::chrono::seconds &timeToRun)
{
    std::promise<std::error_code> promise;
    auto f = promise.get_future();

    if (d->state.access().ref() != State::Idle)
    {
        promise.set_value(make_error_code(ReadoutWorkerError::ReadoutNotIdle));
        return f;
    }

    d->setState(State::Starting);
    d->timeToRun = timeToRun;

    d->readoutThread = std::thread(&Private::loop, d.get(), std::move(promise));

    return f;
}

std::error_code ReadoutWorker::stop()
{
    auto state_ = d->state.access().copy();

    if (state_ == State::Idle || state_ == State::Stopping)
        return make_error_code(ReadoutWorkerError::ReadoutNotRunning);

    d->desiredState = State::Stopping;
    return {};
}

std::error_code ReadoutWorker::pause()
{
    auto state_ = d->state.access().copy();

    if (state_ != State::Running)
        return make_error_code(ReadoutWorkerError::ReadoutNotRunning);

    d->desiredState = State::Paused;
    return {};
}

std::error_code ReadoutWorker::resume()
{
    auto state_ = d->state.access().copy();

    if (state_ != State::Paused)
        return make_error_code(ReadoutWorkerError::ReadoutNotPaused);

    d->desiredState = State::Running;
    return {};
}

} // end namespace mvlc
} // end namespace mesytec
