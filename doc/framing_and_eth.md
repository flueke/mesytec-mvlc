# MVLC data format

## Frame types

| Name              | Value | Binary Value |
| ----------------- | ----- | ------------ |
| StackFrame        | 0xF3  | 0b1111'0011  |
| StackContinuation | 0xF9  | 0b1111'1001  |
| BlockRead         | 0xF5  | 0b1111'0101  |
| StackError        | 0xF7  | 0b1111'0111  |
| SystemEvent       | 0xFA  | 0b1111'1010  |

## UDP format

Two headers, afterwards payload containing the above framing format starts. Size is always a multiple of 32-bits.
Max payload size of packets sent to the MVLC is

**Header0**: `{ 0b00, chan[1:0], packet_number[11:0], ctrl_id[2:0], data_word_count[12:0] }`

**Header1**: `{ udp_timestamp[18:0], next_header_pointer[12:0] }`


`udp_timestamp` is not used so far.

The fact that **Header0** starts with `0b00` which does not collide with any of
the other frames means that it is possible to store mixed streams (USB and ETH)
in a single listmode file. If the MVLC ctrl_id (aka crate_id) is correctly set,
streams from multiple crates can be stored in the same file too. So a mixed
multicrate listfile is possible without running into parsing ambiguities.
