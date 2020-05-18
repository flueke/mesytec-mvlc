#!/usr/bin/env python3

import argparse
import os.path
import subprocess

ListfileSetups = [
        { 'type': None, 'level': 0, },

        { 'type': "zip", 'level': 0, },
        { 'type': "zip", 'level': 1, },
        { 'type': "zip", 'level': 2, },

        { 'type': "lz4", 'level': 0, },
        { 'type': "lz4", 'level': 1, },
        { 'type': "lz4", 'level': 2, },
        { 'type': "lz4", 'level': -1, },
        { 'type': "lz4", 'level': -2, },
        ]

MVLCConnections = [
        { 'type': "usb" },
        { 'type': "eth", 'hostname': "192.168.42.42" },
        ]

RunDuration = 10

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("crateconfig")
    parser.add_argument("-O", "--output-directory", help="listfile output directory")
    cliargs = parser.parse_args()

    lfBase = os.path.basename(cliargs.crateconfig)

    for con in MVLCConnections:
        for setup in ListfileSetups:
            args = [ "./mini-daq", cliargs.crateconfig, str(RunDuration) ]

            if con["type"] == "usb":
                args.append("--mvlc-usb")
            elif con["type"] == "eth":
                args.extend(["--mvlc-eth", con["hostname"]])

            if (setup["type"] is None):
                args.append("--no-listfile")
            else:
                if cliargs.output_directory:
                    listfileOut = os.path.join(cliargs.output_directory, lfBase)
                else:
                    listfileOut = lfBase

                logfileOut = "{}_{}_{}_lvl{}.log".format(listfileOut, con["type"], setup["type"], setup["level"])
                listfileOut = "{}_{}_{}_lvl{}.zip".format(listfileOut, con["type"], setup["type"], setup["level"])

                args.extend(["--listfile", listfileOut])
                args.extend(["--listfile-compression-type", setup["type"]])
                args.extend(["--listfile-compression-level", str(setup["level"])])

                print("Executing", *args)
                print("logfileOut", logfileOut)

                with open(logfileOut, 'w') as logfile:
                    process = subprocess.Popen(args, stdout=logfile, stderr=logfile)
                    process.communicate()
