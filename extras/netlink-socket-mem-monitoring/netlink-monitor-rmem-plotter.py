#!/usr/bin/env python3

# This script can parse the output text file produced by the
# netlink-monitor-rmem dev tool. It creates a plot containing graphs of the
# receive buffer memory usage and the value of the last delay request that was
# sent to the MVLC.

import collections
import os.path
import re
import sys

import matplotlib.pyplot as plt

if __name__ == "__main__":
    filename = sys.argv[1]
    basename = os.path.splitext(filename)[0]

    expr = r'rmem_alloc=(\d+)\s+rcvbuf=(\d+)\s+delay=(\d+)'

    rmem_alloc_values = list()
    rcvbuf_values = list()
    delay_values = list()

    with open(filename, mode='r') as f:
        for line in f:
            m = re.search(expr, line)
            rmem_alloc = int(m.group(1))
            rcvbuf = int(m.group(2))
            delay_us = int(m.group(3))

            rmem_alloc_values.append(rmem_alloc / (1024.0**2))
            rcvbuf_values.append(rcvbuf / (1024.0**2))
            delay_values.append(delay_us)

    fig = plt.figure()
    ax1 = fig.add_subplot(111)

    ax1.plot(rmem_alloc_values)
    ax1.plot(rcvbuf_values)
    ax1.set_ylabel('memory [MB]')
    ax1.set_xlabel('sample #')

    ax2 = ax1.twinx()
    ax2.plot(delay_values, 'r.')
    ax2.set_ylabel('delay [µs]')

    plt.show()
