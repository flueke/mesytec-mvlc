#include "mvlc_listfile_gen.h"
#include "mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

namespace
{
// Note: storing header offsets from the start of the dest buffer instead of raw pointers.
// Stored pointers would get invalidated if the underlying buffer had to grow.

struct FrameState
{
    s64 headerOffset = -1; // Offset from dest.data() to the frame header in bytes.
    u32 wordsWritten = 0;  // Number of words written to the frame.
};

auto get_frame_header = [] (const FrameState &frameState, ReadoutBuffer &dest) -> u32 *
{
    if (frameState.headerOffset < 0)
        return nullptr;
    auto addr = dest.data() + frameState.headerOffset;
    assert(addr + sizeof(u32) < dest.data() + dest.capacity());
    return reinterpret_cast<u32 *>(addr);
};

auto close_frame = [] (FrameState &frameState, ReadoutBuffer &dest)
{
    assert(frameState.headerOffset >= 0);
    // Assert that the value of the size field is 0, then set it to the number of words
    // written into the frame.
    auto hdrPtr = get_frame_header(frameState, dest);
    assert(get_frame_length(*hdrPtr) == 0);
    *get_frame_header(frameState, dest) |= frameState.wordsWritten;
    frameState.headerOffset = -1;
    frameState.wordsWritten = 0;
};

} // end anon namespace

void write_module_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount,
    u32 frameMaxWords)
{
    assert(frameMaxWords > 1);
    assert(crateIndex <= frame_headers::CtrlIdMask);
    // +1 because the standard readout stack for event 0 is stack 1
    assert(eventIndex + 1 <= frame_headers::StackNumMask);

    FrameState stackFrameState;
    FrameState blockFrameState;

    auto get_cur_stack_header = [&] () -> u32 *
    {
        return get_frame_header(stackFrameState, dest);
    };

    auto get_cur_block_header = [&] () -> u32 *
    {
        return get_frame_header(blockFrameState, dest);
    };

    auto start_new_stack_frame = [&] (u8 frameType)
    {
        // Cannot start a new stack frame while a block frame is open
        assert(blockFrameState.headerOffset < 0);

        u32 stackHeader = (frameType << frame_headers::TypeShift
                           | (eventIndex + 1) << frame_headers::StackNumShift
                           | crateIndex << frame_headers::CtrlIdShift
                          );

        dest.push_back(stackHeader);
        stackFrameState.headerOffset = dest.used() - sizeof(stackHeader);
        assert(stackFrameState.headerOffset >= 0);
        stackFrameState.wordsWritten = 0;
    };

    auto start_new_block_frame = [&] ()
    {
        // Cano only open a new block frame if there is an open stack frame.
        assert(stackFrameState.headerOffset >= 0);

        u32 blockHeader = (frame_headers::BlockRead << frame_headers::TypeShift);

        dest.push_back(blockHeader);
        blockFrameState.headerOffset = dest.used() - sizeof(blockHeader);
        assert(blockFrameState.headerOffset >= 0);
        blockFrameState.wordsWritten = 0;
        ++stackFrameState.wordsWritten;
    };

    auto close_cur_stack_frame = [&] ()
    {
        close_frame(stackFrameState, dest);
    };

    auto close_cur_block_frame = [&] ()
    {
        close_frame(blockFrameState, dest);
    };

    dest.setType(ConnectionType::USB);
    start_new_stack_frame(frame_headers::StackFrame);

    for (unsigned mi=0; mi<moduleCount; ++mi)
    {
        auto moduleData = moduleDataList[mi].data;
        const auto moduleEnd = moduleData.data + moduleData.size;
        auto moduleIter = moduleData.data;

        while (moduleIter != moduleEnd)
        {
            if (stackFrameState.headerOffset < 0)
                start_new_stack_frame(frame_headers::StackContinuation);

            if (blockFrameState.headerOffset < 0)
                start_new_block_frame();

            while (moduleIter != moduleEnd
                   && stackFrameState.wordsWritten < frameMaxWords
                   && blockFrameState.wordsWritten < frameMaxWords)
            {
                dest.push_back(*moduleIter++);
                ++blockFrameState.wordsWritten;
                ++stackFrameState.wordsWritten;
            }

            if (moduleIter != moduleEnd)
            {
                // Set Continue in the block frame header and close the frame.
                *get_cur_block_header() |= frame_flags::Continue << frame_headers::FrameFlagsShift;
                close_cur_block_frame();

                // Set Continue for the stack frame too and open a new StackContinuation frame.
                *get_cur_stack_header() |= frame_flags::Continue << frame_headers::FrameFlagsShift;
                close_cur_stack_frame();
                start_new_stack_frame(frame_headers::StackContinuation);
            }
            else
            {
                // Done with the module. Close the current block frame.
                close_cur_block_frame();

                // Also close the current stack frame if it is full.
                if (stackFrameState.wordsWritten >= frameMaxWords)
                {
                    // Not done with the last module yet so the Continue flag needs to be set in
                    // the stack header.
                    if (mi < moduleCount-1)
                        *get_cur_stack_header() |= frame_flags::Continue << frame_headers::FrameFlagsShift;
                    close_cur_stack_frame();
                }
            }
        }
    }

    if (stackFrameState.headerOffset >= 0)
        close_cur_stack_frame();
}

void write_system_event(
    ReadoutBuffer &dest, int crateIndex, const u32 *systemEventHeader, u32 size,
    u32 frameMaxWords)
{
    assert(frameMaxWords > 1);
    assert(crateIndex < frame_headers::CtrlIdMask);
    assert(get_frame_type(*systemEventHeader) == frame_headers::SystemEvent);

    FrameState frameState;

    auto start_new_frame = [&] ()
    {
        assert(frameState.headerOffset < 0);

        // Use the original system event header and add the crateIndex.
        u32 header = *systemEventHeader | crateIndex << system_event::CtrlIdShift;

        dest.push_back(header);
        frameState.headerOffset = dest.used() - sizeof(header);
        assert(frameState.headerOffset >= 0);
        frameState.wordsWritten = 0;
    };

    const u32 *iter = systemEventHeader + 1;
    const u32 * const end = systemEventHeader + size;

    start_new_frame();

    while (iter != end)
    {
        while (iter != end && frameState.wordsWritten < frameMaxWords)
        {
            dest.push_back(*iter++);
            ++frameState.wordsWritten;
        }

        if (iter != end)
        {
            *get_frame_header(frameState, dest) |= 1u << system_event::ContinueShift;
            close_frame(frameState, dest);
            start_new_frame();
        }
    }

    if (frameState.headerOffset >= 0)
        close_frame(frameState, dest);
}

}
}
}
