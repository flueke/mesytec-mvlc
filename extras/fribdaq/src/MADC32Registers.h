/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright MADCDELAY5.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#ifndef __MADC32REGISTERS_H
#define __MADC32REGISTERS_H
#include <VMEAddressModifier.h>

#ifndef Const
#define Const(name) static const int name =
#endif

// Note many of these register definitions work for the MTDC32 as well
// Those definitions of the form MTDCxxxx are additions for registers
// that are repurposed or new registers with the MTDC32.

Const(MADCDELAY)  1;

// The address modifiers that will be used to access the module:

static const uint8_t initamod(VMEAMod::a32UserData);   //  setup using user data access.
static const uint8_t readamod(VMEAMod::a32UserBlock);  //  Read in block mode.

static const uint8_t cbltamod(VMEAMod::a32UserBlock);
static const uint8_t mcstamod(VMEAMod::a32UserData);


// Module address map; for the most part I'm only defining the registers
// we'll actually use.

Const(eventBuffer)          0;

Const(Thresholds)           0x4000;

Const(AddressSource)        0x6000;
Const(Address)              0x6002;
Const(ModuleId)             0x6004;
Const(Reset)                0x6008; // write anything here to reset the module
Const(FirmwareRev)          0x600e;

Const(Ipl)                  0x6010;
Const(Vector)               0x6012;
Const(IrqTest)              0x6014;
Const(IrqReset)             0x6016;
Const(IrqThreshold)         0x6018;
Const(MaxTransfer)          0x601a;
Const(WithdrawIrqOnEmpty)   0x601c;

Const(CbltMcstControl)      0x6020;
Const(CbltAddress)          0x6022;
Const(McstAddress)          0x6024;


Const(LongCount)            0x6030;      // Units depend on data format.
Const(DataFormat)           0x6032;
Const(ReadoutReset)         0x6034;
Const(MultiEvent)           0x6036;
Const(MarkType)             0x6038;
Const(StartAcq)             0x603A;
Const(InitFifo)             0x603c;
Const(DataReady)            0x603e;

Const(BankOperation)        0x6040;
Const(Resolution)           0x6042;
Const(OutputFormat)         0x6044;

Const(HoldDelay0)           0x6050;
Const(MTDCBank0WinStart)    0x6050;

Const(HoldDelay1)           0x6052;
Const(MTDCBank1WinStart)    0x6052;

Const(HoldWidth0)           0x6054;
Const(MTDCBank0WinWidth)    0x6054;

Const(HoldWidth1)           0x6056;
Const(MTDCBank1WinWidth)    0x6056;

Const(EnableGDG)            0x6058;
Const(MTDCBank0TrigSource)  0x6058;
Const(MTDCBank1TrigSource)  0x605a;
Const(MTDCFirstHitOnly)     0x605c;

Const(InputRange)           0x6060;
Const(MTDCEdgeSelect)       0x6060;

Const(ECLTermination)       0x6062;
Const(ECLGate1OrTiming)     0x6064;   // Set timing source too.
Const(ECLFCOrTimeReset)     0x6066;


Const(MTDCTriggerSelect)   0x6068;


Const(NIMGate1OrTiming)     0x606a;   // Set timing source too.
Const(NIMFCOrTimeReset)     0x606c;
Const(NIMBusyFunction)      0x606e;

Const(TestPulser)           0x6070; // In order to ensure it's off !
Const(MTDCPulserPattern)    0x6072;
Const(MTDCBank0InputThr)    0x6078;
Const(MTDCBank1InputThr)    0x607a;

// Omitting the RCBus control registers since those are not so useful
// in the VM-USB environment.

Const(EventCounterReset)    0x6090;
Const(MTDCEventCtrLow)      0x6092;
Const(MTDCEventCtrHigh)     0x6094;
Const(TimingSource)         0x6096;
Const(TimingDivisor)        0x6098;
Const(TimestampReset)       EventCounterReset; // As of firmware 5.
Const(TSCounterLow)         0x609c;
Const(TSCounterHi)          0x609e;


