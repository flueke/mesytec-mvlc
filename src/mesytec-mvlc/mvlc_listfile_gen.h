#ifndef __MESYTEC_MVLC_LISTFILE_GEN_H__
#define __MESYTEC_MVLC_LISTFILE_GEN_H__

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

// Listfile generation from unpacked/parsed module readout data and system event data.
// The generated format is compatible to the MVLC_USB framing format and it should be compatible
// with the existing readout_parser module.
//
// The crateIndex argument is used for the CtrlId field in the frame headers.

void write_module_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount,
    u32 frameMaxWords = frame_headers::LengthMask);

void write_system_event(
    ReadoutBuffer &dest, int crateIndex, const u32 *header, u32 size,
    u32 frameMaxWords = frame_headers::LengthMask);

}
}
}

#endif /* __MESYTEC_MVLC_LISTFILE_GEN_H__ */
