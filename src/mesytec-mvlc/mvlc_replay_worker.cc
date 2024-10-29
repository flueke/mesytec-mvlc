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
        switch (static_cast<ReplayWorkerError>(ev))
        {
        case ReplayWorkerError::NoError:
            return "Ok";
        case ReplayWorkerError::ReplayNotIdle:
            return "Replay not idle";
        case ReplayWorkerError::ReplayNotRunning:
            return "Replay not running";
        case ReplayWorkerError::ReplayNotPaused:
            return "Replay not paused";
        case ReplayWorkerError::UnknownListfileFormat:
            return "Unknown listfile format";
        }

        return fmt::format("unknown error ({})", ev);
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        // No dedicated error condition yet so just return a default value.
        (void) ev;
        return {};
    }
};

const ReplayErrorCategory theReplayErrorCateogry {};

constexpr auto FreeBufferWaitTimeout_ms = std::chrono::milliseconds(100);

} // end anon namespace

std::error_code make_error_code(ReplayWorkerError error)
{
    return { static_cast<int>(error), theReplayErrorCateogry };
}

struct ReplayWorker::Private
{
    WaitableProtected<ReplayWorker::State> state;
    std::atomic<ReplayWorker::State> desiredState;
    ReadoutBufferQueues &snoopQueues;
    listfile::ReadHandle *lfh = nullptr;
    ConnectionType listfileFormat;
    Protected<Counters> counters;
    std::vector<u8> previousData;
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
        , counters()
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

    // TODO: guard against exceptions from read_preamble()
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
                    if (!previousData.empty())
                    {
                        // move bytes from previousData into destBuffer
                        destBuffer->ensureFreeSpace(previousData.size());
                        std::copy(std::begin(previousData), std::end(previousData),
                            destBuffer->data() + destBuffer->used());
                        destBuffer->use(previousData.size());
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

                    size_t bytesMoved = fixup_buffer(listfileFormat,
                        destBuffer->data(), destBuffer->used(), previousData);
                    destBuffer->setUsed(destBuffer->used() - bytesMoved);

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

ReadoutBufferQueues *ReplayWorker::snoopQueues()
{
    return &d->snoopQueues;
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
