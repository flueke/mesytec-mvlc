/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020-2023 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_CONSTANTS_H__
#define __MESYTEC_MVLC_MVLC_CONSTANTS_H__

#include <cstddef>
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
// Communication with the MVLC is done using 32-bit wide binary data words.
// Results from commands and stack executions are also 32-bit aligned.
// All data is in little-endian byte order.

static const u32 AddressIncrement = 4;
static const u32 ReadLocalBlockMaxWords = 768;

// Limitation of the MVLC firmware when running lists of super commands.
// Subtract 2 for CmdBufferStart and CmdBufferEnd which have to be added to
// form a valid super command buffer.
// Note: ETH is additionally limited by the maximum size of ethernet frames!
static const size_t MirrorTransactionMaxWords = 2048; // FIXME: get rid of this. uploadStack() now decides based on whether ETH or USB is used
static const size_t MirrorTransactionMaxContentsWords = MirrorTransactionMaxWords - 2;

static const u8 MaxVMECrates = 8; // crate ids 0-7 transmitted in ETH headers and F3/F9 frames

// Super commands are commands that are directly interpreted and executed
// by the MVLC.
// The values in the SuperCommands enum contain the 2 high bytes of the
// command word.
// The output of super commands always goes to pipe 0, the CommandPipe.
namespace super_commands
{
    static const u32 SuperCmdMask  = 0xFFFF;
    static const u32 SuperCmdShift = 16;
    static const u32 SuperCmdArgMask = 0xFFFF;
    static const u32 SuperCmdArgShift = 0;

    enum class SuperCommandType: u16
    {
        CmdBufferStart = 0xF100,    // Marks the beginning of a command buffer
        CmdBufferEnd   = 0xF200,    // Marks the end of a command buffer
        ReferenceWord  = 0x0101,    // A reference word to be mirrored by the MVLC
        ReadLocal      = 0x0102,    // Read a MVLC register
        ReadLocalBlock = 0x0103,    // Read a block of local memory (not implemented).
        WriteLocal     = 0x0204,    // Write a MVLC register
        WriteReset     = 0x0206,    // Reset command (TODO: is this even used?)
        EthDelay       = 0x0207,    // Ethernet specific delay command for the throttle port only. This
                                    // command is not embedded in CmdBufferStart/End. The lowest 16-bits
                                    // contain a delay value in µs. The max value ((2^16)-1) is used to
                                    // completely stop the MVLC from sending data packets.
    };
}

using SuperCommandType = super_commands::SuperCommandType;

// Stack-only commands. These can be written into the stack memory area
// starting from StackMemoryBegin using WriteLocal commands.
//
// The output produced by a stack execution can go to either the
// CommandPipe or the DataPipe. This is encoded in the StackStart command.

namespace stack_commands
{
    static const u32 CmdMask      = 0xFF;
    static const u32 CmdShift     = 24;
    static const u32 CmdArg0Mask  = 0x00FF;
    static const u32 CmdArg0Shift = 16;
    static const u32 CmdArg1Mask  = 0x0000FFFF;
    static const u32 CmdArg1Shift = 0;

    // 'late' flag for vme_read and read_to_accu stored in bit 3 of the VME
    // data width arg.
    static const u32 LateReadShift = 2;

    enum class StackCommandType: u8
    {
        StackStart          = 0xF3, // First word in a command stack.
        StackEnd            = 0xF4, // Last word in a command stack.

        VMERead             = 0x12, // VME read requests including block reads.
                                    // Always FIFO mode reads (no address increment) for block transfers since FW0036.
                                    // Sensitive to the (stack scoped) SetAddressIncMode in earlier firmware versions.

        VMEReadSwapped      = 0x13, // For MBLT and 2eSST block reads: swaps word order. Use this instead
                                    // of VMERead if your module data arrives in the wrong order.
                                    // Always FIFO mode since FW0036 like VMERead above.

        VMEReadMem          = 0x32, // Same as VMERead but for reads from memory where the read address needs
                                    // to be incremented. New in FW0036.
        VMEReadMemSwapped   = 0x33, // Word swapped version of VMEReadMem. New in FW0036.

