#include "mvlc_replay_worker.h"

#include <cstring>
#include <thread>

#if __linux__
#include <sys/prctl.h>
#endif

#include "mesytec-mvlc/mvlc_util.h"
#include "mesytec-mvlc/mvlc_eth_interface.h"
#include "util/perf.h"
#include "util/logging.h"

namespace mesytec
{
namespace mvlc
{

namespace
{

class ReplayErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "replay_error";
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

const ReplayErrorCategory theReplayErrorCateogry {};

constexpr auto FreeBufferWaitTimeout_ms = std::chrono::milliseconds(100);

// Follows the framing structure inside the buffer until an incomplete frame
// which doesn't fit into the buffer is detected. The incomplete data is moved
// over to the tempBuffer so that the readBuffer ends with a complete frame.
//
// The input buffer must start with a frame header (skip_count will be called
// with the first word of the input buffer on the first iteration).
//
// The SkipCountFunc must return the number of words to skip to get to the next
// frame header or 0 if there is not enough data left in the input iterator to
// determine the frames size.
// Signature of SkipCountFunc:  u32 skip_count(const basic_string_view<const u8> &view);
template<typename SkipCountFunc>
inline void fixup_buffer(
    ReadoutBuffer &readBuffer,
    ReadoutBuffer &tempBuffer,
    SkipCountFunc skip_count)
{
    auto view = readBuffer.viewU8();

    while (!view.empty())
    {
        if (view.size() >= sizeof(u32))
        {
            u32 wordsToSkip = skip_count(view);

            //cout << "wordsToSkip=" << wordsToSkip << ", view.size()=" << view.size() << ", in words:" << view.size() / sizeof(u32));

            if (wordsToSkip == 0 || wordsToSkip > view.size() / sizeof(u32))
            {
                // Move the trailing data into the temporary buffer. This will
                // truncate the readBuffer to the last complete frame or packet
                // boundary.


                tempBuffer.ensureFreeSpace(view.size());

                std::memcpy(tempBuffer.data() + tempBuffer.used(),
                            view.data(), view.size());
                tempBuffer.use(view.size());
                readBuffer.setUsed(readBuffer.used() - view.size());
                return;
            }

            // Skip over the SystemEvent frame or the ETH packet data.
            view.remove_prefix(wordsToSkip * sizeof(u32));
        }
    }
}

} // end anon namespace

std::error_code make_error_code(ReplayWorkerError error)
{
    return { static_cast<int>(error), theReplayErrorCateogry };
}

// The listfile contains two types of data:
// - System event sections identified by a header word with 0xFA in the highest
//   byte.
// - ETH packet data starting with the two ETH specific header words followed
//   by the packets payload
// The first ETH packet header can never have the value 0xFA because the
// highest two bits are always 0.
void fixup_buffer_eth(ReadoutBuffer &readBuffer, ReadoutBuffer &tempBuffer)
{
    auto skip_func = [](const basic_string_view<const u8> &view) -> u32
    {
        if (view.size() < sizeof(u32))
            return 0u;

        // Either a SystemEvent header or the first of the two ETH packet headers
        u32 header = *reinterpret_cast<const u32 *>(view.data());

        if (get_frame_type(header) == frame_headers::SystemEvent)
            return 1u + extract_frame_info(header).len;

        if (view.size() >= 2 * sizeof(u32))
        {
            u32 header1 = *reinterpret_cast<const u32 *>(view.data() + sizeof(u32));
            eth::PayloadHeaderInfo ethHdrs{ header, header1 };
            return eth::HeaderWords + ethHdrs.dataWordCount();
        }

        // Not enough data to get the 2nd ETH header word.
        return 0u;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}

void fixup_buffer_usb(ReadoutBuffer &readBuffer, ReadoutBuffer &tempBuffer)
{
    auto skip_func = [] (const basic_string_view<const u8> &view) -> u32
    {
        if (view.size() < sizeof(u32))
            return 0u;

        u32 header = *reinterpret_cast<const u32 *>(view.data());
        return 1u + extract_frame_info(header).len;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}


struct ReplayWorker::Private
{
    WaitableProtected<ReplayWorker::State> state;
    std::atomic<ReplayWorker::State> desiredState;
    ReadoutBufferQueues &snoopQueues;
    listfile::ReadHandle *lfh = nullptr;
    ConnectionType listfileFormat;
    Protected<Counters> counters;
    ReadoutBuffer previousData;
    ReadoutBuffer *outputBuffer_ = nullptr;
    u32 nextOutputBufferNumber = 1u;
    std::thread replayThread;
    std::shared_ptr<spdlog::logger> logger;

    Private(
        ReadoutBufferQueues &snoopQueues_,
        listfile::ReadHandle *lfh_)
        : state({})
        , snoopQueues(snoopQueues_)
        , lfh(lfh_)
        , counters({})
        , logger(get_logger("replay"))
    {}

    ~Private()
    {
        if (replayThread.joinable())
            replayThread.join();
    }

    void setState(const ReplayWorker::State &state_)
    {
        state.access().ref() = state_;
        desiredState = state_;
        counters.access()->state = state_;
    }

    void loop(std::promise<std::error_code> promise);

    ReadoutBuffer *getOutputBuffer()
    {
        if (!outputBuffer_)
        {
            outputBuffer_ = snoopQueues.emptyBufferQueue().dequeue(FreeBufferWaitTimeout_ms);

            if (outputBuffer_)
            {
                outputBuffer_->clear();
                outputBuffer_->setBufferNumber(nextOutputBufferNumber++);
                outputBuffer_->setType(listfileFormat);
            }
        }

        return outputBuffer_;
    }

    void maybePutBackSnoopBuffer()
    {
        if (outputBuffer_)
            snoopQueues.emptyBufferQueue().enqueue(outputBuffer_);

        outputBuffer_ = nullptr;
    }

    void flushCurrentOutputBuffer()
    {
        if (outputBuffer_ && outputBuffer_->used() > 0)
        {
            snoopQueues.filledBufferQueue().enqueue(outputBuffer_);
            counters.access()->buffersFlushed++;
            outputBuffer_ = nullptr;
        }
    }
};

ReplayWorker::ReplayWorker(
    ReadoutBufferQueues &snoopQueues,
    listfile::ReadHandle *lfh)
    : d(std::make_unique<Private>(snoopQueues, lfh))
{
    assert(lfh);

    d->state.access().ref() = State::Idle;
    d->desiredState = State::Idle;
}

void ReplayWorker::Private::loop(std::promise<std::error_code> promise)
{
#if __linux__
    prctl(PR_SET_NAME,"replay_worker",0,0,0);
#endif

    logger->debug("replay_worker thread starting");

    counters.access().ref() = {}; // reset the counters

    auto preamble = listfile::read_preamble(*lfh);

    if (preamble.magic == listfile::get_filemagic_eth())
        listfileFormat = ConnectionType::ETH;
    else if (preamble.magic == listfile::get_filemagic_usb())
        listfileFormat = ConnectionType::USB;
    else
    {
        promise.set_value(make_error_code(ReplayWorkerError::UnknownListfileFormat));
        return;
    }

    auto tStart = std::chrono::steady_clock::now();
    counters.access()->tStart = tStart;
    setState(State::Running);

    // Set the promises value thus unblocking anyone waiting for the startup to complete.
    promise.set_value({});

    try
    {
        while (true)
        {
            auto state_ = state.access().copy();

            // stay in running state
            if (likely(state_ == State::Running && desiredState == State::Running))
            {
                if (auto destBuffer = getOutputBuffer())
                {
                    if (previousData.used())
                    {
                        destBuffer->ensureFreeSpace(previousData.used());
                        std::memcpy(destBuffer->data() + destBuffer->used(),
                                    previousData.data(), previousData.used());
                        destBuffer->use(previousData.used());
                        previousData.clear();
                    }

                    size_t bytesRead = lfh->read(
                        destBuffer->data() + destBuffer->used(),
                        destBuffer->free());
                    destBuffer->use(bytesRead);

                    if (bytesRead == 0)
                        break;

                    {
                        auto c = counters.access();
                        ++c->buffersRead;
                        c->bytesRead += bytesRead;
                    }

                    //mvlc::util::log_buffer(std::cout, destBuffer->viewU32(), "mvlc_replay: pre fixup workBuffer");

                    switch (listfileFormat)
                    {
                        case ConnectionType::ETH:
                            fixup_buffer_eth(*destBuffer, previousData);
                            break;

                        case ConnectionType::USB:
                            fixup_buffer_usb(*destBuffer, previousData);
                            break;
                    }

                    //mvlc::util::log_buffer(std::cout, destBuffer->viewU32(), "mvlc_replay: post fixup workBuffer");

                    flushCurrentOutputBuffer();
                }
            }
            // pause
            else if (state_ == State::Running && desiredState == State::Paused)
            {
                setState(State::Paused);
                logger->debug("MVLC replay paused");
            }
            // resume
            else if (state_ == State::Paused && desiredState == State::Running)
            {
                setState(State::Running);
                logger->debug("MVLC replay resumed");
            }
            // stop
            else if (desiredState == State::Stopping)
            {
                logger->debug("MVLC replay requested to stop");
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
        }
    }
    catch (...)
    {
        counters.access()->eptr = std::current_exception();
    }

    setState(State::Stopping);

    counters.access()->tEnd = std::chrono::steady_clock::now();
    maybePutBackSnoopBuffer();

    setState(State::Idle);
}

std::future<std::error_code> ReplayWorker::start()
{
    std::promise<std::error_code> promise;
    auto f = promise.get_future();

    if (d->state.access().ref() != State::Idle)
    {
        promise.set_value(make_error_code(ReplayWorkerError::ReplayNotIdle));
        return f;
    }

    d->setState(State::Starting);

    d->replayThread = std::thread(&Private::loop, d.get(), std::move(promise));

    return f;
}

std::error_code ReplayWorker::stop()
{
    auto state_ = d->state.access().copy();

    if (state_ == State::Idle || state_ == State::Stopping)
        return make_error_code(ReplayWorkerError::ReplayNotRunning);

    d->desiredState = State::Stopping;
    return {};
}

std::error_code ReplayWorker::pause()
{
    auto state_ = d->state.access().copy();

    if (state_ != State::Running)
        return make_error_code(ReplayWorkerError::ReplayNotRunning);

    d->desiredState = State::Paused;
    return {};
}

std::error_code ReplayWorker::resume()
{
    auto state_ = d->state.access().copy();

    if (state_ != State::Paused)
        return make_error_code(ReplayWorkerError::ReplayNotPaused);

    d->desiredState = State::Running;
    return {};
}

ReadoutBufferQueues &ReplayWorker::snoopQueues()
{
    return d->snoopQueues;
}

ReplayWorker::~ReplayWorker()
{
}

ReplayWorker::State ReplayWorker::state() const
{
    return d->state.access().copy();
}

WaitableProtected<ReplayWorker::State> &ReplayWorker::waitableState()
{
    return d->state;
}

ReplayWorker::Counters ReplayWorker::counters()
{
    return d->counters.copy();
}

} // end namespace mvlc
} // end namespace mesytec
