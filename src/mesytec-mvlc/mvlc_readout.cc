// =========================
//    MVLC readout outline
// =========================
//
// * Two different formats depending on connection type (ETH, USB).
// * Pass only complete frames around. For readout the detection has to be done
//   anyways so that system frames can be properly inserted.
// * Do not try to hit exactly 1s between SoftwareTimeticks. This will
//   complicate the code a lot and is not really needed if some form of timestamp
//   and/or duration is stored in the timetick event.
//
//
// ETH
// -------------------------
// Small packets of 1500 or 8192 bytes. Two header words for packet loss detection
// and handling (resume processing after loss).
//
// - Strategy
//
//   1) start with a fresh buffer
//
//   2) while free space in buffer > 8k:
//     read packet and append to buffer
//     if (flush timeout elapsed)
//         flush buffer
//     if (time for timetick)
//         insert timetick frame
//
//   3) flush buffer
//
// => Inserting system frames is allowed at any point.
//
// - Replay from file:
//   Read any amount of data from file into memory. If a word is not a system
//   frame then it must be header0() of a previously received packet. Follow
//   the header framing via the header0::NumDataWords value. This way either
//   end up on the next header0() or at the start of a system frame.
//   If part of a packet is at the end of the buffer read from disk store the part
//   temporarily and truncate the buffer. Then when doing the next read add the
//   partial packet to the front of the new buffer.
//   -> Packet boundaries can be restored and it can be guaranteed that only full
//   packets worth of data are passed internally.
//
//
// USB
// -------------------------
// Stream of data. Reads do not coincide with buffer framing. The exception is the
// very first read which starts with an 0xF3 frame.
// To be able to insert system frames (e.g. timeticks) and to make the analysis
// easier to write, internal buffers must contain complete frames only. To make
// this work the readout code has to follow the 0xF3 data framing. Extract the
// length to be able to jump to the next frame start. Store partial data at the
// end and truncate the buffer before flushing it.
//
// - Replay:
//   Starts with a system or a readout frame. Follow frame structure doing
//   truncation and copy of partial frames.
//
// Note: max amount to copy is the max length of a frame. That's 2^13 words
// (32k bytes) for readout frames.

#include "mvlc_readout.h"
#include "mvlc_constants.h"
#include "mvlc_listfile.h"

#include <atomic>
#include <cstring>
#include <exception>
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
#include "util/future_util.h"
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

    // Reset to a clean state
    ret.ec = disable_all_triggers_and_daq_mode(mvlc);

    if (ret.ec)
        return ret;

    std::vector<u32> response;

    // 1) trigger io
    {
        std::vector<u32> response;

        auto errors = execute_stack(
            mvlc, crateConfig.initTriggerIO,
            stacks::StackMemoryWords, stackExecOptions,
            response);

        ret.triggerIO = parse_stack_exec_response(crateConfig.initTriggerIO, response, errors);

        if (auto ec = get_first_error(ret.triggerIO))
        {
            cerr << "Error running init_trigger_io: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // 2) init commands
    {
        std::vector<u32> response;

        auto errors = execute_stack(
            mvlc, crateConfig.initCommands,
            stacks::StackMemoryWords, stackExecOptions,
            response);

        ret.init = parse_stack_exec_response(crateConfig.initCommands, response, errors);

        if (auto ec = get_first_error(ret.init))
        {
            cerr << "Error running init_commands: " << ec.message() << endl;
            return ret;
        }
    }

    // 3) upload stacks
    {
        ret.ec = setup_readout_stacks(mvlc, crateConfig.stacks);

        if (ret.ec)
        {
            cerr << "Error uploading readout stacks: " << ret.ec.message() << endl;
            return ret;
        }
    }

    // enable/disable eth jumbo frames
    if (mvlc.connectionType() == ConnectionType::ETH)
    {
        auto mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
        mvlcETH->enableJumboFrames(crateConfig.ethJumboEnable);
    }

    return ret;
}

void MESYTEC_MVLC_EXPORT listfile_buffer_writer(
    listfile::WriteHandle *lfh,
    ReadoutBufferQueues &bufferQueues,
    Protected<ListfileWriterCounters> &protectedState)
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
        state->tStart = ListfileWriterCounters::Clock::now();
        state->state = ListfileWriterCounters::Running;
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
                if (lfh)
                {
                    auto bufferView = buffer->viewU8();
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
            state->eptr = std::current_exception();
        }

        cerr << "listfile_writer caught a std::runtime_error: " << e.what() << endl;
    }
    catch (...)
    {
        {
            auto state = protectedState.access();
            state->eptr = std::current_exception();
        }

        cerr << "listfile_writer caught an unknown exception." << endl;
    }

    {
        auto state = protectedState.access();
        state->state = ListfileWriterCounters::Idle;
        state->tEnd = ListfileWriterCounters::Clock::now();
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
    static constexpr size_t ListfileWriterBufferSize = util::Megabytes(1);
    static constexpr size_t ListfileWriterBufferCount = 10;
    static constexpr std::chrono::seconds ShutdownReadoutMaxWait = std::chrono::seconds(10);

    WaitableProtected<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;

    MVLC mvlc;
    eth::MVLC_ETH_Interface *mvlcETH = nullptr;
    usb::MVLC_USB_Interface *mvlcUSB = nullptr;
    ReadoutBufferQueues &snoopQueues;
    std::array<u32, stacks::ReadoutStackCount> stackTriggers;
    StackCommandBuilder mcstDaqStart;
    StackCommandBuilder mcstDaqStop;
    std::chrono::seconds timeToRun;
    Protected<Counters> counters;
    std::thread readoutThread;
    ReadoutBufferQueues listfileQueues;
    listfile::WriteHandle *lfh = nullptr;
    ReadoutBuffer localBuffer;
    ReadoutBuffer previousData;
    ReadoutBuffer *outputBuffer_ = nullptr;
    u32 nextOutputBufferNumber = 1u;

    Private(MVLC &mvlc_, ReadoutBufferQueues &snoopQueues_)
        : state({})
        , mvlc(mvlc_)
        , snoopQueues(snoopQueues_)
        , counters({})
        , listfileQueues(ListfileWriterBufferSize, ListfileWriterBufferCount)
        , localBuffer(ListfileWriterBufferSize)
        , previousData(ListfileWriterBufferSize)
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
    std::error_code startReadout();
    std::error_code terminateReadout();

    std::error_code readout(size_t &bytesTransferred);
    std::error_code readout_usb(usb::MVLC_USB_Interface *mvlcUSB, size_t &bytesTransferred);
    std::error_code readout_eth(eth::MVLC_ETH_Interface *mvlcETH, size_t &bytesTransferred);
};

