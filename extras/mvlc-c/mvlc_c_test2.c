/**
 * \file
 * Minimal DAQ program implemented using the mvlc-c interface.
 *
 * Uses the mvlc_readout_t abstraction with custom callbacks.
 * \ingroup mvlc-c
 * \{
 */
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
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
    printf("  --crateconfig <filename>  # The CrateConfig YAML file to use\n");
    printf("  --mvlc-eth <hostname>     # Override the mvlc from the crateconfig\n");
    printf("  --mvlc-usb                # Override the mvlc from the crateconfig\n");
    printf("  --listfile <filename>     # Specify an output filename, e.g. run01.zip\n");
    printf("  --no-listfile             # Do not write a an output listfile\n");
    printf("  --overwrite-listfile      # Overwrite existing listfiles\n");
    printf("  --print-readout-data      # Print readout data\n");
    printf("  --duration <seconds>      # DAQ run duration in seconds\n");
}

int main(int argc, char *argv[])
{
    char *opt_mvlcEthHost = NULL;
    bool opt_useUSB = false;
    char *opt_crateConfigPath = NULL;
    char *opt_listfilePath = NULL;
    bool opt_noListfile = false;
    bool opt_overwriteListfile = false;
    bool opt_printReadoutData = false;
    int opt_runDuration = 5; // run duration in seconds

    // cli options
    while (1)
    {
        static struct option long_options[] =
        {
            {"mvlc-eth", required_argument, 0, 0 },
            {"mvlc-usb", no_argument, 0, 0 },
            {"listfile", required_argument, 0, 0 },
            {"no-listfile", no_argument, 0, 0 },
            {"overwrite-listfile", no_argument, 0, 0 },
            {"print-readout-data", no_argument, 0, 0 },
            {"crateconfig", required_argument, 0, 0 },
            {"duration", required_argument, 0, 0 },
            {"help", no_argument, 0, 0 },
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
                if (strcmp(optname, "crateconfig") == 0)
                    opt_crateConfigPath = optarg;
                else if (strcmp(optname, "mvlc-eth") == 0)
                    opt_mvlcEthHost = optarg;
                else if (strcmp(optname, "mvlc-usb") == 0)
                    opt_useUSB = true;
                else if (strcmp(optname, "listfile") == 0)
                    opt_listfilePath = optarg;
                else if (strcmp(optname, "no-listfile") == 0)
                    opt_noListfile = true;
                else if (strcmp(optname, "overwrite-listfile") == 0)
                    opt_overwriteListfile = true;
                else if (strcmp(optname, "print-readout-data") == 0)
                    opt_printReadoutData = true;
                else if (strcmp(optname, "duration") == 0)
                    opt_runDuration = atoi(optarg);
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

    if (!opt_crateConfigPath)
    {
        printf("Error: missing --crateconfig\n");
        return 1;
    }

    // crateconfig
    mvlc_crateconfig_t *crateconfig = mvlc_read_crateconfig_from_file(opt_crateConfigPath);

    // mvlc creation
    mvlc_ctrl_t *mvlc = NULL;

    if (opt_mvlcEthHost)
    {
        printf("Creating MVLC_ETH instance..\n");
        mvlc = mvlc_ctrl_create_eth(opt_mvlcEthHost);

    }
    else if (opt_useUSB)
    {
        printf("Creating MVLC_USB instance..\n");
        mvlc = mvlc_ctrl_create_usb();
    }
    else
    {
        printf("Creating MVLC from crateconfig..\n");
        mvlc = mvlc_ctrl_create_from_crateconfig(crateconfig);
    }

    #define errbufsize 1024
    char errbuf[errbufsize];

    printf("Connecting to mvlc..\n");
    mvlc_err_t err = mvlc_ctrl_connect(mvlc);

    if (!mvlc_is_error(err))
    {
        printf("Connected to mvlc!\n");
    }
    else
    {
        printf("Error connecting to mvlc: %s\n", mvlc_format_error(err, errbuf, errbufsize));
        return 1;
    }

    // listfile setup
    mvlc_listfile_params_t listfileParams = make_default_listfile_params();

    if (opt_listfilePath)
        listfileParams.filepath = opt_listfilePath;

    if (opt_noListfile)
        listfileParams.writeListfile = false;

    if (opt_overwriteListfile)
        listfileParams.overwrite = true;

    // Note: NULL callbacks would also be ok. In this case the readout data is only
    // passed written to the listfile.
    //readout_parser_callbacks_t parserCallbacks = {NULL, NULL};

    readout_parser_callbacks_t parserCallbacks =
    {
        process_readout_event_data,
        process_readout_system_event
    };

    void *userContext = (void *) opt_printReadoutData;

    // readout
    mvlc_readout_t *rdo = mvlc_readout_create4(
            mvlc,
            crateconfig,
            listfileParams,
            parserCallbacks,
            userContext);

    MVLC_ReadoutState rdoState = get_readout_state(rdo);
    assert(rdoState == ReadoutState_Idle);

    err = mvlc_readout_start(rdo, opt_runDuration);

    if (!mvlc_is_error(err))
        printf("Readout started\n");
    else
    {
        printf("Error starting readout: %s\n", mvlc_format_error(err, errbuf, errbufsize));
        return 1;
    }

    while (get_readout_state(rdo) != ReadoutState_Idle)
    {
        usleep(100 * 1000);
    }

    printf("Readout done\n");

    stack_error_collection_t stackErrors = mvlc_ctrl_get_stack_errors(mvlc);

    if (stackErrors.count)
    {
        printf("MVLC Stack Errors:\n");

        for (size_t i=0; i<stackErrors.count; ++i)
        {
            stack_error_t err = stackErrors.errors[i];
            char *flags = mvlc_format_frame_flags(err.frameFlags);
            printf("  stack=%u, line=%u, flags=%s, count=%u\n",
                    err.stackId, err.stackLine, flags, err.count);
            free(flags);
        }
    }

    mvlc_ctrl_stack_errors_destroy(stackErrors);


    mvlc_readout_destroy(rdo);
    mvlc_ctrl_destroy(mvlc);

    // ETH

    return 0;
}

/**\}*/
