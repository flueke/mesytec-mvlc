#ifndef __MVME2_EXTERNAL_MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESY_VME_FORMAT_CHECKER_H_
#define __MVME2_EXTERNAL_MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESY_VME_FORMAT_CHECKER_H_

#include <vector>

#include "util/int_types.h"
#include "mvlc_readout_parser.h"

namespace mesytec::mvlc
{

namespace vme_modules
{
    // Full 16 bit values of the hardware id register (0x6008).
    struct HardwareIds
    {
        u16 MADC_32 = 0x5002;
        u16 MQDC_32 = 0x5003;
        u16 MTDC_32 = 0x5004;
        u16 MDPP_16 = 0x5005;
        // The VMMRs use the exact same software, so the hardware ids are equal.
        // VMMR-8 is a VMMR-16 with 8 busses missing.
        u16 VMMR_8  = 0x5006;
        u16 VMMR_16 = 0x5006;
        u16 MDPP_32 = 0x5007;
    };

    // Firmware type is encoded in the highest nibble of the firmware register
    // (0x600e). The lower nibbles contain the firmware revision. Valid for both
    // MDPP-16 and MDPP-32 but not all packages exist for the MDPP-32.
    struct MDPP_FirmwareTypes
    {
        u16 PADC = 0;
        u16 SCP  = 1;
        u16 RCP  = 2;
        u16 QDC  = 3;
    };
}

struct FormatCheckerState
{
    using WorkBuffer = readout_parser::ReadoutParserState::WorkBuffer;

    // Last readout buffer number that was processed. Note: the readout worker
    // starts with buffer number 1, not 0! Doing this makes handling counter
    // wrapping easier.
    u32 lastBufferNumber = 0;

    // Linear data from a stack execution. No more mvlc framing or udp packet headers.
    WorkBuffer stackExecData;

    u32 currentStackHeader = 0;
    u32 currentBlockHeader = 0;
};

void format_checker_process_buffer(
    FormatCheckerState &state, u32 bufferNumber, const u32 *buffer, size_t bufferWords);

}

#endif // __MVME2_EXTERNAL_MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESY_VME_FORMAT_CHECKER_H_