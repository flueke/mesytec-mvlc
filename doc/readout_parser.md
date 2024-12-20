# MVLC readout_parser module

## Problem

Parse an incoming - possibly lossful - stream of buffers containing MVLC readout
data into a stream of events containing module readout data.

```
input data: USB data stream or ETH packet stream

                        +-----------------+    +-----------------+
   +--------+           | output event N  |    | output event N+1|
   | 0xF3.. |           +-----------------+    +-----------------+
   | ...... |           | module0         |    | module0         |
   | 0xF5.. |  ->       | payload         |    | payload         | ...
   | ...... |           +-----------------+    +-----------------+
   | ...... |           | module1         |    | module1         |
   ..........           | payload         |    | payload         |
   +--------+           +-----------------+    +-----------------+
```
