#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <unordered_map>

using namespace mesytec::mvlc;
using readout_parser::ModuleData;
using readout_parser::ReadoutParserCallbacks;;

struct Event
{
    enum class Type
    {
        None,
        Readout,
        System
    };

    struct Readout
    {
        int eventIndex;
        const ModuleData *moduleDataList;
        unsigned moduleCount;
    };

    struct System
    {
        const u32 *header;
        u32 size;
    };

    Type type;
    Readout readout;
    System system;

    inline operator bool() const
    {
        return type != Type::None;
    }
};

struct SyncContext
{
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;    // true if there is an event available to be processed
    bool processed_ = true; // true if the current event has been processed
    Event event_;
};

Event next_event(SyncContext &ctx)
{
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

// Readout parser callback for event data
void event_data_blocking(
    void *userContext,
    int eventIndex,
    const ModuleData *moduleDataList,
    unsigned moduleCount)
{
    auto &ctx = *reinterpret_cast<SyncContext *>(userContext);

    {
        // Wait until the last event has been processed.
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.cv_.wait(lock, [&ctx] { return ctx.processed_; });

        // Copy the callback args into the context event structure.
        ctx.event_.type = Event::Type::Readout;
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
    auto &ctx = *reinterpret_cast<SyncContext *>(userContext);

    {
        // Wait until the last event has been processed.
        std::unique_lock<std::mutex> lock(ctx.mutex_);
        ctx.cv_.wait(lock, [&ctx] { return ctx.processed_; });

        // Copy the callback args into the context event structure.
        ctx.event_.type = Event::Type::System;
        ctx.event_.readout = {};
        ctx.event_.system = { header, size };

        // Update state
        ctx.ready_ = true;
        ctx.processed_ = false;
    }

    // Notify the main thread (blocked in next_event()).
    ctx.cv_.notify_one();
}


// Generic monitory function working with ReadoutWorker and ReplayWorker
// instances.
template<typename Worker>
void monitor(
    Worker &worker,
    std::thread &parserThread,
    std::atomic<bool> &parserQuit,
    SyncContext &ctx)
{
    auto logger = spdlog::get("readout_parser_sync");

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
        ctx.event_.type = Event::Type::None;
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

int main(int argc, char *argv[])
{
    SyncContext ctx;

    ReadoutParserCallbacks callbacks =
    {
        event_data_blocking,
        system_event_blocking
    };

    auto replay = make_mvlc_replay(argv[1], callbacks, &ctx);

    if (auto ec = replay.start())
        return 1;

    auto monitorThread = std::thread(
        monitor<ReplayWorker>,
        std::ref(replay.replayWorker()),
        std::ref(replay.parserThread()),
        std::ref(replay.parserQuit()),
        std::ref(ctx)
        );

    size_t nSystems = 0;
    size_t nReadouts = 0;
    std::unordered_map<u8, size_t> sysEventTypes;
    std::unordered_map<int, size_t> eventHits;

    while (auto event = next_event(ctx))
    {
        if (event.type == Event::Type::System)
        {
            nSystems++;
            u8 t = system_event::extract_subtype(*event.system.header);
            sysEventTypes[t] += 1u;
        }
        else if (event.type == Event::Type::Readout)
        {
            nReadouts++;
            eventHits[event.readout.eventIndex] += 1;
        }
    }

    if (monitorThread.joinable())
        monitorThread.join();

    spdlog::info("nSystems={}, nReadouts={}",
                 nSystems, nReadouts);

    for (const auto &kv: sysEventTypes)
    {
        u8 sysEvent = kv.first;
        size_t count = kv.second;
        auto sysEventName = system_event_type_to_string(sysEvent);
        spdlog::info("system event {}: {}", sysEventName, count);
    }

    for (const auto &kv: eventHits)
    {
        int eventIndex = kv.first;
        size_t count = kv.second;

        spdlog::info("hits for event {}: {}", eventIndex, count);
    }


    return 0;
}