// RC-bus registers
Const(RCBusNo)              0x6080;
Const(RCModNum)             0x6082;
Const(RCOpCode)             0x6084;
Const(RCAddr)               0x6086;
Const(RCData)               0x6088;
Const(RCStatus)             0x608a;// 48bit timestamp:

Const(MTDCCtrBTimeL)        0x60a8;
Const(MTDCCtrBTimeM)        0x60aa;
Const(MTDCCtrBTimeH)        0x60ac;
Const(MTDCStopCtrB)         0x60ae;

// RC-bus op code bits
Const(RCOP_ON)              0x03;
Const(RCOP_OFF)             0x04;
Const(RCOP_READID)          0x06;
Const(RCOP_WRITEDATA)       0x10;
Const(RCOP_READDATA)        0x12;

// RC-bus status bits
Const(RCSTAT_MASK)          0x01;
Const(RCSTAT_ACTIVE)        0x00;
Const(RCSTAT_ADDRCOLLISION) 0x02;
Const(RCSTAT_NORESPONSE)    0x04;

// MTDC Hit limits for an event:

Const(MTDCBank0HighLimit)   0x60b0;
Const(MTDCBank0LowLimit)    0x60b2;
Const(MTDCBank1HighLimit)     0x60b4;
Const(MTDCBank1LowLimit)    0x60b6;

// Mcast/CBLT control register bits:

Const(MCSTENB)              0x80;
Const(MCSTDIS)              0x40;
Const(FIRSTENB)             0x20;
Const(FIRSTDIS)             0x10;
Const(LASTENB)              0x08;
Const(LASTDIS)              0x04;
Const(CBLTENB)              0x02;
Const(CBLTDIS)              0x01;

// Data length format for MTDC 32

Const(MTDC8Bit)             0x0000;
Const(MTDC16Bit)            0x0001;
Const(MTDC32Bit)            0x0002;
Const(MTDC64Bit)            0x0003;

// Multi event control register:

Const(MTDCSingleEvent)      0x0000;
Const(MTDCMultEvent)        0x0001;
Const(MTDCLimitedMulti)     0x0003;
Const(MTDCSKIPBERR)         0x0004;
Const(MTDCCountEvents)      0x0010;

// Marker types:

Const(MTDCMarkEventCounter) 0x0000;
Const(MTDCMarkTimestamp)    0x0001;
Const(MTDCMarkExtendTStamp) 0x0003;

// TDC Resolultion values:

Const(MTDCRes500ps)         9;
Const(MTDCRes250ps)         8;
Const(MTDCRes125ps)         7;
Const(MTDCRes62_5ps)        6;
Const(MTDCRes31_3ps)        5;
Const(MTDCRes15_6ps)        4;
Const(MTDCRes7_8ps)         3;
Const(MTDCRes3_9ps)         2;

// TDC Output format:

Const(MTDCStandard)        0;
Const(MTDCContinuous)      1;    // like continous storage mode.

// TDC Joined bank trigger conditions (Window start).

Const(MTDCJointTrigSrcTr0)    0x0001;
Const(MTDCJointTrigSrcTr1)    0x0002;
Const(MTDCJointTrigChanShift) 0x0002;
Const(MTDCJointTrigChanEna)   0x0080;
Const(MTDCJointTrigBank0)     0x0100;
Const(MTDCJointTrigBank1)     0x0200;

// TDC SPlit bank triger conditions starts window for bank1

Const(MTDCBank1TrigBank0)     0x0001;
Const(MTDCBank1TrigBank1)     0x0002;
Const(MTDCBank1ChannelShift)  0x0002;
Const(MTDCBank1ChanEnab)      0x0080;


//
Const(MTDCBank0FirstHit)   1;
Const(MTDCBank1FirstHIt)   2;

#endif