constexpr std::chrono::seconds ReadoutWorker::Private::ShutdownReadoutMaxWait;

ReadoutWorker::ReadoutWorker(
    MVLC mvlc,
    const std::array<u32, stacks::ReadoutStackCount> &stackTriggers,
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

ReadoutWorker::ReadoutWorker(
    MVLC mvlc,
    const std::vector<u32> &stackTriggers,
    ReadoutBufferQueues &snoopQueues,
    listfile::WriteHandle *lfh
    )
    : d(std::make_unique<Private>(mvlc, snoopQueues))
{
    d->state.access().ref() = State::Idle;
    d->desiredState = State::Idle;

    d->stackTriggers.fill(0u);
    std::copy_n(std::begin(stackTriggers),
                std::min(stackTriggers.size(), d->stackTriggers.size()),
                std::begin(d->stackTriggers));

    d->lfh = lfh;
}

void ReadoutWorker::setMcstDaqStartCommands(const StackCommandBuilder &commands)
{
    d->mcstDaqStart = commands;
}

void ReadoutWorker::setMcstDaqStopCommands(const StackCommandBuilder &commands)
{
    d->mcstDaqStop = commands;
}

// FIXME: exceptions
void ReadoutWorker::Private::loop(std::promise<std::error_code> promise)
{
#ifndef __WIN32
    prctl(PR_SET_NAME,"readout_worker",0,0,0);
#endif

    std::cout << "readout_worker thread starting" << std::endl;

    counters.access().ref() = {}; // reset the readout counters

    // ConnectionType specifics
    this->mvlcETH = nullptr;
    this->mvlcUSB = nullptr;

    switch (mvlc.connectionType())
    {
        case ConnectionType::ETH:
            this->mvlcETH = dynamic_cast<eth::MVLC_ETH_Interface *>(mvlc.getImpl());
            mvlcETH->resetPipeAndChannelStats(); // reset packet loss counters
            assert(mvlcETH);

            // Send an initial empty frame to the UDP data pipe port so that
            // the MVLC knows where to send the readout data.
            {
                static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
                size_t bytesTransferred = 0;

                if (auto ec = mvlc.write(
                        Pipe::Data,
                        reinterpret_cast<const u8 *>(EmptyRequest.data()),
                        EmptyRequest.size() * sizeof(u32),
                        bytesTransferred))
                {
                    promise.set_value(ec);
                    setState(State::Idle);
                    return;
                }
            }
            break;

        case ConnectionType::USB:
            this->mvlcUSB = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
            assert(mvlcUSB);
            break;
    }

    assert(mvlcETH || mvlcUSB);

    // Reset the MVLC-wide stack error counters
    mvlc.clearStackErrorCounters();

    // listfile writer thread
    Protected<ListfileWriterCounters> writerCounters({});

    auto writerThread = std::thread(
        listfile_buffer_writer,
        lfh,
        std::ref(listfileQueues),
        std::ref(writerCounters));

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

    std::error_code ec = startReadout();

    // Set the promises value now to unblock anyone waiting for startup to
    // complete.
    promise.set_value(ec);

    if (!ec)
    {
        try
        {
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

                        // Also copy the ListfileWriterCounters into our
                        // ReadoutWorker::Counters structure.
                        counters.access()->listfileWriterCounters = writerCounters.access().ref();
                    }
                }

                auto state_ = state.access().copy();

                // stay in running state
                if (likely(state_ == State::Running && desiredState == State::Running))
                {
                    size_t bytesTransferred = 0u;
                    ec = this->readout(bytesTransferred);

                    if (ec == ErrorType::ConnectionError)
                    {
                        std::cout << "Lost connection to MVLC, leaving readout loop. Error="
                            << ec.message() << std::endl;
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
                    startReadout();
                    listfile::BufferWriteHandle wh(*getOutputBuffer());
                    listfile_write_timestamp_section(wh, system_event::subtype::Resume);
                    setState(State::Running);
                    std::cout << "MVLC readout resumed" << std::endl;
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
                    //std::cout << "MVLC readout paused" << std::endl;
                    constexpr auto PauseSleepDuration = std::chrono::milliseconds(100);
                    std::this_thread::sleep_for(PauseSleepDuration);
                }
                else
                {
                    assert(!"invalid code path");
                }

                // Check if the listfile writer caught an exception. This can
                // only happen if we actually do write a listfile.
                // For now just rethrow the exception and let the outer
                // try/catch handle it.
                if (auto eptr = writerCounters.access()->eptr)
                {
                    std::rethrow_exception(eptr);
                }
            }
        }
        catch (...)
        {
            counters.access()->eptr = std::current_exception();
        }
    }

    // DAQ stop/termination sequence
    std::cout << "MVLC readout stopping" << std::endl;
    setState(State::Stopping);

    auto tTerminateStart = std::chrono::steady_clock::now();
    terminateReadout();
    auto tTerminateEnd = std::chrono::steady_clock::now();

    auto terminateDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        tTerminateEnd - tTerminateStart);

    {
        auto c = counters.access();
        c->tTerminateStart = tTerminateStart;
        c->tTerminateEnd = tTerminateEnd;
    }

    std::cout << "terminateReadout() took " << terminateDuration.count()
        << " ms to complete" << std::endl;

    // Write EndRun and EndOfFile system event sections into a ReadoutBuffer
    // and immediately flush the buffer.
    if (writerCounters.access()->state == ListfileWriterCounters::Running)
    {
        listfile::BufferWriteHandle wh(*getOutputBuffer());
        listfile_write_timestamp_section(wh, system_event::subtype::EndRun);
        listfile_write_system_event(wh, system_event::subtype::EndOfFile);
        flushCurrentOutputBuffer();
    }

    maybePutBackSnoopBuffer();

    // stop the listfile writer
    if (writerCounters.access()->state == ListfileWriterCounters::Running)
    {
        auto sentinel = listfileQueues.emptyBufferQueue().dequeue_blocking();
        sentinel->clear();
        listfileQueues.filledBufferQueue().enqueue(sentinel);
    }

    if (writerThread.joinable())
        writerThread.join();

    // Final copy of the listfile writer counters to our
    // ReadoutWorker::Counters structure.
    counters.access()->listfileWriterCounters = writerCounters.access().ref();

    // Record the final tEnd
    {
        auto tEnd = std::chrono::steady_clock::now();
        auto c = counters.access();
        c->tEnd = tEnd;
        c->ec = ec;
    }

    // Check that all buffers from the listfile writer queue have been returned.
    assert(listfileQueues.emptyBufferQueue().size() == ListfileWriterBufferCount);

    setState(State::Idle);
}

