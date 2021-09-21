TODO {#todo}
============

* Better mvlc factory to create an mvlc based on a connection string

* Core API for reading listfiles and working with the data:
  - Multiple views:
    - linear readout data (does not need crate config)
    - parsed readout data
  - Single threaded
  - Example:
    auto lfh = open_listfile("my_run01.zip");
    while (auto event = next_event(lfh))
    {
        event->index;
        event->modules[0].data.begin;
        event->modules[0].data.end;
    }


    // Larger example:
    // Need to figure out how to leave the next_event() loop.
    // Readout/Replay need to be Idle and the snoopQueues must be empty (all data consumed).
    // Then parserQuit needs to be set for run_readout_parser() to leave the loop.
    // In this state next_event() needs to return a 'None' type event or some other value to test against.
    // Maybe add a third monitor thread watchng both the readout/replay
    // (producer) and the parser sides (consumer).

    while (auto event = next_event(syncContext))
    {
        if (event.type == SystemEvent)
        {
            // use event.system
        }
        else if (event.type == ReadoutEvent)
        {
            // use event.readout
        }
    }

    struct Event
    {
        struct Readout
        {
            int eventIndex;
            const ModuleData *moduleDataList;
            unsigned moduleCount;
        };

        struct System
        {
            u32 *header;
            u32 size;
        };

        enum class Type { None, Readout, System };

        Type type;
        Readout readout;
        System system;
    }

    struct SyncContext
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false;
        bool processed = true;
        bool done = false;

        Event event;
    };

    Event next_event(SyncContext &ctx)
    {
        {
            std::unique_lock<std::mutex> lock(ctx.mutex);
            ctx.ready = false;
            ctx.processed = true;
        }

        cv.notify_one();

        {
            std::unique_lock<std::mutex> lock(ctx.mutex);
            ctx.cv.wait(lock, [&ctx] { return ctx.ready || ctx.done; });
            assert(ctx.ready == true || ctx.done == true);
            assert(ctx.processed == false);
        }

        return ctx.event;
    }

    // FIXME: need SyncContext
    void event_data_callback(int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        {
            std::unique_lock<std::mutex> lock(ctx.mutex);
            ctx.cv.wait(lock, [&ctx] { return ctx.processed; });

            ctx.event.type = Event::Type::Readout;
            ctx.event.readout = { eventIndex, moduleDataList, moduleCount };
            ctx.event.system = {}

            ctx.ready = true;
            ctx.processed = false;
        }

        cv.notify_one();
    }

    void system_event_callback(const u32 *header, u32 size)
    {
        {
            std::unique_lock<std::mutex> lock(ctx.mutex);
            ctx.cv.wait(lock, [&ctx] { return ctx.processed; });

            ctx.event.type = Event::Type::System;
            ctx.event.readout = {}
            ctx.event.system = { header, size };

            ctx.ready = true;
            ctx.processed = false;
        }

        cv.notify_one();
    }

* Add (API) version info to CrateConfig and the yaml format.

* abstraction for the trigger/io system. This needs to be flexible because the
  system is going to change with future firmware udpates.

* examples
  - Minimal CrateConfig creation and writing it to file
  - Complete manual readout including CrateConfig setup for an MDPP-16

* Multicrate support (later)
  - Additional information needed and where to store it.
  - multi-crate-mini-daq
  - multi-crate-mini-daq-replay

* mini-daq and mini-daq-replay: load plugins like in mvme/listfile_reader This
  would make the two tools way more useful (and more complex). Specify plugins
  to load (and their args) on the command line.
