#ifndef __MVME2_EXTERNAL_MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESY_VME_FORMAT_CHECKER_H_
#define __MVME2_EXTERNAL_MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESY_VME_FORMAT_CHECKER_H_

#include <vector>

#include "util/int_types.h"
#include "mvlc_readout_parser.h"

namespace mesytec::mvlc
{

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