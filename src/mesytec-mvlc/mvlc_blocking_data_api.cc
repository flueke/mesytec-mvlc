#include "mesytec-mvlc/mvlc_blocking_data_api.h"

#include "util/logging.h"

namespace mesytec
{
namespace mvlc
{

struct BlockingContext
{
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;    // true if there is an event available to be processed
    bool processed_ = true; // true if the current event has been processed
    EventContainer event_;
};

namespace
{
    // Readout parser callback for event data
    void event_data_blocking(
        void *userContext,
        int eventIndex,
        const readout_parser::ModuleData *moduleDataList,
        unsigned moduleCount)
    {
        auto &ctx = *reinterpret_cast<BlockingContext *>(userContext);

        {
            // Wait until the last event has been processed.
            std::unique_lock<std::mutex> lock(ctx.mutex_);
            ctx.cv_.wait(lock, [&ctx] { return ctx.processed_; });

            // Copy the callback args into the context event structure.
            ctx.event_.type = EventContainer::Type::Readout;
            ctx.event_.readout = { eventIndex, moduleDataList, moduleCount };
            ctx.event_.system = {};

            // Update state
            ctx.ready_ = true;
            ctx.processed_ = false;
        }

        // Notify the main thread (blocked in next_event()).
        ctx.cv_.notify_one();
    }

    // Readout parser callback for system events
    void system_event_blocking(
        void *userContext,
        const u32 *header,
        u32 size)
    {
        auto &ctx = *reinterpret_cast<BlockingContext *>(userContext);

        {
            // Wait until the last event has been processed.
            std::unique_lock<std::mutex> lock(ctx.mutex_);
            ctx.cv_.wait(lock, [&ctx] { return ctx.processed_; });

            // Copy the callback args into the context event structure.
            ctx.event_.type = EventContainer::Type::System;
            ctx.event_.readout = {};
            ctx.event_.system = { header, size };

            // Update state
            ctx.ready_ = true;
            ctx.processed_ = false;
        }

        // Notify the main thread (blocked in next_event()).
        ctx.cv_.notify_one();
    }

