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

#if 0
size_t calculate_output_size(const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    size_t res = 1u; // first StackFrame

    for (unsigned mi=0; mi<moduleCount;++mi)
    {

    }

    return res;
}
#endif

// Goal: write out all ModuleData blocks. Respect max frame sizes and use continuation frames
// and continue bits accordingly.
// Outer framing: F3 followed by optional F9 stack frames
// Inner framing: F5 block read frames
// All header types need to have the Continue bit if there is follow-up data.
void write_module_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount,
    u32 frameMaxWords = frame_headers::LengthMask);

// Writes out an adjusted header containing the specified crateIndex, followed by the unmodified
// system event data.
//template<typename Dest>
//void write_system_event(Dest &dest, int crateIndex, const u32 *header, u32 size)
void write_system_event(
    ReadoutBuffer &dest, int crateIndex, const u32 *header, u32 size,
    u32 frameMaxWords = frame_headers::LengthMask);

}
}
}

#endif /* __MESYTEC_MVLC_LISTFILE_GEN_H__ */