        VMEWrite            = 0x23, // VME write requests.
        WriteMarker         = 0xC2, // Writes a 32-bit marker value into the output data stream.
        WriteSpecial        = 0xC1, // Write a special value into the output data stream.
                                    // Values: 0=timestamp, 1=accumulator

        Wait                = 0xC4, // Delay in units of MVLC clocks. The number of clocks to delay is
                                    // specified as a 24-bit number.

        SignalAccu          = 0xC6, // Constant data word used to activate the interal signal array. This creates an
                                    // MVLC-internal IRQ signal.
        MaskShiftAccu       = 0xC5, // first mask is applied, then the left rotation
        SetAccu             = 0xC8, // Set the accumulator to a specific 32 bit value.
        ReadToAccu          = 0x14, // Single register VME read into the accumulator.
        CompareLoopAccu     = 0xC7, // CompareMode, 0=eq, 1=lt, 2=gt. Loops to previous command if false.
    };
}

using StackCommandType = stack_commands::StackCommandType;

// Constants for working with incoming data frames.
namespace frame_headers
{
    enum FrameTypes: u8
    {
        SuperFrame          = 0xF1, // Outermost command buffer response frame.
        SuperContinuation   = 0xF2, // Since FW0036: Continuation frame for Super Command mirror responses.
                                    // Same mechanism as 0xF3/0xF9 for the stack command layer.
        StackFrame          = 0xF3, // Outermost frame for readout data produced by command stack execution.
        BlockRead           = 0xF5, // Inner frame for block reads. Always contained within a StackFrame.
        StackError          = 0xF7, // Error notification frame embedded either between readout data or
                                    // sent to the command port for monitoring.
        StackContinuation   = 0xF9, // Continuation frame for StackFrame frames with the Continue bit set.
                                    // The last F9 frame in a sequence has the Continue bit cleared.
        SystemEvent         = 0xFA, // Software generated frames used for transporting additional
                                    // information. See the system_event namespace below for details.

        SystemEvent2        = 0xFB, // Software generated frames used for transporting additional
                                    // information. See the system_event namespace below for details.
    };

    // Header: Type[7:0] Continue[0:0] ErrorFlags[2:0] StackNum[3:0] CtrlId[2:0] Length[12:0]
    // TTTT TTTT CEEE SSSS IIIL LLLL LLLL LLLL
    // The Continue bit and the ErrorFlags are combined into a 4 bit
    // FrameFlags field.

    static const u8 TypeShift           = 24;
    static const u8 TypeMask            = 0xff;

    static const u8 FrameFlagsMask      = 0xf;
    static const u8 FrameFlagsShift     = 20;

    static const u8 StackNumShift       = 16;
    static const u8 StackNumMask        = 0xf;

    static const u8 CtrlIdShift         = 13;
    static const u8 CtrlIdMask          = 0b111;

    static const u16 LengthShift        = 0;
    static const u16 LengthMask         = 0x1fff;
}

inline u8 get_frame_type(u32 header)
{
    return (header >> frame_headers::TypeShift) & frame_headers::TypeMask;
}

// Constants describing the Continue and ErrorFlag bits present in StackFrames
// and StackContinuations.
namespace frame_flags
{
    // These shifts are relative to the beginning of the FrameFlags field.
    namespace shifts
    {
        static const u8 Timeout     = 0;
        static const u8 BusError    = 1;
        static const u8 SyntaxError = 2;
        static const u8 Continue    = 3;
    }

    static const u8 Timeout     = 1u << shifts::Timeout;
    static const u8 BusError    = 1u << shifts::BusError;
    static const u8 SyntaxError = 1u << shifts::SyntaxError;
    static const u8 Continue    = 1u << shifts::Continue;

    static const u8 AllErrorFlags = (
        frame_flags::Timeout |
        frame_flags::BusError |
        frame_flags::SyntaxError);
}

