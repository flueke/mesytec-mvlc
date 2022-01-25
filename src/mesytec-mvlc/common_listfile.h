#ifndef __MESYTEC_MVLC_COMMON_LISTFILE_H__
#define __MESYTEC_MVLC_COMMON_LISTFILE_H__

/* Common listfile format
 *
 * Motivation: store preprocessed readout data, system events and config
 * information in a connection type independent format. The format must support
 * storing the merged data from a multi crate readout.
 */

#include "mvlc_listfile.h"

namespace mesytec
{
namespace mvlc
{
namespace common_listfile
{

namespace frame_headers
{
    enum FrameTypes: u8
    {
        BeginRun,
        EndRun,
        EventData,
        ModuleData,
        EndOfFile,
        SystemEvent = 0xA,
    };

    // Event: Type | CrateIndex | EventIndex | Size
    // Module: Type | ModuleIndex | Size
    // SystemEvent: Type | CrateIndex | Size

    static const u32 TypeShift = 28;
    static const u32 TypeMask  = 0xf; // 4 bit type

    static const u32 Index1Shift = 24;
    static const u32 Index1Mask  = 0xf; // 4 bit CrateIndex/ModuleIndex

    static const u32 Index2Shift = 20;
    static const u32 Index2Mask  = 0xf; // 4 bit EventIndex

    static const u32 FlagsShift  = 18;
    static const u32 FlagsMask   = 0b11; // 2 bit flags

    static const u32 SizeShift = 0u;
    static const u32 SizeMask  = 0x3ffffu; // 18 bit size in 32-bit words
}

}
}
}

#endif /* __MESYTEC_MVLC_COMMON_LISTFILE_H__ */
