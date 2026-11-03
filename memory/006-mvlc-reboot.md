# mvlc-cli reboot (MVP boot instruction)

Implemented 2026-11-03 in `src/mesytec-mvlc/mvlc_mvp.{h,cc}` and `mvlc-cli reboot`. Not yet tested
on hardware.

- Same mechanism as libmvp `mvlc-mvp-updater boot-module --vme-address 0xffff0000`: enable the flash
  interface (0x6200 = 1), write the BFP instruction `70 CD AB <area>` byte-wise to the input fifo
  0x6202 (A32/D16).
- Sequence: enable flash interface, clear output fifo, NOP round trip (validates the path), then the
  BFP bytes fire and forget: one super buffer uploads an immediate stack with the 4 writes and
  triggers it, written raw via `getImpl()->write()` under the cmd lock. A normal `vmeWrite` would
  run into `stackTransaction` retries (3 x 2 s) because the MVLC reboots before answering.
- Namespace `mesytec::mvlc::mvp`, header not in `mesytec-mvlc.h`: libmvp has `mesytec::mvp` with the
  same function names and uses `using namespace mesytec::mvlc`.
- Default area 0. The dipswitch bits in the MVP status byte are wrong when read over VME (per
  Florian), needs investigation. Min firmware FW0036.
- Reboot detection: the device must go offline and come back within `--timeout` (default 10 s,
  MVLC boots in ~2 s, DHCP may take longer). ETH uses a raw UDP register read probe (200 ms
  timeout) because `MVLC::connect()` against an offline MVLC blocks for up to 3 x 2 s. USB checks
  the D3XX device list for the serial; the FTDI index may change, the serial does not.
- To verify on hardware: whether the FT601 really re-enumerates on an FPGA reboot (the USB offline
  detection depends on it), real boot time, quiet disconnect after the device vanished, Windows.