// Software generated system events which do not collide with the MVLCs framing
// format.
namespace system_event
{
    // TTTT TTTT CIII SSSS SSSL LLLL LLLL LLLL
    // Type     [ 7:0] set to 0xFA
    // Continue [ 0:0] continue bit set for all but the last part
    // CtrlId   [ 2:0] 3 bit MVLC controller id
    // Subtype  [ 6:0] 7 bit system event SubType
    // Length   [12:0] 13 bit length counted in 32-bit words

    static const u8 ContinueShift = 23;
    static const u8 ContinueMask  = 0b1;

    static const u8 CtrlIdShift   = 20;
    static const u8 CtrlIdMask    = 0b111;

    static const u8 SubtypeShift  = 13;
    static const u8 SubtypeMask   = 0x7f;

    static const u16 LengthShift  = 0;
    static const u16 LengthMask   = 0x1fff;

    static const u32 EndianMarkerValue = 0x12345678u;

    namespace subtype
    {
        static const u8 EndianMarker    = 0x01;

        // Written right before a DAQ run starts. Contains a software timestamp.
        static const u8 BeginRun        = 0x02;

        // Written right before a DAQ run ends. Contains a software timestamp.
        static const u8 EndRun          = 0x03;

        // For compatibility with existing mvme-generated listfiles. This
        // section contains a JSON encoded version of the mvme VME setup.
        // This section is not directly used by the library.
        static const u8 MVMEConfig      = 0x10;

        // Software generated low-accuracy timestamp, written once per second.
        // Contains a software timestamp.
        static const u8 UnixTimetick    = 0x11;

        // Written when the DAQ is paused. Contains a software timestamp.
        static const u8 Pause           = 0x12;

        // Written when the DAQ is resumed. Contains a software timestamp.
        static const u8 Resume          = 0x13;

        // The config section generated by this library.
        static const u8 MVLCCrateConfig = 0x14;

        // Summary of stack error counters received on the MVLC command pipe.
        // The readout can periodically write these into the data stream.
        //
        // Format of each 32-bit word:
        //   [stackNum (4), frame_flags (4), stackLine (8), count(16)]
        static const u8 StackErrors     = 0x15;

        // Range reversed for user-defined system events injected into the data
        // stream.
        static const u8 UserMin         = 0x20;
        static const u8 UserMax         = 0x2F;

        // Written before closing the listfile.
        static const u8 EndOfFile       = 0x77;

        static const u8 SubtypeMax      = SubtypeMask;
    }

    inline u8 extract_subtype(u32 header)
    {
        return (header >> SubtypeShift) & SubtypeMask;
    }
}

enum class VMEDataWidth
{
    D16 = 0x1,
    D32 = 0x2
};

enum class Blk2eSSTRate: u8
{
    Rate160MB = 0,
    Rate276MB = 1,
    Rate320MB = 2,
};

// Shift relative to the AddressMode argument of the read.
static const u8 Blk2eSSTRateShift = 6;

// For the WriteSpecial command
enum class SpecialWord: u8
{
    Timestamp,
    Accu,
};

enum class AccuComparator: u8
{
    EQ, // ==
    LT, // <
    GT, // >
};

namespace stacks
{
    // 16 stacks since FW0037, 8 stacks in earlier firmware versions.
    static const u8 StackCountPreFW0037 = 8;
    static const u8 StackCount = 16;

    // First stack trigger register.
    static const u16 Stack0TriggerRegister = 0x1100;

    // Note: The stack offset registers take offsets from StackMemoryBegin, not
    // absolute memory addresses. The offsets are counted in bytes, not words.
    static const u16 Stack0OffsetRegister  = 0x1200;

    static const u16 StackMemoryBegin      = 0x2000;
    static const u16 StackMemoryWords      = 2048;
    static const u16 StackMemoryBytes      = StackMemoryWords * 4;
    static const u16 StackMemoryEnd        = StackMemoryBegin + StackMemoryBytes;

    // Mask for the number of valid bits in the stack offset register.
    // Higher order bits outside the mask are ignored by the MVLC.
    static const u16 StackOffsetBitMaskWords    = 0x03FF;
    static const u16 StackOffsetBitMaskBytes    = StackOffsetBitMaskWords * 4;

