#ifndef MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_MVP_H
#define MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_MVP_H

// Minimal access to the VME <-> MVP flash interface of mesytec modules, the MVLC
// itself included (moduleBase = SelfVMEAddress). The full implementation lives
// in libmvp. This subset is enough to check the interface and boot a flash area.

#include <system_error>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc.h"
#include "util/int_types.h"

namespace mesytec::mvlc::mvp
{

static const u16 EnableFlashRegister = 0x6200;
static const u16 InputFifoRegister = 0x6202;
static const u16 OutputFifoRegister = 0x6204;
static const u16 StatusRegister = 0x6206;

namespace output_fifo_flags
{
    static const u32 InvalidRead = 1u << 9;
    static const u32 DataMask = 0xff;
}

namespace opcodes
{
    static const u8 NOP = 0x00;
    static const u8 RAI = 0x30; // read area index
    static const u8 BFP = 0x70; // set FPGA boot area index and boot
}

// Status byte at the end of each instruction response.
namespace status_flags
{
    static const u8 InstSuccess = 0x01;
    static const u8 AreaMask = 0x06;
    static const unsigned AreaShift = 1;
    static const u8 DipswitchMask = 0x18;
    static const unsigned DipswitchShift = 3;
}

inline u8 area_from_status(u8 statusByte)
{
    return (statusByte & status_flags::AreaMask) >> status_flags::AreaShift;
}

// Dipswitch setting = area booted at power-up.
inline u8 dipswitch_from_status(u8 statusByte)
{
    return (statusByte & status_flags::DipswitchMask) >> status_flags::DipswitchShift;
}

static const u8 AccessCode[] = { 0xCD, 0xAB };
static const unsigned AreaMax = 3;

// Minimum MVLC firmware revision supporting MVP access through VME.
static const u16 MVLCMinFirmwareRevision = 0x0036;

std::error_code MESYTEC_MVLC_EXPORT enable_flash_interface(MVLC &mvlc, u32 moduleBase);
std::error_code MESYTEC_MVLC_EXPORT disable_flash_interface(MVLC &mvlc, u32 moduleBase);

// Max number of reads clear_output_fifo() performs. Modules without MVP
// support never report an empty fifo.
static const unsigned OutputFifoMaxClearReads = 1024;

// Reads from the output fifo until it is empty. Returns
// std::errc::protocol_error if the fifo is not empty after
// OutputFifoMaxClearReads reads.
std::error_code MESYTEC_MVLC_EXPORT clear_output_fifo(MVLC &mvlc, u32 moduleBase);

std::error_code MESYTEC_MVLC_EXPORT write_instruction(
    MVLC &mvlc, u32 moduleBase, const std::vector<u8> &instruction);

// Waits for the first response byte, then reads until the output fifo is empty.
std::error_code MESYTEC_MVLC_EXPORT read_response(
    MVLC &mvlc, u32 moduleBase, std::vector<u8> &dest);

// Checks that the response mirrors the request and ends in 0xff followed by a
// status byte with the success bit set.
bool MESYTEC_MVLC_EXPORT check_response(
    const std::vector<u8> &request, const std::vector<u8> &response);

// NOP instruction round trip. The flash interface must be enabled. Optionally
// returns the status byte of the response.
std::error_code MESYTEC_MVLC_EXPORT nop(MVLC &mvlc, u32 moduleBase, u8 *statusByte = nullptr);

// RAI instruction: reads the currently selected area index. The flash
// interface must be enabled.
std::error_code MESYTEC_MVLC_EXPORT read_area_index(MVLC &mvlc, u32 moduleBase, u8 &area);

// Returns a super command buffer that uploads and immediately executes a stack
// writing the BFP instruction to the input fifo.
std::vector<u32> MESYTEC_MVLC_EXPORT make_boot_command_buffer(
    u32 moduleBase, u8 area, u16 superRef, u32 stackRef);

// Writes the boot command buffer to the command pipe without waiting for a
// response: the module reboots and does not answer. The flash interface must
// be enabled. Only write errors are reported, disconnect afterwards.
std::error_code MESYTEC_MVLC_EXPORT send_boot_instruction(MVLC &mvlc, u32 moduleBase, u8 area);

}

#endif // MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_MVP_H
