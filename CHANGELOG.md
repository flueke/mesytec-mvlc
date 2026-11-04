# Changelog

## v0.3-rc

### API
- UDP packet allowance (FW0048): register constants `udp_packet_status` and `udp_packet_limit`.
  `MVLC::connect()` writes 0 to `udp_packet_limit` by default, see
  `MVLC::setResetPacketAllowanceOnConnect()`.
- New `mvlc_mvp.h`: minimal VME <-> MVP flash interface access (NOP check, fire and forget boot
  instruction, RAI, status byte flags). Not part of `mesytec-mvlc.h`.

### mvlc-cli
- New switch `--mvlc-keep-packet-allowance`.
- New command `reboot`: reboots the MVLC (FW >= FW0036) through its MVP interface, waits for it
  to come back and reconnects. Optional positional `<vme-address>` reboots a mesytec VME module
  instead and polls it via VME until it responds again. Without `--area` the dipswitch area
  (power-up default) is booted.

## v0.2

### USB / FTDI D3XX
- New structure for the FTDI D3XX drivers. Merged the x64 and aarch64 USB implementations.
- Linux: support ftdi-d3xx 1.1.8.
- Linux: improved command pipe timeouts and performance.
- Linux: fix stale replies after a USB reset and recover locked up MVLCs.
- Windows: load ftd3xx 1.3 (FTDIBUS3) and 1.4 (WinUSB) at runtime.
- Windows: read the command pipe one max packet at a time, 100 ms command pipe read timeout,
  no abort on command read timeouts.
- cmd_pipe_reader: removed an infinite loop, count orphaned continuations, resync on bad
  continuation headers instead of stalling.

### ETH
- Fix the outgoing max packet size check, report
  `MVLCErrorCode::UdpMaxOutgoingPacketSizeExceeded`.

### API
- Renamed the constant `parse_error_counter` to `cmd_pipe_full`. Added docs explaining the
  former "parse error counter".

### mvlc-cli
- `show_usb_devices` prints the USB speed.
- No missing command warning on `--version` and `--help`.

### Logging
- spdlog logs to stderr by default.
- Fixed broken fmt logging statements.

### Build / Tests / Tools
- Skip the `doc/` build if a Python dependency is missing.
- Added stack upload and ETH max payload size tests.
- USB tests: select an MVLC by serial via `MVLC_TEST_USB_SERIAL`.
- Added two USB dev/test tools.
- Added instructions for AI agents.
