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

Hello empty stackFrame!
TRACE - mvlc_rdo_parser parse_readout_contents(): state.curBlockFrame.header=0xf3810172
begin buffer 'input around the non block frame header' (size=500)
  0x0400643D
  0x04106037
  0x04046407
  0x0414658D
  0x0408636E
  0x041865F9
  0x040C6317
  0x041C6550
  0x0401644A
  0x0411604A
  0x0405640A
  0x041565A3
  0x0409638B
  0x04196605
  0x040D6319
  0x041D654B
  0xC00989E7
  0x40002015
  0x0402642D
  0x04126021
  0x040663E6
  0x040A637B
  0x040E6319
  0x04206400
  0x040363FF
  0x04076378
  0x040B632D
  0x040F62C9
  0xF9810172
  0xF5800171
  0x0400643D
  0x04046406
  0x0408636E
  0x040C6317
  0x04016449
  0x0405640B
  0x0409638A
  0x040D6318
  0x041D654A
  0x00000000
  0xC00989E8
  0x40002023
  0x04016447
  0x04116044
  0x04056408
  0x04155D98
  0x04096388
  0x04195DFA
  0x040D6316
  0x041D654A
  0x04026431
  0x04126023
  0x040663EA
  0x04165DB9
  0x040A637E
  0x041A5E0F
  0x040E631B
  0x041E604E
  0x04206400
  0x0421642A
  0x040363FF
  0x04135FAD
  0x04076379
  0x041765DC
  0x040B632D
  0x041B6628
  0x040F62C7
  0x041F5FD2
  0x0400643E
  0x04106034
  0x04046406
  0x04146576
  0x0408636E
  0x041865E2
  0x040C6316
  0x041C6073
  0xC00989E9
  0x40002023
  0x0400643E
  0x04106033
  0x04046405
  0x04145DA2
  0x0408636D
  0x04185E0E
  0x040C6316
  0x041C6551
  0x04206400
  0x0421642A
  0x0401644B
  0x0411604C
  0x0405640A
  0x041565B8
  0x0409638C
  0x0419661A
  0x040D6319
  0x041D6554
  0x04026433
  0x04126026
  0x040663EC
  0x041665DA
  0x040A637F
  0x041A6630
  0x040E631A
  0x041E6055
  0x040363FF
  0x04135FAB
  0x04076378
  0x041765FE
  0x040B632D
  0x041B64BD
  0x040F62C6
  0x041F5FD4
  0xC00989EA
  0x40002021
  0x040363FE
  0x04135FAA
  0x04076376
  0x041759E3
  0x040B632C
  0x041B5E31
  0x040F62C7
  0x041F5FD0
  0x0400643F
  0x04106035
  0x04046407
  0x04145D80
  0x0408636E
  0x04185DEC
  0x040C6316
  0x041C6074
  0x04206400
  0x0421642A
  0x0401644B
  0x04116047
  0x0405640B
  0x04156595
  0x0409638A
  0x041965F7
  0x040D6318
  0x041D654A
  0x04026431
  0x04126024
  0x040663EB
  0x040A637F
  0x040E631B
  0x041E604E
  0xC00989EB
  0x40002023
  0x040363FD
  0x04135FA9
  0x04076377
  0x04175DE0
  0x040B632C
  0x041B64B3
  0x040F62C7
  0x041F5FD1
  0x04206400
  0x04216429
  0x04006440
  0x04106038
  0x04046406
  0x0414657B
  0x0408636D
  0x041865E7
  0x040C6317
  0x041C6633
  0x04016447
  0x04116048
  0x0405640C
  0x04156591
  0x0409638A
  0x041965F3
  0x040D6317
  0x041D6544
  0x04026430
  0x04126024
  0x040663EB
  0x041665B1
  0x040A637D
  0x041A6607
  0x040E631B
  0x041E6050
  0xC00989EC
  0x40002023
  0x0401644B
  0x04116048
  0x0405640B
  0x04155DC2
  0x0409638B
  0x04195E24
  0x040D6317
  0x041D655C
  0x04026433
  0x04126026
  0x040663EB
  0x04165DE2
  0x040A6380
  0x041A5E38
  0x040E6319
  0x041E6053
  0x04206400
  0x0421642A
  0x04036400
  0x04135FAA
  0x04076378
  0x04176606
  0x040B632D
  0x041B64B2
  0x040F62C7
  0x041F5FD6
  0x04006440
  0x04106039
  0x04046408
  0x041465A0
  0x04086370
  0x0418660C
  0x040C6319
  0x041C6557
  0xC00989ED
  0x40002019
  0x0401644A
  0x04116048
  0x0405640A
  0x04155DAB
  0x0409638B
  0x04195E0D
  0x040D6317
  0x041D6549
  0x04206400
  0x04216429
  0x04026433
  0x04126026
  0x040663EB
  0x040A637E
  0x040E631A
  0x040363FE
  0x04076377
  0x040B632D
  0x040F62C8
  0x0400643F
  0x04046408
  0x0408636F
  0x040C6318
  0x041C6550
  0xC00989EE
  0x40002023
  0x04016448
  0x04116044
  0x04056409
  0x04155D94
  0x04096389
  0x04195DF6
  0x040D6317
  0x041D654A
  0x04206400
  0x04216429
  0x04026430
  0x04126023
  0x040663EA
  0x041665B5
  0x040A637D
  0x041A660B
  0x040E6317
  0x041E604D
  0x040363FF
  0x04135FAB
  0x04076377
  0x041765D8
  0x040B632C
  0x041B6624
  0x040F62C8
  0x041F5FD0
  0x0400643E
  0x04106034
  0x04046406
  0x04146572
  0x0408636E
  0x041865DE
  0x040C6315
  0x041C6073
  0xC00989EF
  0x40002021
  0x040363FF
  0x04135FAA
  0x04076376
  0x04176602
  0x040B632D
  0x041B64B1
  0x040F62C8
  0x041F5FD5
  0x04206400
  0x0421642A
  0x0400643F
  0x04106037
  0x04046408
  0x0414659E
  0x04086370
  0x0418660A
  0x040C6317
  0x041C6550
  0x0401644B
  0x0411604D
  0x0405640C
  0x041565B4
  0x0409638D
  0x04196616
  0x040D631A
  0x041D6551
  0x04026433
  0x04126028
  0x040663ED
  0x040A6380
  0x040E631B
  0x041E6055
  0xC00989F0
  0x40002023
  0x04026430
  0x04126021
  0x040663E9
  0x04165DEB
  0x040A637B
  0x041A5E41
  0x040E6315
  0x041E604E
  0x040363FE
  0x04135FA8
  0x04076376
  0x0417660F
  0x040B632C
  0x041B64B0
  0x040F62C8
  0x041F5FD5
  0x04206400
  0x0421642A
  0x04006440
  0x04106039
  0x04046407
  0x041465AB
  0x0408636F
  0x04186617
  0x040C6317
  0x041C6552
  0x0401644B
  0x0411604C
  0x0405640B
  0x041565C1
  0x0409638D
  0x04196623
  0x040D6319
  0x041D654F
  0xC00989F1
  0x40002015
  0x0402642F
  0x04126022
  0x040663E8
  0x040A637D
  0x040E6318
  0x04206400
  0x04036400
  0x04076378
  0x040B632E
  0x040F62C8
  0x0400643F
  0x04046408
  0x0408636F
  0x040C6318
  0x0401644A
  0x0405640B
  0x0409638D
  0x040D6319
  0x041D654E
  0x00000000
  0xC00989F2
  0x40002019
  0x0401644A
  0x04116049
  0x0405640B
  0x04155DA7
  0x0409638B
  0x04195E09
  0x040D6318
  0x041D6549
  0x04206400
  0x04216429
  0x04026433
  0x04126026
  0x040663EC
  0x040A637F
  0x040E631C
  0x040363FF
  0x04076378
  0x040B632C
  0x040F62C8
  0x04006440
  0x04046408
  0x0408636F
  0x040C6318
  0x041C654F
  0xC00989F3
  0xF9010000
  0xF3810172
  0xF5800171
  0x40002023
  0x0400643C
  0x04106035
  0x04046403
  0x04145DA0
  0x0408636C
  0x04185E0C
  0x040C6315
  0x041C6550
  0x0401644C
  0x0411604C
  0x0405640C
  0x041565B5
  0x0409638D
  0x04196617
  0x040D631A
  0x041D6551
  0x04206400
  0x0421642A
  0x04026433
  0x04126028
  0x040663EC
  0x041665D8
  0x040A637F
  0x041A662E
  0x040E631B
  0x041E6053
  0x04036400
  0x04135FAB
  0x04076377
  0x041765FB
  0x040B632D
  0x041B64B8
  0x040F62C8
  0x041F5FD5
  0xC00989F4
  0x4000201D
  0x0400643D
  0x04106035
  0x04046404
  0x04145DA4
  0x0408636C
  0x04185E10
  0x040C6315
  0x041C654F
  0x04206400
  0x0421642A
  0x0401644B
  0x0411604A
  0x0405640C
  0x041565B9
  0x0409638C
  0x0419661B
  0x040D6319
  0x041D6555
  0x04026433
  0x04126024
  0x040663EC
  0x040A6380
  0x040E631A
  0x040363FE
  0x04076377
  0x040B632D
  0x040F62C7
  0x041F5FD3
  0xC00989F5
  0x40002023
  0x040363FD
  0x04135FA9
  0x04076377
  0x04175DF6
  0x040B632C
  0x041B64B0
  0x040F62C8
  0x041F5FD4
  0x04206400
  0x04216429
  0x0400643E
  0x04106038
  0x04046407
  0x04146591
  0x0408636F
  0x041865FD
  0x040C6317
  0x041C654D
  0x0401644A
  0x0411604B
  0x0405640B
  0x041565A6
  0x0409638B
  0x04196608
  0x040D6319
  0x041D654A
  0x04026431
  0x04126026
  0x040663EB
  0x041665C6
  0x040A637E
end buffer input around the non block frame header' (size=500)