// Start readout or resume after pause
// Run the last part of the init sequence in parallel to reading from the data pipe.
// The last part enables the stack triggers, runs the multicast daq start
// sequence and enables MVLC daq mode.
std::error_code ReadoutWorker::Private::startReadout()
{
    auto f = std::async(
        std::launch::async,
        [this] ()
        {
            // enable readout stacks
            if (auto ec = setup_readout_triggers(mvlc, stackTriggers))
                return ec;

            // multicast daq start
            std::vector<u32> responseBuffer;
            auto errors = execute_stack(mvlc, mcstDaqStart, responseBuffer);
            auto execResults = parse_stack_exec_response(mcstDaqStart, responseBuffer, errors);

            cout << "Results from mcstDaqStart: " << execResults << endl;

            if (auto ec = get_first_error(errors))
                return ec;

            // enable daq mode
            return enable_daq_mode(mvlc);
        });

    while (f.valid() && !is_ready(f))
    {
        size_t bytesTransferred = 0u;
        this->readout(bytesTransferred);
    }

    std::error_code ec = f.get();

    if (ec)
    {
        cerr << "startReadout: error running daq start sequence: " << ec.message() << endl;
    }

    return ec;
}

// Cleanly end a running readout session. The code disables all triggers by
// writing to the trigger registers via the command pipe while in parallel
// reading and processing data from the data pipe until no more data
// arrives. These things have to be done in parallel as otherwise in the
// case of USB the data from the data pipe could clog the bus and no
// replies could be received on the command pipe.
std::error_code ReadoutWorker::Private::terminateReadout()
{
    auto f = std::async(
        std::launch::async,
        [this] ()
        {
            // disable daq mode
            if (auto ec = disable_daq_mode(mvlc))
                return ec;

            // multicast daq stop
            std::vector<u32> responseBuffer;
            auto errors = execute_stack(mvlc, mcstDaqStop, responseBuffer);
            auto execResults = parse_stack_exec_response(mcstDaqStop, responseBuffer, errors);
            if (auto ec = get_first_error(errors))
                return ec;

            // disable readout stacks
            static const int DisableTriggerRetryCount = 5;

            for (int try_ = 0; try_ < DisableTriggerRetryCount; try_++)
            {
                if (auto ec = disable_all_triggers_and_daq_mode(this->mvlc))
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

    std::error_code ec = f.get();

    if (ec)
    {
        cerr << "terminateReadout: error running daq stop sequence: " << ec.message() << endl;
    }

    return ec;
}

// Note: in addition to stack frames this includes SystemEvent frames. These
// are written into the readout buffers by the listfile_write_* functions.
inline bool is_valid_readout_frame(const FrameInfo &frameInfo)
{
    return (frameInfo.type == frame_headers::StackFrame
            || frameInfo.type == frame_headers::StackContinuation
            || frameInfo.type == frame_headers::SystemEvent);
}

// Ensure that the readBuffer contains only complete frames. In other words: if
// a frame starts then it should fully fit into the readBuffer. Trailing data
// is moved to the tempBuffer.
//
// Walk through the readBuffer following the frame structure. If a partial
// frame is found at the end of the buffer move the trailing bytes to the
// tempBuffer and shrink the readBuffer accordingly.
//
// Note that invalid data words (ones that do not pass
// is_valid_readout_frame()) are just skipped and left in the buffer without
// modification. This has to be taken into account on the analysis side.
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
            u32 frameHeader = 0u;

            while (view.size() >= sizeof(u32))
            {
                frameHeader = *reinterpret_cast<const u32 *>(&view[0]);
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
                cout << fmt::format("non valid readout frame: frameHeader=0x{:08x}", frameHeader) << endl;

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

            if (frameInfo.type == frame_headers::StackFrame
                || frameInfo.type == frame_headers::StackContinuation)
            {
                ++counters.access()->stackHits[frameInfo.stack];
            }

            // Skip over the frameHeader and the frame contents.
            view.remove_prefix((frameInfo.len + 1) * sizeof(u32));
        }
    }
}

static const std::chrono::milliseconds FlushBufferTimeout(500);

// Code to add delays after reading from the MVLC.
#define ENABLE_ARTIFICIAL_READ_DELAYS 0

#if ENABLE_ARTIFICIAL_READ_DELAYS
static const std::chrono::milliseconds DebugPostReadoutDelay(10);
static const std::chrono::milliseconds DebugPostReadoutDelayIncrement(30);
static const size_t StartDelayBufferNumber = 500;
#endif

std::error_code ReadoutWorker::Private::readout(size_t &bytesTransferred)
{
    assert(this->mvlcETH || this->mvlcUSB);

    std::error_code ec = {};

    if (mvlcUSB)
        ec = readout_usb(mvlcUSB, bytesTransferred);
    else
        ec = readout_eth(mvlcETH, bytesTransferred);

#if ENABLE_ARTIFICIAL_READ_DELAYS == 1
    // Static Delay
    if (DebugPostReadoutDelay.count() > 0)
        std::this_thread::sleep_for(DebugPostReadoutDelay);
#elif ENABLE_ARTIFICIAL_READ_DELAYS == 2
    // Increasing delay based on buffer number
    if (DebugPostReadoutDelay.count() > 0)
        std::this_thread::sleep_for(DebugPostReadoutDelay
                                    + DebugPostReadoutDelayIncrement * nextOutputBufferNumber);
#elif ENABLE_ARTIFICIAL_READ_DELAYS == 3
    // Delay starts after StartDelayBufferNumbers buffers haven been filled
    if (DebugPostReadoutDelay.count() > 0 && nextOutputBufferNumber > StartDelayBufferNumber)
        std::this_thread::sleep_for(DebugPostReadoutDelay);
#endif
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

    destBuffer->ensureFreeSpace(usb::USBStreamPipeReadSize);

    while (destBuffer->free() >= usb::USBStreamPipeReadSize)
    {
        const size_t bytesToRead = usb::USBStreamPipeReadSize;
        size_t bytesTransferred = 0u;

        auto dataGuard = mvlc.getLocks().lockData();
        ec = mvlcUSB->read_unbuffered(
            Pipe::Data,
            destBuffer->data() + destBuffer->used(),
            bytesToRead,
            bytesTransferred);
        dataGuard.unlock();

        destBuffer->use(bytesTransferred);
        totalBytesTransferred += bytesTransferred;

        if (ec == ErrorType::ConnectionError)
        {
            //cerr << "connection error from usb::Impl::read_unbuffered(): " << ec.message() << endl;
            break;
        }

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= FlushBufferTimeout)
        {
            //cerr << "flush buffer timeout reached, leaving reaodut_usb()" << endl;
            break;
        }
    }

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
    std::array<size_t, stacks::StackCount> stackHits = {};

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

            // Record stack hits in the local counters array.
            count_stack_hits(result, stackHits);

            // A crude way of handling packets with residual bytes at the end. Just
            // subtract the residue from buffer->used which means the residual
            // bytes will be overwritten by the next packets data. This will at
            // least keep the structure somewhat intact assuming that the
            // dataWordCount in header0 is correct. Note that this case does not
            // happen, the MVLC never generates packets with residual bytes.
            if (unlikely(result.leftoverBytes()))
            {
                //std::cout << "Oi! There's residue here!" << std::endl; // TODO: log a warning instead of using cout
                destBuffer->setUsed(destBuffer->used() - result.leftoverBytes());
            }

            auto elapsed = std::chrono::steady_clock::now() - tStart;

            if (elapsed >= FlushBufferTimeout)
                break;
        }
    } // with dataGuard

    // Copy the ethernet pipe stats and the stack hits into the Counters
    // structure. The getPipeStats() access is thread-safe in the eth
    // implementation.
    {
        auto c = counters.access();

        c->ethStats = mvlcETH->getPipeStats();

        for (size_t stack=0; stack<stackHits.size(); ++stack)
            c->stackHits[stack] += stackHits[stack];
    }

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

bool count_stack_hits(const eth::PacketReadResult &prr, StackHits &stackHits)
{
    if (prr.isNextHeaderPointerValid())
    {
        const u32 *frameHeaderPointer = prr.payloadBegin() + prr.nextHeaderPointer();

        while (frameHeaderPointer < prr.payloadEnd())
        {
            auto fi = extract_frame_info(*frameHeaderPointer);

            if (fi.type == frame_headers::StackFrame || fi.type == frame_headers::StackContinuation)
                ++stackHits[fi.stack];
            else
                return false;

            frameHeaderPointer += fi.len + 1;
        }
    }

    return true;
}

const char *readout_worker_state_to_string(const ReadoutWorker::State &state)
{
    switch (state)
    {
        case ReadoutWorker::State::Idle:
            return "Idle";
        case ReadoutWorker::State::Starting:
            return "Starting";
        case ReadoutWorker::State::Running:
            return "Running";
        case ReadoutWorker::State::Paused:
            return "Paused";
        case ReadoutWorker::State::Stopping:
            return "Stopping";
    }

    return "unknown";
}

} // end namespace mvlc
} // end namespace mesytec
