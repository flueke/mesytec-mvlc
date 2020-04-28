#include "mvlc_readout.h"
#include "mvlc_eth_interface.h"
#include "mvlc_usb_interface.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include "mvlc_factory.h"
#include "mvlc_dialog_util.h"
#include "util/storage_sizes.h"
#include "util/perf.h"

using std::cerr;
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

            // sentinel checks
            if (!buffer)
                break;

            if (buffer->empty())
            {
                empty.enqueue(buffer);
                break;
            }

            try
            {
                auto bufferView = buffer->viewU8();

                bytesWritten += lfh->write(bufferView.data(), bufferView.size());
                ++writes;

                empty.enqueue(buffer);

                auto state = protectedState.access();
                state->bytesWritten = bytesWritten;
                state->writes = writes;

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
    static constexpr size_t BufferSize = Megabytes(1);
    static constexpr size_t BufferCount = 10;

    std::atomic<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;

    MVLC mvlc;
    ReadoutBufferQueues &snoopQueues;
    std::vector<u32> stackTriggers;
    std::chrono::seconds timeToRun;
    Protected<Counters> counters;
    std::thread readoutThread;
    std::thread listfileThread;
    ReadoutBufferQueues listfileQueues;
    listfile::WriteHandle *lfh;
    ReadoutBuffer localBuffer;
    ReadoutBuffer previousData;
    ReadoutBuffer *outputBuffer_ = nullptr;
    u32 nextOutputBufferNumber = 0u;

    Private(MVLC &mvlc_, ReadoutBufferQueues &snoopQueues_)
        : mvlc(mvlc_)
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
        state = state_;
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

    void maybePutBackBuffer()
    {
        if (outputBuffer_ && outputBuffer_ != &localBuffer)
            snoopQueues.emptyBufferQueue().enqueue(outputBuffer_);

        outputBuffer_ = nullptr;
    }

    void flushCurrentOutputBuffer()
    {
        if (outputBuffer_ && outputBuffer_->used() > 0)
        {
            // TODO: count flushed buffers
            auto listfileBuffer = listfileQueues.emptyBufferQueue().dequeue_blocking();
            // copy the data and queue it up for the writer thread
            *listfileBuffer = *outputBuffer_;
            listfileQueues.filledBufferQueue().enqueue(listfileBuffer);

            if (outputBuffer_ != &localBuffer)
                snoopQueues.filledBufferQueue().enqueue(outputBuffer_);
            else
            {
                // TODO: count dropped buffer
            }

            outputBuffer_ = nullptr;
        }
    }

    void loop(std::promise<std::error_code> promise);
    std::error_code readout_usb(usb::MVLC_USB_Interface *mvlcUSB, size_t &bytesTransferred);
    std::error_code readout_eth(eth::MVLC_ETH_Interface *mvlcETH, size_t &bytesTransferred);
};

