#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mesytec-mvlc-c.h>

int main(int argc, char *argv[])
{
    char *mvlc_eth_host = NULL;
    char *crateconfig_path = NULL;

    // cli options
    while (1)
    {
        static struct option long_options[] =
        {
            {"eth_host", required_argument, 0, 0 },
            {"crateconfig", required_argument, 0, 0 },
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
                if (strcmp(optname, "eth_host") == 0)
                    mvlc_eth_host = optarg;
                else if(strcmp(optname, "crateconfig") == 0)
                    crateconfig_path = optarg;

                break;

            case '?':
                return 1;
        }
    }

    if (!crateconfig_path)
    {
        printf("Error: missing --crateconfig\n");
        return 1;
    }

    static const size_t errbufsize = 1024;
    char errbuf[errbufsize];

    // mvlc creation
    mvlc_ctrl_t *mvlc = NULL;

    if (mvlc_eth_host)
    {
        printf("Creating MVLC_ETH instance..\n");
        mvlc = mvlc_ctrl_create_eth(mvlc_eth_host);

    }
    else
    {
        printf("Creating MVLC_USB instance..\n");
        mvlc = mvlc_ctrl_create_usb();
    }

    printf("Connecting to mvlc..\n");
    mvlc_err_t err = mvlc_ctrl_connect(mvlc);

    if (!mvlc_is_error(err))
    {
        printf("Connected to mvlc!\n");
    }
    else
    {
        printf("Error connecting to mvlc: %s\n", mvlc_format_error(err, errbuf, errbufsize));
    }


    // crateconfig
    mvlc_crateconfig_t *crateconfig = mvlc_read_crateconfig_from_file(crateconfig_path);

    // FIXME: leftoff here. how to make the listfile easy and simple to use?
    // Right now would have to pass in a mvlc_listfile_write_handle() which means
    // it would be the same as in the old mini-daq: create the zip archive and the member file,
    // then implement the write handle using those.



    mvlc_ctrl_destroy(mvlc);

    // ETH

    return 0;
}

