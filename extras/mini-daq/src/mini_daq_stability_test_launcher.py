#!/usr/bin/env python3

import argparse
import os.path
import random
import subprocess
import sys

RunDurationMin = 10
RunDurationMax = 60
RunCount = 10

if __name__ == "__main__":
    crateConfigFile = sys.argv[1]

    for runNum in range(RunCount):
        runDuration = random.randint(RunDurationMin, RunDurationMax)
        daqArgs = [ "./mini-daq", crateConfigFile, str(runDuration), "--no-listfile" ]

        print("Run {} of {}: starting ".format(runNum, RunCount), *daqArgs)
        daqProc = subprocess.Popen(daqArgs)
        daqProc.communicate()
        if daqProc.returncode != 0:
            raise Exception("mini-daq returned {}".format(daqProc.returncode))
