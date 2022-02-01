#ifndef __MESYTEC_MVLC_COMMON_LISTFILE_H__
#define __MESYTEC_MVLC_COMMON_LISTFILE_H__

/* Common listfile format
 *
 * Motivation: store preprocessed readout data, system events and config
 * information in a connection type independent format. The format must support
 * storing the merged data from a multi crate readout.
 */

#include "readout_buffer.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_parser.h"

namespace mesytec
{
namespace mvlc
{
namespace common_listfile
{


// Note: always emits a BlockRead frame for each module
// FIXME: Probably not the right API. Use an output iterator instead.
template<typename Dest>
void write_module_data(Dest &dest, int crateIndex, int eventIndex,
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

    dest.reserve(dest.size() + outputWordCount);

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

        std::copy(moduleData.data, moduleData.data + moduleData.size,
                  std::back_inserter(dest));
    }
}

template<typename Dest>
void write_system_event(Dest &dest, int crateIndex, const u32 *header, u32 size)
{
    assert(crateIndex < frame_headers::CtrlIdMask);
    assert(size < frame_headers::LengthMask);

    dest.reserve(dest.size() + size + 1);

    u32 eventHeader = (frame_headers::SystemEvent << frame_headers::TypeShift
                       | crateIndex << frame_headers::CtrlIdShift
                       | size
                      );

    dest.push_back(eventHeader);

    std::copy(header, header + size, std::back_inserter(dest));
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

#endif /* __MESYTEC_MVLC_COMMON_LISTFILE_H__ */
