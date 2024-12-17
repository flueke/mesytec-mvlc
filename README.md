# mesytec-mvlc - driver and utilities for the mesytec MVLC VME Controller

*mesytec-mvlc* is a driver and utility library for the [Mesytec MVLC VME
controller](https://mesytec.com/products/nuclear-physics/MVLC.html) written in
C++. The library consists of low-level code for accessing MVLCs over USB or
Ethernet and higher-level communication logic for reading and writing internal
MVLC registers and accessing VME modules. Additionally the basic blocks needed
to build high-performance MVLC based DAQ readout systems are provided:

* Configuration holding the setup and readout information for a single VME
  crate containing multiple VME modules.

* Multithreaded readout worker and listfile writer using fast data compression
  (LZ4 or ZIP deflate).

* Readout parser which is able to handle potential ethernet packet loss.

* Live access to readout data on a best effort basis. Sniffing at the data
  stream does not block or slow down the readout.

* Examples showing how to combine the above parts into a working readout system
  and how to replay data from a previously recorded listfile.

* Various counters for monitoring the system.

mesytec-mvlc is used in the [mvme](https://mesytec.com/downloads/mvme.html) DAQ
software.

## Documentation

* [MVLC command format](doc/command_format.md)
* [MVLC data format](doc/data_format.md)
* [mvme manual](https://mesytec.com/downloads/mvme/mvme.pdf)

  The mvme manual contains a section about the MVLC Trigger/IO system. mvme VME
  script supports all implemented MVLC commands.

## Building

```sh
apt-get update && apt-get install -y --no-install-recommends \
ca-certificates build-essential git cmake ninja-build zlib1g-dev \
libzmq3-dev cppzmq-dev

mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=~/local/mesytec-mvlc .. \
cmake --build . --target all
ctest .
```

Dockerfiles can be found [here](tools/dockerfiles).

## Tools

* `mvlc-cli`: command line interface to the MVLC. Useful subcommands:
  - scanbus: Scans the upper 16 bits of the VME address space for (mesytec) VME modules.
  - register_read/register_write: Internal register access.
  - vme_read/vme_write: Single cycle VME access.
  - stack_info: Print readout command stack and trigger information.
  - show_usb_devices: Info about MVLC USB devices found on the system.

  `mvlc-cli -h -a` shows help for all implemented commands.

* `mvlc-mini-daq`: Standalone minimal DAQ tool. Configuration via .yaml/json
  files exported from mvme. Writes readout data to listfile and/or dumps it to
  console.

Pass `-DMVLC_BUILD_DEV_TOOLS=ON -DMVLC_BUILD_TOOLS=ON` to cmake to build additional tools.

* `mvlc-listfile-info <zipfile>`: Processes the input listfile, showing
  information about the contained configuration and decoded readout/system
  events. Quick as it does not process the actual readout data.

* `decode-mvlc-frame-header`: reads raw frame headers from stdin, attempts to
  decode and print information:
  ```
   $ ./decode-mvlc-frame-header 0xf380001f
   0xf380001f -> StackResultFrame (len=31, stackNum=0, ctrlId=0, frameFlags=continue)
   ```

* `decode-mvlc-eth-headers <header0> <header1>`: decodes the ETH frame header words:
  ```
  ./decode-mvlc-eth-headers 0x00070004 0x00010000
  header0 = 0x00070004, header1 = 0x00010000
  header0: packetChannel=0, packetNumber=7, controllerId=0, dataWordCount=4
  header1: udpTimestamp=16, nextHeaderPointer=0x0000, isHeaderPointerPresent=1
  ```