    // Generic monitor function working with both ReadoutWorker and
    // ReplayWorker types.
    template<typename Worker>
    void monitor(
        Worker &worker,
        std::thread &parserThread,
        std::atomic<bool> &parserQuit,
        BlockingContext &ctx)
    {
        auto logger = get_logger("mvlc_blocking_api");

        logger->info("monitor() waiting for idle producer");

        // Wait until the readout/replay data producer transitioned to idle state.
        worker.waitableState().wait(
            [] (const typename Worker::State &state)
            {
                return state == Worker::State::Idle;
            });

        // Ensure that all buffers have been processed by the parserThread
        logger->info("monitor() waiting for filled buffer queue to become empty");

        while (!worker.snoopQueues().filledBufferQueue().empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Tell the parser to quit and wait for it to exit.
        logger->info("monitor() telling parserThread to quit");
        parserQuit = true;

        if (parserThread.joinable())
            parserThread.join();

        logger->info("monitor() creating final event and notifying main");

        {
            // Wait until the last event has been processed.
            std::unique_lock<std::mutex> lock(ctx.mutex_);
            ctx.cv_.wait(lock, [&ctx] { return ctx.processed_; });

            // Setup the special 'None' event type so that the main loop knows it's
            // time to quit.
            ctx.event_.type = EventContainer::Type::None;
            ctx.event_.readout = {};
            ctx.event_.system = {};

            // Update state
            ctx.ready_ = true;
            ctx.processed_ = false;
        }

        // Notify the main thread (blocked in next_event()).
        ctx.cv_.notify_one();

        logger->info("monitor() done");
    }
} // end anon namespace

// readout

struct BlockingReadout::Private
{
    std::unique_ptr<MVLCReadout> rdo_;
    std::unique_ptr<BlockingContext> ctx_;
    std::thread monitorThread;
};

BlockingReadout::BlockingReadout()
    : d(std::make_unique<Private>())
{
}

BlockingReadout::~BlockingReadout()
{
    if (d && d->monitorThread.joinable())
        d->monitorThread.join();
}

BlockingReadout::BlockingReadout(BlockingReadout &&other)
{
    d = std::move(other.d);
}

BlockingReadout &BlockingReadout::operator=(BlockingReadout &&other)
{
    d = std::move(other.d);
    return *this;
}

std::error_code BlockingReadout::start(const std::chrono::seconds &timeToRun)
{
    if (auto ec = d->rdo_->start(timeToRun))
        return ec;

    if (d->monitorThread.joinable())
        d->monitorThread.join();

    d->monitorThread = std::thread(
        monitor<ReadoutWorker>,
        std::ref(d->rdo_->readoutWorker()),
        std::ref(d->rdo_->parserThread()),
        std::ref(d->rdo_->parserQuit()),
        std::ref(*d->ctx_)
        );

    return {};
}

BlockingReadout make_mvlc_readout_blocking(
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReadout>(make_mvlc_readout(
            crateConfig,
            listfileParams,
            parserCallbacks,
            ctx.get()
            ));

    BlockingReadout result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

BlockingReadout make_mvlc_readout_blocking(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReadout>(make_mvlc_readout(
            mvlc,
            crateConfig,
            listfileParams,
            parserCallbacks,
            ctx.get()
            ));

    BlockingReadout result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

BlockingReadout make_mvlc_readout_blocking(
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReadout>(make_mvlc_readout(
            crateConfig,
            listfileWriteHandle,
            parserCallbacks,
            ctx.get()
            ));

    BlockingReadout result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

BlockingReadout make_mvlc_readout_blocking(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReadout>(make_mvlc_readout(
            mvlc,
            crateConfig,
            listfileWriteHandle,
            parserCallbacks,
            ctx.get()
            ));

    BlockingReadout result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

EventContainer next_event(BlockingReadout &br)
{
    auto &ctx = *br.d->ctx_;

    // Notify the parser that the current event has been processed and the data
    // may be discarded.
    {
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.ready_ = false;
        ctx.processed_ = true;
    }

    ctx.cv_.notify_one();

    // Wait for the parser to fill the event structure with data from the next
    // event.
    {
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.cv_.wait(lock, [&ctx] { return ctx.ready_; });
        assert(ctx.ready_);
        assert(!ctx.processed_);
    }

    return ctx.event_;
}

// replay

struct BlockingReplay::Private
{
    std::unique_ptr<MVLCReplay> rdo_;
    std::unique_ptr<BlockingContext> ctx_;
    std::thread monitorThread;
};

BlockingReplay::BlockingReplay()
    : d(std::make_unique<Private>())
{
}

BlockingReplay::~BlockingReplay()
{
    if (d && d->monitorThread.joinable())
        d->monitorThread.join();
}

BlockingReplay::BlockingReplay(BlockingReplay &&other)
{
    d = std::move(other.d);
}

BlockingReplay &BlockingReplay::operator=(BlockingReplay &&other)
{
    d = std::move(other.d);
    return *this;
}

std::error_code BlockingReplay::start()
{
    if (auto ec = d->rdo_->start())
        return ec;

    if (d->monitorThread.joinable())
        d->monitorThread.join();

    d->monitorThread = std::thread(
        monitor<ReplayWorker>,
        std::ref(d->rdo_->replayWorker()),
        std::ref(d->rdo_->parserThread()),
        std::ref(d->rdo_->parserQuit()),
        std::ref(*d->ctx_)
        );

    return {};
}

BlockingReplay make_mvlc_replay_blocking(
    const std::string &listfileArchiveName)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReplay>(make_mvlc_replay(
        listfileArchiveName,
        parserCallbacks,
        ctx.get()));

    BlockingReplay result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

BlockingReplay make_mvlc_replay_blocking(
    const std::string &listfileArchiveName,
    const std::string &listfileArchiveMemberName)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReplay>(make_mvlc_replay(
        listfileArchiveName,
        listfileArchiveMemberName,
        parserCallbacks,
        ctx.get()));

    BlockingReplay result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

BlockingReplay make_mvlc_replay_blocking(
    listfile::ReadHandle *lfh)
{
    readout_parser::ReadoutParserCallbacks parserCallbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto ctx = std::make_unique<BlockingContext>();

    auto rdo = std::make_unique<MVLCReplay>(make_mvlc_replay(
        lfh,
        parserCallbacks,
        ctx.get()));

    BlockingReplay result;
    result.d->ctx_ = std::move(ctx);
    result.d->rdo_ = std::move(rdo);
    return result;
}

EventContainer next_event(BlockingReplay &br)
{
    auto &ctx = *br.d->ctx_;

    // Notify the parser that the current event has been processed and the data
    // may be discarded.
    {
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.ready_ = false;
        ctx.processed_ = true;
    }

    ctx.cv_.notify_one();

    // Wait for the parser to fill the event structure with data from the next
    // event.
    {
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.cv_.wait(lock, [&ctx] { return ctx.ready_; });
        assert(ctx.ready_);
        assert(!ctx.processed_);
    }

    return ctx.event_;
}

}
}
