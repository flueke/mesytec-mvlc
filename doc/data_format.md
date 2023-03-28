# MVLC data stream format and parsing

This document attempts to describe the MVLC generated data stream and to provide
useful hints on how to parse the stream. The request/response based command side
is not documented here.


32-bit oriented data format. UDP packets are multiples of 32-bit long. The term
*word* referes to 4 bytes unless otherwise noted.

# ETH packet format and packet loss handling

MVLC generated UDP packets start with two header words followed by the actual
payload data. The header words contain a packet number and a pointer to the next
MVLC frame header present in the packet data. With this information packet loss
can be detected and parsing can be deterministically resumed after packet loss
occured.

## ETH header words
```
Header0: {0, 0, chan[1:0], packet_number[11:0], ctrl_id[2:0], num_data_words[12:0]}
Header1: {udp_timestamp[18:0], next_header_pointer[11:0]}
```

* `chan` is the 2 bit logical packet channel. Each channel has its own packet counter.

  chan field | channel
  -----------|-------------------------------
  0          | command mirror responses
  1          | immediate stack exec data
  2          | DAQ mode stack exec/event data

* `num_data_words` contains the number of data words following the two header words.

* `next_header_pointer` is the zero-based word offset to the next MVLC outer
framing header (`0xF3`, `0xF9`) following the two ETH header words. The max
value `0xfff` is used to indicate that no header is present in the packet data.



# Output producing commands

| Command        | # output words | Notes                                                      |
|----------------|----------------|------------------------------------------------------------|
| VMERead        | 1              | single read VME amods (0x09, 0x0D, ...) stack accu not set |
| WriteMarker    | 1              | produces the static marker value                           |
| WriteSpecial   | 1              | MVLC timestamp or current accu value                       |
| VMERead        | N              | block VME amods (0x0b, 0x08, ...) or stack accu set        |
| VMEMBLTSwapped | N              | only MBLT amods allowed (0x08, 0x0c)                       |

**Note:** Setting the stack accumulator (via `SetAccu` or `ReadToAccu`) turns
the next non-block read instruction into an emulated block read. The accumulator
value specifies the number of reads to perform. The result is output as a
sequence of `0xF5 BlockRead` frames - the same way standard block reads are
handled.


# Outer Framing

0xF3 & 0xF9 stack frames -> each stack execution ("event") produces a 0xF3 frame
optionally followed by additional 0xF9 frames.

Generated 0xFA frames can be inserted into the data stream -> system events.

0xF7 stack error frames (command pipe only).

# MVLC Inner BlockRead Framing