    // The stack used for immediate execution, e.g for directly writing a VME
    // device register. This is a software-side convention only, hardware wise
    // nothing special is going on.
    static const u8 ImmediateStackID = 0;

    // Offset the immediate stack by this number of words from the start of the
    // stack memory. This allows deactivating a stack by settings its offset to 0
    // which will never contain a valid stack buffer.
    static const u16 ImmediateStackStartOffsetWords = 1;
    static const u16 ImmediateStackStartOffsetBytes = ImmediateStackStartOffsetWords * 4;

    // Readout stacks must start after this point.
    static const u16 ImmediateStackReservedWords = 256 - ImmediateStackStartOffsetWords;
    static const u16 ImmediateStackReservedBytes = ImmediateStackReservedWords * 4;

    // Constant for the standard layout where every stack is allocated a fixed
    // amount of memory.
    static const u16 StackMemorySegmentSize = 128; // TODO: get rid of this.

    // All stacks other than the one reserved for immediate execution can be
    // used as readout stacks activated by IRQ or via the Trigger/IO system.
    static const u8 FirstReadoutStackID = 1;
    static const u8 ReadoutStackCount = StackCount - 1u;

    enum TriggerType: u8
    {
        NoTrigger   = 0,
        IRQWithIACK = 1,
        IRQNoIACK   = 2,
        External    = 3, // external trigger via Trigger/IO system
    };

    // Note: For IRQ triggers the TriggerBits have to be set to the value
    // (IRQ-1), e.g. value 0 for IRQ1! Higher IRQ numbers have higher priority.
    static const u16 TriggerBitsMask    = 0b11111;
    static const u16 TriggerBitsShift   = 0;
    static const u16 TriggerTypeMask    = 0b111;
    static const u16 TriggerTypeShift   = 5;
    static const u16 ImmediateMask      = 0b1;
    static const u16 ImmediateShift     = 8;

    inline u16 get_trigger_register(u8 stackId)
    {
        return Stack0TriggerRegister + stackId * AddressIncrement;
    }

    inline u16 get_offset_register(u8 stackId)
    {
        return Stack0OffsetRegister + stackId * AddressIncrement;
    }

    enum TriggerSubtype: u8
    {
        IRQ1   =  0,
        IRQ2   =  1,
        IRQ3   =  2,
        IRQ4   =  3,
        IRQ5   =  4,
        IRQ6   =  5,
        IRQ7   =  6, // last VME IRQ
        IRQ8   =  7,
        IRQ9   =  8,
        IRQ10  =  9,
        IRQ11  = 10,
        IRQ12  = 11,
        IRQ13  = 12,
        IRQ14  = 13,
        IRQ15  = 14,
        IRQ16  = 15,

        // Since FW0037: triggers activated on MasterTrigger signal generated by
        // writing register 0x1306 or via Trigger I/O L3.MasterTrigger units.
        Slave0 = 16,
        Slave1 = 17,
        Slave2 = 18,
        Slave3 = 19,

        // Since FW0037: timers dedicated to periodic stack execution.
        Timer0 = 20,
        Timer1 = 21,
        Timer2 = 22,
        Timer3 = 23,
    };

    union Trigger
    {
        struct
        {
            u16 subtype: 5;
            u16 type: 3;
            u16 immediate: 1;
            u16 _: 7;
        };
        u16 value;
    } __attribute__((packed, aligned(1)));

    static_assert(sizeof(Trigger) == sizeof(u16));

    static const u16 SlaveTriggersCount = 4;

    // Since FW0037: StackTimer units that are not part of the Trigger I/O
    // module: can be used to periodically start stacks without having to use a
    // Timer unit connected to a StackStart unit. Uses Triggers::Timer0-3 as the
    // trigger values.
    static const u16 StackTimersCount = 4;
    static const u16 StackTimer0OffsetRegister  = 0x1180;

    inline u16 get_stacktimer_register(u8 timerId)
    {
        // Does not use the default AddressIncrement of 4 for some reason!
        return StackTimer0OffsetRegister + timerId * 2;
    }
} // end namespace stacks

