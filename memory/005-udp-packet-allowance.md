# UDP packet allowance (FW004X)

Spec: `doc/dev/MVLC UDP Packet Allowance.pdf` (Robert, firmware). Registers 0x4500 (C, STOP, HOLD)
and 0x4504 (L, EN, HOLD). Requested by a user (mail 2026-07-23) for
credit-based flow control so a readout stopped in gdb cannot overflow its UDP receive buffer.

- The user has their own readout code, not mesytec-mvlc. Library integration is not needed.
- Firmware pauses UDP data packet output, not stack starts as the user proposed. Bound is exact:
  `SO_RCVBUF >= window * max packet size`, no max event size term.
- L is compared for equality, not wrapping subtraction. L behind C -> ~65k packets before stop.
  Host must set L once via HOLD, then only increase it, computed from unwrapped header packet
  numbers (lost packets included), never from a count of received packets.
- Per Florian: C counts only PacketChannel::Data. Direct exec stacks have a separate output buffer
  that the driver always drains, so immediate VME access keeps working while data is held. Stack
  execution incl. block transfers is atomic (to his knowledge), no mid-BLT stall.
- Spec tests 1/2 disagree on C: C is the next packet number (test 2 is right).
- The MVLC is single-client by design. ETH accepts commands from several clients, but responses can
  be routed to the wrong one. The user's side access from separate processes is unsupported; never
  plan tests that rely on a second client.
- `MVLC::setResetPacketAllowanceOnConnect()` (default on, FW >= 0x0048) writes 0x4504 = 0 in
  `MVLC::connect()`. mvlc-cli `--mvlc-keep-packet-allowance` skips it for multi-step manual setup.
