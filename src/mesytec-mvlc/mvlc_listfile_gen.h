#ifndef __MESYTEC_MVLC_LISTFILE_GEN_H__
#define __MESYTEC_MVLC_LISTFILE_GEN_H__

#include "mesytec-mvlc/mvlc_constants.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_parser.h"
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
inline void write_module_data2(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(crateIndex <= frame_headers::CtrlIdMask);
    // +1 because the standard readout stack for event 0 is stack 1
    assert(eventIndex + 1 <= frame_headers::StackNumMask);


    // Store header offsets from the start of the dest buffer. Stored pointers would get
    // invalidated if the underlying buffer had to grow.
    s64 stackHeaderOffset = -1; // offset to the current F3/F9 frame from dest.data() in bytes
    s64 blockHeaderOffset = -1; // offset to the current F5 frame from dest.data() in bytes

    // FIXME: using a small value for testing
    //static const u32 FrameMaxWords = frame_headers::LengthMask;
    static const u32 FrameMaxWords = 10;

    u32 stackFrameWordsWritten = 0;
    u32 blockFrameWordsWritten = 0;

    auto get_cur_stack_header = [&dest, &stackHeaderOffset] () -> u32 *
    {
        assert(stackHeaderOffset >= 0);
        if (stackHeaderOffset < 0) return nullptr;
        auto addr = dest.data() + stackHeaderOffset;
        assert(addr + sizeof(u32) < dest.data() + dest.capacity());
        return reinterpret_cast<u32 *>(addr);
    };

    auto get_cur_block_header = [&dest, &blockHeaderOffset] () -> u32 *
    {
        assert(blockHeaderOffset >= 0);
        if (blockHeaderOffset < 0) return nullptr;
        auto addr = dest.data() + blockHeaderOffset;
        assert(addr + sizeof(u32) < dest.data() + dest.capacity());
        return reinterpret_cast<u32 *>(addr);
    };

    auto start_new_stack_frame = [&] (u8 frameType)
    {
        assert(blockHeaderOffset < 0);
        u32 stackHeader = (frameType << frame_headers::TypeShift
                           | (eventIndex + 1) << frame_headers::StackNumShift
                           | crateIndex << frame_headers::CtrlIdShift
                          );
        dest.push_back(stackHeader);
        stackHeaderOffset = dest.used() - sizeof(stackHeader);
        assert(stackHeaderOffset >= 0);
        stackFrameWordsWritten = 0;
    };

    auto start_new_block_frame = [&] ()
    {
        assert(stackHeaderOffset >= 0);
        u32 blockHeader = (frame_headers::BlockRead << frame_headers::TypeShift);
        dest.push_back(blockHeader);
        blockHeaderOffset = dest.used() - sizeof(blockHeader);
        assert(blockHeaderOffset >= 0);
        blockFrameWordsWritten = 0;
        ++stackFrameWordsWritten;
    };

    for (unsigned mi=0; mi<moduleCount; ++mi)
    {
        auto moduleData = moduleDataList[mi].data;
        //const auto moduleBegin = moduleData.data;
        const auto moduleEnd = moduleData.data + moduleData.size;
        auto moduleIter = moduleData.data;

        while (moduleIter != moduleEnd)
        {
            if (stackHeaderOffset < 0)
                start_new_stack_frame(frame_headers::StackFrame);

            if (blockHeaderOffset < 0)
                start_new_block_frame();

            while (moduleIter != moduleEnd
                   && stackFrameWordsWritten < FrameMaxWords
                   && blockFrameWordsWritten < FrameMaxWords)
            {
                dest.push_back(*moduleIter++);
                ++blockFrameWordsWritten;
                ++stackFrameWordsWritten;
            }

            *get_cur_block_header() |= blockFrameWordsWritten;

            if (moduleIter != moduleEnd)
            {
                // Set Continue in the block frame header and close the frame.
                *get_cur_block_header() |= frame_flags::Continue << frame_headers::FrameFlagsShift;
                blockHeaderOffset = -1;

                // Set Continue for the stack frame too and open a new StackContinuation frame.
                *get_cur_stack_header() |= frame_flags::Continue << frame_headers::FrameFlagsShift;
                *get_cur_stack_header() |= stackFrameWordsWritten; // size

                stackHeaderOffset = -1;
                start_new_stack_frame(frame_headers::StackContinuation);
            }
        }
    }

    if (stackHeaderOffset >= 0)
    {
        *get_cur_stack_header() |= stackFrameWordsWritten; // size
    }
}

// Note: always emits a BlockRead frame for each module
inline void write_module_data(ReadoutBuffer &dest, int crateIndex, int eventIndex,
                       const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
    assert(crateIndex < frame_headers::CtrlIdMask);

    // +1 because the standard readout stack for event 0 is stack 1
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
