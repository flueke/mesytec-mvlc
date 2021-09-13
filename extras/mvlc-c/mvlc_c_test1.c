#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mesytec-mvlc-c.h>

void run_some_mvlc_functions(mvlc_ctrl_t *mvlc)
{
    printf("Running a few of the MVLC functions..\n");

    printf("\tisConnected: %d\n", mvlc_ctrl_is_connected(mvlc));
    printf("\thardwareId: 0x%04x\n", get_mvlc_ctrl_hardware_id(mvlc));
    printf("\tfirmwareRev: 0x%04x\n", get_mvlc_ctrl_firmware_revision(mvlc));
    char *conInfo = get_mvlc_ctrl_connection_info(mvlc);
    printf("\tconnection info: %s\n", conInfo);
    free(conInfo);

    u32 readVal = 0;
    mvlc_err_t err = mvlc_ctrl_read_register(mvlc, 0x600e, &readVal);
    if (!mvlc_is_error(err))
        printf("\tregister 0x600e: 0x%04x\n", readVal);
    else
    {
        static const size_t bufsize = 1024;
        char buf[bufsize];
        printf("Error reading MVLC register: %s\n", mvlc_format_error(err, buf, bufsize));
        return;
    }

    static const u32 modBase = 0x03000000;
    static const u32 hwReg = 0x6008;
    static const u32 fwReg = 0x600e;
    static const u8 amod = 0x09;

    err = mvlc_ctrl_vme_read(mvlc, modBase + hwReg, &readVal, amod, VMEDataWidth_D16);

    if (!mvlc_is_error(err))
        printf("\tVME module hwReg: 0x%04x\n", readVal);
    else
    {
        static const size_t bufsize = 1024;
        char buf[bufsize];
        printf("Error reading VME module register: %s\n", mvlc_format_error(err, buf, bufsize));
        return;
    }

    err = mvlc_ctrl_vme_read(mvlc, modBase + fwReg, &readVal, amod, VMEDataWidth_D16);

    if (!mvlc_is_error(err))
        printf("\tVME module fwReg: 0x%04x\n", readVal);
    else
    {
        static const size_t bufsize = 1024;
        char buf[bufsize];
        printf("Error reading VME module register: %s\n", mvlc_format_error(err, buf, bufsize));
        return;
    }
}

int main(int argc, char *argv[])
{
    char *mvlc_eth_host = NULL;

    while (1)
    {
        static struct option long_options[] =
        {
            {"eth_host", required_argument, 0, 0 },
            { 0, 0, 0, 0 }
        };

        int option_index = 0;

        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
            case 0:
                if (strcmp(long_options[option_index].name, "eth_host") == 0)
                {
                    mvlc_eth_host = optarg;
                }
                break;

            case '?':
                return 1;
        }
    }

    // USB
    printf("Creating MVLC_USB instance..\n");
    mvlc_ctrl_t *mvlc = mvlc_ctrl_create_usb();

    printf("Connecting to MVLC_USB..\n");
    mvlc_err_t err = mvlc_ctrl_connect(mvlc);

    if (!mvlc_is_error(err))
    {
        printf("Connected to MVLC_USB!\n");
    }
    else
    {
        static const size_t bufsize = 1024;
        char buf[bufsize];
        printf("Error connecting to MVLC_USB: %s\n", mvlc_format_error(err, buf, bufsize));
    }

    run_some_mvlc_functions(mvlc);

    printf("Disconnecting from MVLC_USB..\n");
    err = mvlc_ctrl_disconnect(mvlc);

    if (!mvlc_is_error(err))
    {
        printf("Disconnected from MVLC_USB.\n");
    }
    else
    {
        static const size_t bufsize = 1024;
        char buf[bufsize];
        printf("Error disconnecting from MVLC_USB: %s\n", mvlc_format_error(err, buf, bufsize));
    }

    mvlc_ctrl_destroy(mvlc);

    // ETH
    if (mvlc_eth_host)
    {
        printf("Creating MVLC_ETH instance..\n");
        mvlc_ctrl_t *mvlc = mvlc_ctrl_create_eth(mvlc_eth_host);

        printf("Connecting to MVLC_ETH..\n");
        mvlc_err_t err = mvlc_ctrl_connect(mvlc);

        if (!mvlc_is_error(err))
        {
            printf("Connected to MVLC_ETH!\n");
        }
        else
        {
            static const size_t bufsize = 1024;
            char buf[bufsize];
            printf("Error connecting to MVLC_ETH: %s\n", mvlc_format_error(err, buf, bufsize));
        }

        run_some_mvlc_functions(mvlc);

        printf("Disconnecting from MVLC_ETH..\n");
        err = mvlc_ctrl_disconnect(mvlc);

        if (!mvlc_is_error(err))
        {
            printf("Disconnected from MVLC_ETH.\n");
        }
        else
        {
            static const size_t bufsize = 1024;
            char buf[bufsize];
            printf("Error disconnecting from MVLC_ETH: %s\n", mvlc_format_error(err, buf, bufsize));
        }

        mvlc_ctrl_destroy(mvlc);
    }

    return 0;
}
