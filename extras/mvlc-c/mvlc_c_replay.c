/**
 * \file
 * Minimal listfile replay tool implemented using the mvlc-c interface.
 *
 * Uses the mvlc_replay_t abstraction with custom callbacks.
 * \ingroup mvlc-c
 * \{
 */
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mesytec-mvlc-c.h>

// Called for each readout event recorded by the DAQ.
void process_readout_event_data(
        void *userContext, int eventIndex, const readout_moduledata_t *moduleDataList, unsigned moduleCount)
{
    bool printData = (bool) userContext;

    if (printData)
    {
        printf("process_readout_event_data, userContext=%p, eventIndex=%d, moduleCount=%u\n",
                userContext, eventIndex, moduleCount);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            readout_moduledata_t md = moduleDataList[mi];

            if (md.data.size)
            {
                readout_datablock_t block = md.data;
                printf("  data block of size %u: ", block.size);
                for (u32 i=0; i<block.size; ++i)
                    printf("0x%08x ", block.data[i]);
                printf("\n");
            }
        }
    }
}

// Called for each software generated system event.
void process_readout_system_event(
        void *userContext, const u32 *header, u32 size)
{
    bool printData = (bool) userContext;

    if (printData)
    {
        printf("process_readout_system_event, userContext=%p, header=0x%08x, size=%u\n",
               userContext, *header, size);
    }
}

void print_help()
{
    printf("Options:\n");
    printf("  --listfile <filename>     # Specify an input filename, e.g. run01.zip\n");
    printf("  --print-config            # Print the MVLC CrateConfig extracted from the listfile and exit\n");
    printf("  --print-readout-data      # Print readout data\n");
}

int main(int argc, char *argv[])
{
    char *opt_listfilePath = NULL;
    bool opt_printConfig = false;
    bool opt_printReadoutData = false;

    #define errbufsize 1024
    char errbuf[errbufsize];

    while(1)
    {
        static struct option long_options[] =
        {
            {"listfile", required_argument, 0, 0 },
            {"print-config", no_argument, 0, 0 },
            {"print-readout-data", no_argument, 0, 0 },
            { 0, 0, 0, 0 }
        };

        int option_index = 0;

        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1)
            break;

        const char *optname = long_options[option_index].name;

        switch (c)
        {
            case 0:
                if (strcmp(optname, "listfile") == 0)
                    opt_listfilePath = optarg;
                else if (strcmp(optname, "print-config") == 0)
                    opt_printConfig = true;
                else if (strcmp(optname, "print-readout-data") == 0)
                    opt_printReadoutData = true;
                else if (strcmp(optname, "help") == 0)
                {
                    print_help();
                    return 0;
                }

                break;

            case '?':
                print_help();
                return 1;
        }
    }

    readout_parser_callbacks_t parserCallbacks =
    {
        process_readout_event_data,
        process_readout_system_event
    };

    void *userContext = (void *) opt_printReadoutData;

    mvlc_replay_t *replay = mvlc_replay_create(opt_listfilePath, parserCallbacks, userContext);

    if (opt_printConfig)
    {
        mvlc_crateconfig_t *cfg = mvlc_replay_get_crateconfig(replay);
        assert(cfg);
        printf("%s\n", mvlc_crateconfig_to_string(cfg));
        mvlc_crateconfig_destroy(cfg);
        return 0;
    }

    MVLC_ReadoutState rdoState = get_replay_state(replay);
    assert(rdoState == ReadoutState_Idle); (void) rdoState;

    mvlc_err_t err = mvlc_replay_start(replay);

    if (!mvlc_is_error(err))
        printf("Replay started\n");
    else
    {
        printf("Error starting replay: %s\n", mvlc_format_error(err, errbuf, errbufsize));
        return 1;
    }

    while (get_replay_state(replay) != ReadoutState_Idle)
        usleep(100 * 1000);

    printf("Replay done\n");

    mvlc_replay_destroy(replay);

    return 0;
}

/**\}*/
