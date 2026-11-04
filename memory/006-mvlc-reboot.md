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
- Default area 0. Earlier assumption that the dipswitch bits are wrong over VME was disproven, see
  the dipswitch check below. Min firmware FW0036.
- Reboot detection: the device must go offline and come back within `--timeout` (default 10 s,
  MVLC boots in ~2 s, DHCP may take longer). ETH uses a raw UDP register read probe (200 ms
  timeout) because `MVLC::connect()` against an offline MVLC blocks for up to 3 x 2 s. USB checks
  the D3XX device list for the serial; the FTDI index may change, the serial does not.
- To verify on hardware: whether the FT601 really re-enumerates on an FPGA reboot (the USB offline
  detection depends on it), real boot time, quiet disconnect after the device vanished, Windows.
- Module reboot (2026-11-04): `mvlc-cli reboot <vme-address>`, any address != 0xffff0000. No MVLC
  FW requirement. Uses normal `vmeWrite`s (MVLC stays up, so the stack response arrives), a
  NoVMEResponse on the last BFP byte is accepted. Waits via `scanbus::read_module_info()` polls
  (NoVMEResponse/BERR = offline). Tested 2026-11-04 on mvlc-0066 (USB + ETH) with an MDPP-32 SCP
  at 0x0e000000: all 4 BFP bytes are acknowledged, polls get NoVMEResponse, back after ~1.3 s.
- Hardware tests 2026-11-04 on mvlc-0066 (FW0048): MVLC reboot works via USB (back after 2.1 s,
  one `FT_DEVICE_NOT_CONNECTED` read warning from the reader thread) and ETH (4.0 s). MDPP-16 SCP at
  0x00000000 booted area 0 = CSI firmware, so area 0 is not necessarily the running firmware.
  MADC-32 (no MVP) at 0xf0000000: 0x6200-0x6204 are normal registers, 0x6204 reads 0x010d without
  the InvalidRead bit, so `clear_output_fifo` spins 2.5 s and fails with 'timed out'.
- Active area (2026-11-04): neither RAI nor the status byte area bits (1-2) reflect the booted
  area, both read 0 on all devices even after booting the MDPP-16 into area 1. The status byte
  dipswitch bits (3-4) read 1 on the MDPP-16 (SCP in area 1, power-up default) and 0 on the
  MDPP-32 and MVLC. Raw 0x6204 on idle MVP devices reads 0x3xx (InvalidRead|FlashEmpty|last status).
- Non-MVP modules: `clear_output_fifo` gives up after `OutputFifoMaxClearReads` (1024) reads with
  `protocol_error`, the MADC fails in ~2 s. The CLI raises the `mvlc_usb` logger from warn to err
  during an MVLC reboot to hide the `FT_DEVICE_NOT_CONNECTED` read warning.
- Dipswitch check 2026-11-04: MDPP-16 set to 2 boots RCP, status byte dipswitch bits read 2. MVLC
  set to 1 (booted area 0 via onboard USB) reads 1. So the dipswitch bits are correct over VME on
  both. RAI and status area bits stay 0 regardless of dipswitch and booted area.
- RAI VME vs USB serial (2026-11-04, MVLC + MDPP-32 onboard USB, /dev/serial/by-id/usb-mesytec_*):
  responses are byte-identical. RAI returns the SAI-selected area index, the status byte area bits
  mirror it, and the selection is shared between both interfaces (SAI 1 via serial -> RAI 1 via
  VME). Neither reflects the booted area. Serial probe scripts need /usr/bin/python3 (the repo
  .venv has no pyserial).
