# mesytec-mvlc

*User space driver library for the Mesytec MVLC VME controller.*

## Components

* USB and ETH implementations DONE
  - Two pipes
  - Buffered reads
  - Unbuffered/low level reads
  - Max write size checks (eth packet size limit)?
  - support: error codes and conditions
  - Counters

* Dialog layer DONE
  - (UDP) retries DONE
* Error polling DONE
* Listfile format, Writer and Reader code DONE
* Readout loop, Readout Worker DONE
* Stack Building DONE
* Stack Management DONE
* Readout/Response Parser using readout stack to parse incoming data DONE
* listfile format, writer and reader, tools to get the readout config back to
  construct a readout parser for the file. DONE
  mvme will thus be able to replay files recorded by the library.
  If mvme would also store the library generated config the other way would
  also work. Do this!

* single create readout config: DONE
  - list of stack triggers          <- required to setup the mvlc
  - list of readout stacks          <- required for parsing the data stream
  - additional: VME init sequence   <- for reference only to know what's going on

* single create readout instance: DONE
  - readout config
  - readout buffer structure with crateId, number, type, capacity, used and view/read/write access
  - readout buffer queue plus operations (blocking, non-blocking)
  - listfile output (the readout config is serialized into the listfile at the start)

* listfile writer DONE
  - should be able to take buffers from multiple readout workers for multicrate setup
  - takes copies of readout buffers and internally queues them up for writing
  - listfile output
  - write serialized verison of the readout setup (single or multicrate variants)

* listfile reader  DONE
  - open archive file
  - open listfile in archive
  - open and read other files from the archive, list files
  - read sequences of specific section types to get MVLC and MVME
    configs (and possibly custom sections) out
  - deserialize config. this can then be used to construct a readout parser

* stack batch execution DONE
  - Used to execute large command lists by splitting them into max sized stack
    chunks and running those. Max size means that the command stack does not
    overflow.
  - Important: delays are needed in init sequences (wait after module reset).
    The MVLC can not handle these natively.

## TODO
* how to detect and handle single vs multicrate setups? what additional
  information is needed for multicrate setups?
* abstraction for the trigger/io system. This needs to be flexible because the
  system is going to change with future firmware udpates.
* mini-daq
  - add real help text (mention `mvme_to_mvlc` tool)
  - make getting the CrateConfig from the listfile preamble easier. basically
    move the code from mini_daq_replay.cc into the lib
