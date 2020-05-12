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
* Listfile format, Writer and Reader code
* Readout loop, Readout Worker DONE
* Stack Building DONE
* Stack Management
* Readout/Response Parser using readout stack to parse incoming data DONE
* listfile format, writer and reader, tools to get the readout config back to
  construct a readout parser for the file.
  mvme will thus be able to replay files record by the library.
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

* listfile reader MOSTLY DONE
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

FIXME: open issues
- how to detect and handle single vs multicrate setups?

## TODO
* Should be fixed now:  disabling triggers when stopping a DAQ does not work reliably right now.

## Misc

archive                        type     level writeRate[MB/s] readRate[MB/s]  bytesWritten         bytesRead            zipCompressed        zipUncompressed      ratio     
compression-test-zip_0.zip     zip      0     611.00          1327.43         314572800            314572800            314572800            314572800            1.00
compression-test-zip_1.zip     zip      1     51.26           204.50          314572800            314572800            215027670            314572800            0.68
compression-test-zip_2.zip     zip      2     42.63           208.48          314572800            314572800            214638505            314572800            0.68
compression-test-lz4_0.zip     lz4      0     332.96          1136.36         314572800            314572800            299653656            299653656            0.95
compression-test-lz4_1.zip     lz4      1     327.51          1140.68         314572800            314572800            299653656            299653656            0.95
compression-test-lz4_2.zip     lz4      2     333.70          1136.36         314572800            314572800            299653656            299653656            0.95
compression-test-lz4_3.zip     lz4      3     50.19           983.61          314572800            314572800            276179544            276179544            0.88
compression-test-lz4_4.zip     lz4      4     49.49           983.61          314572800            314572800            275666349            275666349            0.88
compression-test-lz4_5.zip     lz4      5     49.79           993.38          314572800            314572800            275554515            275554515            0.88
compression-test-lz4_6.zip     lz4      6     48.43           996.68          314572800            314572800            275359030            275359030            0.88
compression-test-lz4_7.zip     lz4      7     45.83           1000.00         314572800            314572800            275038072            275038072            0.87
compression-test-lz4_8.zip     lz4      8     44.03           1000.00         314572800            314572800            274623461            274623461            0.87
compression-test-lz4_9.zip     lz4      9     41.03           993.38          314572800            314572800            274370853            274370853            0.87
compression-test-lz4_-1.zip    lz4      -1    408.72          1140.68         314572800            314572800            299264770            299264770            0.95
compression-test-lz4_-2.zip    lz4      -2    590.55          1140.68         314572800            314572800            299764860            299764860            0.95
compression-test-lz4_-3.zip    lz4      -3    597.61          1115.24         314572800            314572800            297414453            297414453            0.95

MTDC-32 with new (fast blt) firmware and jumper set:
  writeabs a32 d32 0xfaaeaaaa 0x1337abcd
  setbase 0x01000000
  read a32 d16 0x6000 # addr low      
  read a32 d16 0x6002 # addr high
  read a32 d16 0x6004 # value low
  read a32 d16 0x6006 # value high
  read a32 d16 0x6008 # amod

  13:56:01:   setbase 0x01000000
  13:56:01:   read a32 d16 0x01006000 -> 0x0000aaa8 (43688 dec)
  13:56:01:   read a32 d16 0x01006002 -> 0x0000faae (64174 dec)
  13:56:01:   read a32 d16 0x01006004 -> 0x0000abcd (43981 dec)
  13:56:01:   read a32 d16 0x01006006 -> 0x00001337 (4919 dec)
  13:56:01:   read a32 d16 0x01006008 -> 0x0000000d (13 dec)