ReadoutWorker::ReadoutWorker(
    MVLC &mvlc,
    const std::vector<u32> &stackTriggers,
    ReadoutBufferQueues &snoopQueues,
    listfile::WriteHandle *lfh
    )
    : d(std::make_unique<Private>(mvlc, snoopQueues))
{
    d->state = State::Idle;
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

    counters.access().ref() = {};

    Protected<ListfileBufferWriterState> writerState({});

    auto writerThread = std::thread(
        listfile_buffer_writer, lfh, std::ref(listfileQueues),
        std::ref(writerState));

    const auto TimestampInterval = std::chrono::seconds(1);
    const auto connectionType = mvlc.connectionType();
    eth::MVLC_ETH_Interface *mvlcETH = nullptr;
    usb::MVLC_USB_Interface *mvlcUSB = nullptr;

    switch (connectionType)
    {
        case ConnectionType::ETH:
            mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
            assert(mvlcETH);
            break;

        case ConnectionType::USB:
            mvlcUSB = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
            assert(mvlcUSB);
            break;
    }

    assert(mvlcETH || mvlcUSB);

    std::error_code ec;
    auto tStart = std::chrono::steady_clock::now();
    counters.access()->tStart = tStart;
    setState(State::Running);
    promise.set_value({});

    while (ec != ErrorType::ConnectionError)
    {
        // timetick TODO

        // check if timeToRun has elapsed
        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (timeToRun.count() != 0 && elapsed >= timeToRun)
        {
            std::cout << "MVLC readout timeToRun reached" << std::endl;
            break;
        }

        using State = ReadoutWorker::State;

        // stay in running state
        if (likely(state == State::Running && desiredState == State::Running))
        {
            size_t bytesTransferred = 0u;

            if (mvlcUSB)
                ec = readout_usb(mvlcUSB, bytesTransferred);
            else
                ec = readout_eth(mvlcETH, bytesTransferred);

            counters.access()->bytesRead += bytesTransferred;
            flushCurrentOutputBuffer();
        }
        // pause
        else if (state == State::Running && desiredState == State::Paused)
        {
            // TODO: disable triggers
            // TODO: listfile_write_system_event(d->listfileOut, system_event::subtype::Pause);
            setState(State::Paused);
            std::cout << "MVLC readout paused" << std::endl;
        }
        // resume
        else if (state == State::Paused && desiredState == State::Running)
        {
            // TODO: enable triggers
            // TODO: listfile_write_system_event(d->listfileOut, system_event::subtype::Resume);
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
        else if (state == State::Paused)
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

    // stop the listfile writer
    {
        auto sentinel = listfileQueues.emptyBufferQueue().dequeue_blocking();
        sentinel->clear();
        listfileQueues.filledBufferQueue().enqueue(sentinel);
    }

    if (writerThread.joinable())
        writerThread.join();

    assert(listfileQueues.emptyBufferQueue().size() == BufferCount);

    auto tEnd = std::chrono::steady_clock::now();
    {
        auto c = counters.access();
        c->tEnd = tEnd;
        c->ec = ec;
    }

    setState(State::Idle);
}

inline bool is_valid_readout_frame(const FrameInfo &frameInfo)
{
    return (frameInfo.type == frame_headers::StackFrame
            || frameInfo.type == frame_headers::StackContinuation);
}

inline void fixup_usb_buffer(
    ReadoutBuffer &readBuffer,
    ReadoutBuffer &tempBuffer)
{
    auto view = readBuffer.viewU8();

    while (!view.empty())
    {
        if (view.size() >= sizeof(u32))
        {
            FrameInfo frameInfo = {};

            while (view.size() >= sizeof(u32))
            {
                // Can peek and check the next frame header
                frameInfo = extract_frame_info(*reinterpret_cast<const u32 *>(&view[0]));

                if (is_valid_readout_frame(frameInfo))
                    break;

                // TODO: ++counters.frameTypeErrors;

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
            if (frameInfo.len + 1u > view.size() / sizeof(u32))
            {
                std::memcpy(
                    tempBuffer.data(),
                    view.data(),
                    view.size());
                tempBuffer.setUsed(view.size());
                // TODO: counters.partialFrameTotalBytes += trailingBytes;
                return;
            }

            // Skip over the frameHeader and the frame contents.
            view.remove_prefix((frameInfo.len + 1) * sizeof(u32));
        }
    }
}

static const std::chrono::milliseconds FlushBufferTimeout(500);
static const size_t USBReadMinBytes = mesytec::mvlc::usb::USBSingleTransferMaxBytes;

std::error_code ReadoutWorker::Private::readout_usb(
    usb::MVLC_USB_Interface *mvlcUSB,
    size_t &totalBytesTransferred)
{
    auto tStart = std::chrono::steady_clock::now();
    auto destBuffer = getOutputBuffer();
    std::error_code ec;

    if (previousData.used())
    {
        // move bytes from previousData into destBuffer
        destBuffer->ensureFreeSpace(previousData.used());
        std::memcpy(destBuffer->data() + destBuffer->used(),
                    previousData.data(), previousData.used());
        previousData.setUsed(0);
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

    fixup_usb_buffer(*destBuffer, previousData);

    return ec;
}

std::error_code ReadoutWorker::Private::readout_eth(
    eth::MVLC_ETH_Interface *mvlcETH,
    size_t &totalBytesTransferred)
{
    auto tStart = std::chrono::steady_clock::now();
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

            if (result.ec == ErrorType::ConnectionError)
                return result.ec;

            if (result.ec == MVLCErrorCode::ShortRead)
            {
                // TODO: daqStats.buffersWithErrors++;
                continue;
            }

            // A crude way of handling packets with residual bytes at the end. Just
            // subtract the residue from buffer->used which means the residual
            // bytes will be overwritten by the next packets data. This will at
            // least keep the structure somewhat intact assuming that the
            // dataWordCount in header0 is correct. Note that this case does not
            // happen, the MVLC never generates packets with residual bytes.
            if (unlikely(result.leftoverBytes()))
                destBuffer->setUsed(destBuffer->used() - result.leftoverBytes());

            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= FlushBufferTimeout)
                break;
        }
    } // with dataGuard

    return ec;
}

ReadoutWorker::~ReadoutWorker()
{
}

ReadoutWorker::State ReadoutWorker::state() const
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

    if (d->state != State::Idle)
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
    if (d->state == State::Idle || d->state == State::Stopping)
        return make_error_code(ReadoutWorkerError::ReadoutNotRunning);

    d->desiredState = State::Stopping;
    return {};
}

std::error_code ReadoutWorker::pause()
{
    if (d->state != State::Running)
        return make_error_code(ReadoutWorkerError::ReadoutNotRunning);

    d->desiredState = State::Paused;
    return {};
}

std::error_code ReadoutWorker::resume()
{
    if (d->state != State::Paused)
        return make_error_code(ReadoutWorkerError::ReadoutNotPaused);

    d->desiredState = State::Running;
    return {};
}

} // end namespace mvlc
} // end namespace mesytec
