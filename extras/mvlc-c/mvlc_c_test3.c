#include <stdio.h>
#include <stdlib.h>

#include <mesytec-mvlc-c.h>

/**
 * \file
 * MVLC listfile replay tool using the blocking API instead of callbacks.
 *
 * \ingroup mvlc-c
 * \{
 */

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <listfile>\n", argv[0]);
        return 1;
    }

    char *listfile = argv[1];
    size_t nSystems = 0;
    size_t nReadouts = 0;
    size_t eventHits[MVLC_ReadoutStackCount] = {0};

    mvlc_blocking_replay_t *replay = mvlc_blocking_replay_create(listfile);

    mvlc_err_t err = mvlc_blocking_replay_start(replay);

    if (mvlc_is_error(err))
    {
        char *msg = mvlc_format_error_alloc(err);
        printf("Error starting replay: %s\n", msg);
        free(msg);
        mvlc_blocking_replay_destroy(replay);
        return 1;
    }

    while (true)
    {
        event_container_t event = next_replay_event(replay);

        if (!is_valid_event(&event))
            break;

        if (event.type == MVLC_EventType_System)
        {
            nSystems++;
        }
        else if (event.type == MVLC_EventType_Readout)
        {
            nReadouts++;
            eventHits[event.readout.eventIndex]++;
        }
    }

    printf("system events=%lu, readout_events=%lu\n",
            nSystems, nReadouts);

    printf("readout event counts:\n");
    for (size_t i=0; i<MVLC_ReadoutStackCount; ++i)
    {
        size_t hits = eventHits[i];

        if (hits)
        {
            printf("  event%lu: %lu\n", i, hits);
        }
    }

    mvlc_blocking_replay_destroy(replay);

    return 0;
}

/**\}*/