constexpr const u32 SelfVMEAddress = 0xFFFF0000u;

namespace usb
{
    // Limit imposed by FT_WritePipeEx and FT_ReadPipeEx
    static const size_t USBSingleTransferMaxBytes = 1 * 1024 * 1024;
    static const size_t USBSingleTransferMaxWords = USBSingleTransferMaxBytes / sizeof(u32);
    // Under windows stream pipe mode is enabled for all read pipes and the
    // streaming read size is set to this value. This means all read requests
    // have to be of this exact size.
    static const size_t USBStreamPipeReadSize = USBSingleTransferMaxBytes;
} // end namespace usb

namespace eth
{
    static const u16 CommandPort = 0x8000; // 32768
    static const u16 DataPort = CommandPort + 1;
    static const u16 DelayPort = DataPort + 1;

    static const u32 HeaderWords = 2;
    static const u32 HeaderBytes = HeaderWords * sizeof(u32);

    namespace header0
    {
        // 2 bit packet channel number. Values represent different streams of
        // data each with its own packet number counter (see PacketChannel enum
        // below).
        static const u32 PacketChannelMask  = 0b11;
        static const u32 PacketChannelShift = 28;

        // 12 bit packet number
        // Packet channel specific incrementing packet number.
        static const u32 PacketNumberMask  = 0xfff;
        static const u32 PacketNumberShift = 16;

        // 3 Reserved Bits
        // TODO: this should contain the ctrl_id
        static const u32 CtrlIdMask        = 0x7;
        static const u32 CtrlIdShift       = 13;

        // 13 bit number of data words
        // This is the number of data words following the two header words.
        static const u32 NumDataWordsMask  = 0x1fff;
        static const u32 NumDataWordsShift = 0;
    }

    namespace header1
    {
        // 20 bit ETH timestamp. Increments in 1ms steps. Wraps after 17.5
        // minutes.
        static const u32 TimestampMask      = 0xfffff;
        static const u32 TimestampShift     = 12;

        // Points to the next buffer header word in the packet data. The
        // position directly after this header1 word is 0.
        // The maximum value possible indicates that there's no buffer header
        // present in the packet data. This means the packet must contain
        // continuation data from a previously started buffer.
        // This header pointer value can be used to resume processing data
        // packets in case of packet loss.
        static const u32 HeaderPointerMask  = 0xfff;
        static const u32 HeaderPointerShift = 0;
        static const u32 NoHeaderPointerPresent = HeaderPointerMask;
    }

    static const size_t JumboFrameMaxSize = 9000;

    enum class PacketChannel: u8
    {
        Command = 0, // Command and mirror responses
        Stack   = 1, // Data produced by stack executions routed to the command
                     // pipe (immediate stack executions and F7 error notifications).
        Data    = 2, // Readout data produced by stacks routed to the data pipe
    };

    static const u8 NumPacketChannels = 3;

    // Constants for the EthDelay command.
    static const u16 NoDelay = 0u;
    static const u16 StopSending = (1u << 16) - 1;

} // end namespace eth

// Note: these registers are available via low-level access
// (readRegister/writeRegister) and also via the internal VME interface at
// address 0xffff0000.
namespace registers
{
    // Setting bit 0 to 1 enables autonomous execution of stacks in reaction to
    // triggers.
    static const u16 daq_mode               = 0x1300;

    // R/W, 3 bit wide controller id. Transmitted in F3/F9 frames and ETH header0.
    static const u16 controller_id          = 0x1304;

    // Since FW0037: allows to generate master trigger signals without having to
    // use trigger io resources. 4 valid bits, one for each master trigger.
    // These signal are available in the trigger i/o as slave triggers and via
    // the SlaveN TriggerSubtype.
    static const u16 send_master_trigger    = 0x1306;

