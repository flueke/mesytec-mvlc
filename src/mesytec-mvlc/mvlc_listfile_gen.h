#ifndef __MESYTEC_MVLC_LISTFILE_GEN_H__
#define __MESYTEC_MVLC_LISTFILE_GEN_H__

#include "mvlc_listfile.h"
#include "mvlc_readout_parser.h"
#include "readout_buffer.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// Note: always emits a BlockRead frame for each module
inline void write_module_data(ReadoutBuffer &dest, int crateIndex, int eventIndex,
                       const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(crateIndex < frame_headers::CtrlIdMask);

    // +1 because the readout stack for event 0 is stack 1
    assert(eventIndex + 1 < frame_headers::StackNumMask);

    size_t outputWordCount = 1u; // event header

    for (unsigned mi=0;mi<moduleCount;++mi)
    {
        outputWordCount += 1; // module header
        outputWordCount += moduleDataList[mi].data.size;
    }

    // -1 because the event header itself is not counted in the frames size field
    assert(outputWordCount - 1 < frame_headers::LengthMask);

    u32 eventHeader = (frame_headers::StackFrame << frame_headers::TypeShift
                       | (eventIndex + 1) << frame_headers::StackNumShift
                       | crateIndex << frame_headers::CtrlIdShift
                       | (outputWordCount - 1)
                      );

    dest.push_back(eventHeader);

    for (unsigned mi=0;mi<moduleCount;++mi)
    {
        const auto &moduleData = moduleDataList[mi].data;

        assert(moduleData.size < frame_headers::LengthMask);

        u32 moduleHeader = (frame_headers::BlockRead << frame_headers::TypeShift
                            | moduleData.size
                           );

        dest.push_back(moduleHeader);

        for (auto it=moduleData.data; it<moduleData.data + moduleData.size; ++it)
            dest.push_back(*it);
    }
}


// Writes out an adjusted header containing the specified crateIndex, followed by the unmodified
// system event data.
//template<typename Dest>
//void write_system_event(Dest &dest, int crateIndex, const u32 *header, u32 size)
inline void write_system_event(ReadoutBuffer &dest, int crateIndex, const u32 *header, u32 size)
{
    assert(crateIndex < frame_headers::CtrlIdMask);
    assert(size > 0);
    assert(size < frame_headers::LengthMask);
    assert(get_frame_type(*header) == frame_headers::SystemEvent);

    u32 headerWithCrateIndex = *header | crateIndex << frame_headers::CtrlIdShift;

    dest.push_back(headerWithCrateIndex);

    for (auto it=header+1; it<header+size; ++it)
        dest.push_back(*it);
}


#if 0
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
#endif

/* Alternative to a new format: reuse the USB framing format.
 * This solution would allow to reuse the existing readout_parser and other tools.
 */

}
}
}

#endif /* __MESYTEC_MVLC_LISTFILE_GEN_H__ */
