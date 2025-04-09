#ifndef __MESYTEC_MVLC_LISTFILE_GEN_H__
#define __MESYTEC_MVLC_LISTFILE_GEN_H__

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_constants.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_parser.h"
#include "mvlc_util.h"
#include "readout_buffer.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// Listfile generation from unpacked/parsed module readout data and system event
// data.  The generated format is compatible to the MVLC_USB framing format and
// thus should be parseable by the readout_parser module.
//
// The crateIndex argument is used for the CtrlId field in the frame headers.

void MESYTEC_MVLC_EXPORT write_event_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount,
    u32 frameMaxWords = frame_headers::LengthMask);

// Similar to the above but just copies raw data into the event buffer. No
// special handling for block frames or anything else.
// TODO: add this to the tests
void MESYTEC_MVLC_EXPORT write_event_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const u32 *data, size_t size,
    u32 frameMaxWords = frame_headers::LengthMask);

// The header argument must point to a buffer of size 'size', starting with a
// system event header. The header is reused in case the data has to be split
// into multiple frames.
void MESYTEC_MVLC_EXPORT write_system_event(
    ReadoutBuffer &dest, int crateIndex, const u32 *header, u32 size,
    u32 frameMaxWords = frame_headers::LengthMask);

}
}
}

#endif /* __MESYTEC_MVLC_LISTFILE_GEN_H__ */