    // Since FW0039: when a direct stack command transaction finishes, the MVLC
    // now writes a status word to 0x1400 and copies the second word of stack
    // output data to 0x1404. By convention the second stack data word is the
    // transaction reference number written by a 'marker' command.
    // Having both the status word and the reference word allows to detect if
    // the last stack transaction was properly executed or if the command packet
    // was lost on the way to the MVLC. Retry code in mvlc.cc can now ensure
    // that retries are only done if the transaction has not been performed yet.
    // This works as long as unique reference numbers are used.
    // Note: the contents of 0x1400 might look like valid 0xF3/0xF7 frames
    // but they are not! Only the status bits (Timeout, BusError and
    // SyntaxError) are valid.
    static const u16 stack_exec_status0     = 0x1400;
    static const u16 stack_exec_status1     = 0x1404;

    // Send gap for USB in 0.415us. Defaults to 20000 == 8.3ms
    static const u16 usb_send_gap           = 0x0400;

    static const u16 own_ip_lo              = 0x4400;
    static const u16 own_ip_hi              = 0x4402;
    // Writing 0 values to own_ip_lo and own_ip_lo resets the IP and forces the
    // DHCP sequence to run again.

    static const u16 dhcp_active            = 0x4406; // 0 = fixed IP, 1 = DHCP
    static const u16 dhcp_ip_lo             = 0x4408;
    static const u16 dhcp_ip_hi             = 0x440a;

    static const u16 cmd_ip_lo              = 0x440c;
    static const u16 cmd_ip_hi              = 0x440e;

    static const u16 data_ip_lo             = 0x4410;
    static const u16 data_ip_hi             = 0x4412;

    static const u16 cmd_mac_0              = 0x4414;
    static const u16 cmd_mac_1              = 0x4416;
    static const u16 cmd_mac_2              = 0x4418;

    static const u16 cmd_dest_port          = 0x441a;
    static const u16 data_dest_port         = 0x441c;

    static const u16 data_mac_0             = 0x441e;
    static const u16 data_mac_1             = 0x4420;
    static const u16 data_mac_2             = 0x4422;

    // Set to 1 to enable 8k eth jumbo frames on the data pipe.
    static const u16 jumbo_frame_enable     = 0x4430;

    // Returns the delay value of the last ETH delay request on the throttling port.
    static const u16 eth_delay_read         = 0x4432;

    // Mask specifying what should be reset on a write to 0x6090.
    // TODO: document the bits and find out what works and what doesn't
    static const u16 reset_register_mask    = 0x0202;
    // Counter/state reset register.
    static const u16 reset_register         = 0x6090;
    // write: master reset, read: hardware_id
    static const u16 hardware_id            = 0x6008;
    // read: firmware_revision
    static const u16 firmware_revision      = 0x600e;
    static const u16 mcst_enable            = 0x6020;
    static const u16 mcst_address           = 0x6024;

    // Since FW0000_PTP (TODO: add the real version here, once it's released):
    // Number of errors generated by the command parser stage in the firmware.
    // Currently starts at 0x1000, then increments, wrapping at 16 bits.
    // Read-only.
    static const u16 parse_error_counter    = 0x8000;
} // end namespace registers

static const u8 CommandPipe = 0;
static const u8 DataPipe = 1;
static const u8 SuppressPipeOutput = 2;
static const unsigned PipeCount = 2; // SuppressPipeOutput is not counted as a Pipe

enum class Pipe: u8
{
    Command = CommandPipe,
    Data = DataPipe,
};

enum class ConnectionType
{
    USB,
    ETH
};

namespace stack_error_info
{
    static const unsigned StackLineMask = 0xffffu;
    static const unsigned StackLineShift = 0u;
    static const unsigned StackNumberMask = 0xffffu;
    static const unsigned StackNumberShift = 16u;
}

namespace listfile
{
    // Constant magic bytes at the start of the listfile. The terminating zero
    // is not written to file, so the markers use 8 bytes.
    constexpr size_t get_filemagic_len() { return 8; }
    constexpr const char *get_filemagic_eth() { return "MVLC_ETH"; }
    constexpr const char *get_filemagic_usb() { return "MVLC_USB"; }
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_CONSTANTS_H__ */
