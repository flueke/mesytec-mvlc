#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    auto replay = make_mvlc_replay_blocking(argv[1]);

    if (auto ec = replay.start())
        return 1;

    size_t nSystems = 0;
    size_t nReadouts = 0;
    std::unordered_map<u8, size_t> sysEventTypes;
    std::unordered_map<int, size_t> eventHits;

    while (auto event = next_event(replay))
    {
        if (event.type == EventContainer::Type::System)
        {
            nSystems++;
            u8 t = system_event::extract_subtype(*event.system.header);
            sysEventTypes[t] += 1u;
        }
        else if (event.type == EventContainer::Type::Readout)
        {
            nReadouts++;
            eventHits[event.readout.eventIndex] += 1;
        }
    }

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
