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
    u32 wordsWritten = 0;  // Number of 32-bit words written to the frame.
};

bool is_frame_open(const FrameState &frameState)
{
    return frameState.headerOffset >= 0;
}

u32 *get_frame_header(const FrameState &frameState, ReadoutBuffer &dest)
{
    if (!is_frame_open(frameState))
        return nullptr;

    auto addr = dest.data() + frameState.headerOffset;
    assert(addr + sizeof(u32) <= dest.data() + dest.capacity());
    return reinterpret_cast<u32 *>(addr);
}

void close_frame(FrameState &frameState, ReadoutBuffer &dest)
{
    assert(is_frame_open(frameState));

    // Get the frame header, clear the length field and set it to
    // frameState.wordsWritten.
    auto hdrPtr = get_frame_header(frameState, dest);
    *hdrPtr &= ~frame_headers::LengthMask; // zero out the length field
    *hdrPtr |= frameState.wordsWritten & frame_headers::LengthMask;
    frameState.headerOffset = -1;
    frameState.wordsWritten = 0;
}

void push_frame_header(FrameState &frameState, ReadoutBuffer &dest, u32 frameHeader)
{
    dest.push_back(frameHeader);
    frameState.headerOffset = dest.used() - sizeof(frameHeader);
    assert(frameState.headerOffset >= 0);
    assert(dest.data() + frameState.headerOffset < dest.data() + dest.capacity());
    frameState.wordsWritten = 0;
}

struct GenState
{
    ReadoutBuffer *dest;
    const u32 frameMaxWords;
    const int crateIndex;
    const int eventIndex;
    FrameState stackFrameState; // outer stack frame (0xF3, 0xF9)
    FrameState blockFrameState; // inner block frame (0xF5)
};

u32 *get_cur_stack_header(const GenState &gs)
{
    return get_frame_header(gs.stackFrameState, *gs.dest);
}

u32 *get_cur_block_header(const GenState &gs)
{
    return get_frame_header(gs.blockFrameState, *gs.dest);
}

void close_cur_stack_frame(GenState &gs)
{
    close_frame(gs.stackFrameState, *gs.dest);
}

void close_cur_block_frame(GenState &gs)
{
    close_frame(gs.blockFrameState, *gs.dest);
}

bool has_open_stack_frame(const GenState &gs)
{
    return is_frame_open(gs.stackFrameState);
}

bool has_open_block_frame(const GenState &gs)
{
    return is_frame_open(gs.blockFrameState);
}

void start_new_stack_frame(GenState &gs)
{
    // Cannot start a new stack frame while a stack frame is open.
    assert(!has_open_stack_frame(gs));

    // Cannot start a new stack frame while a block frame is open.
    assert(!has_open_block_frame(gs));

    assert(gs.dest);

    const u8 frameType = frame_headers::StackFrame;
    u32 stackHeader = (frameType << frame_headers::TypeShift
                        | (gs.eventIndex + 1) << frame_headers::StackNumShift
                        | gs.crateIndex << frame_headers::CtrlIdShift
                        );

    push_frame_header(gs.stackFrameState, *gs.dest, stackHeader);
}

void start_new_block_frame(GenState &gs);

void continue_stack_frame(GenState &gs)
{
    // Adding a stack continuation only makes sense if there is an open stack
    // frame.
    assert(has_open_stack_frame(gs));


    // If there is an open block frame the caller was forced to create a stack
    // continuation due to frame size constraints. Close the block frame and
    // later on open a new one in the created stack continuation.
    bool reopenBlockFrame = false;

    if (has_open_block_frame(gs))
    {
        *get_cur_block_header(gs) |= frame_flags::Continue << frame_headers::FrameFlagsShift;
        close_cur_block_frame(gs);
        reopenBlockFrame = true;
    }

    assert(!has_open_block_frame(gs));

    assert(gs.dest);

    *get_cur_stack_header(gs) |= frame_flags::Continue << frame_headers::FrameFlagsShift;
    close_cur_stack_frame(gs);

    const u8 frameType = frame_headers::StackContinuation;
    u32 stackHeader = (frameType << frame_headers::TypeShift
                        | (gs.eventIndex + 1) << frame_headers::StackNumShift
                        | gs.crateIndex << frame_headers::CtrlIdShift
                        );

    push_frame_header(gs.stackFrameState, *gs.dest, stackHeader);

    if (reopenBlockFrame)
        start_new_block_frame(gs);
}

void start_new_block_frame(GenState &gs)
{
    // Can only open a new block frame if there is an open stack frame.
    assert(has_open_stack_frame(gs));

    // Also enforce that there is no open block frame.
    assert(!has_open_block_frame(gs));

    assert(gs.dest);

    // If the current stack frame would reach its max size by adding the block
    // frame header add a new stack continuation frame instead. Otherwise we'd
    // get a block frame header with size 0 and the continue flag set., then a
    // stack continuation and more non-empty block frames.
    if (gs.stackFrameState.wordsWritten >= gs.frameMaxWords - 1)
        continue_stack_frame(gs);

    u32 blockHeader = (frame_headers::BlockRead << frame_headers::TypeShift);

    push_frame_header(gs.blockFrameState, *gs.dest, blockHeader);
    ++gs.stackFrameState.wordsWritten;
}

void continue_block_frame(GenState &gs)
{
    // Can only continue a block frame if there is an open stack frame.
    assert(has_open_stack_frame(gs));

    // Adding a block continuation only makes sense if there is an open block
    // frame.
    assert(has_open_block_frame(gs));

    assert(gs.dest);

    *get_cur_block_header(gs) |= frame_flags::Continue << frame_headers::FrameFlagsShift;
    close_cur_block_frame(gs);

    u32 blockHeader = (frame_headers::BlockRead << frame_headers::TypeShift);

    push_frame_header(gs.blockFrameState, *gs.dest, blockHeader);
    ++gs.stackFrameState.wordsWritten;
}

void push_data_word(GenState &gs, u32 dataWord)
{
    assert(has_open_stack_frame(gs));
    assert(gs.stackFrameState.wordsWritten < gs.frameMaxWords);
    assert(gs.dest);
    assert(!has_open_block_frame(gs) || gs.blockFrameState.wordsWritten < gs.frameMaxWords);

    gs.dest->push_back(dataWord);

    if (has_open_block_frame(gs))
        ++gs.blockFrameState.wordsWritten;

    ++gs.stackFrameState.wordsWritten;
}

void write_module_data(GenState &gs, const readout_parser::ModuleData &moduleData)
{
    assert(readout_parser::size_consistency_check(moduleData));

    auto write_non_block_word = [&gs] (const u32 word)
    {
        assert(!has_open_block_frame(gs));

        if (!has_open_stack_frame(gs))
            start_new_stack_frame(gs);
        else if (gs.stackFrameState.wordsWritten >= gs.frameMaxWords)
            continue_stack_frame(gs);

        push_data_word(gs, word);
    };

    auto write_block_word = [&gs] (const u32 word)
    {
        if (!has_open_stack_frame(gs))
        {
            assert(!has_open_block_frame(gs));
            start_new_stack_frame(gs);
        }
        else if (gs.stackFrameState.wordsWritten >= gs.frameMaxWords)
            continue_stack_frame(gs);

        if (!has_open_block_frame(gs))
            start_new_block_frame(gs);
        else if (gs.blockFrameState.wordsWritten >= gs.frameMaxWords)
            continue_block_frame(gs);

        push_data_word(gs, word);
    };

    auto dataIter = moduleData.data.data;

    for (auto i=0u; i<moduleData.prefixSize; ++i)
        write_non_block_word(*dataIter++);

    if (moduleData.hasDynamic)
    {
        start_new_block_frame(gs);
        for (auto i=0u; i<moduleData.dynamicSize; ++i)
            write_block_word(*dataIter++);
        close_cur_block_frame(gs);
    }

    for (auto i=0u; i<moduleData.suffixSize; ++i)
        write_non_block_word(*dataIter++);

    assert(dataIter == moduleData.data.data + moduleData.data.size);
    assert(!has_open_block_frame(gs));
}

} // end anon namespace


/* Notes:
 *
 * If moduleCount is 0 the function does nothing. This should not happen as
 * completely empty MVLC readout stacks make no sense.
 *
 * A (possibly empty) stack frame has to be created even if none of the
 * modules contain any data.
 *
 * An empty block frame has to be created if the module does have a dynamic
 * part, even if the dynamic size of the current event is 0.
 */
void write_event_data(
    ReadoutBuffer &dest, int crateIndex, int eventIndex,
    const readout_parser::ModuleData *moduleDataList, unsigned moduleCount,
    u32 frameMaxWords)
{
    assert(moduleCount);
    assert(frameMaxWords > 1);
    assert(crateIndex <= frame_headers::CtrlIdMask);
    // +1 because the standard readout stack for event 0 is stack 1
    assert(eventIndex + 1 <= frame_headers::StackNumMask);

    if (moduleCount == 0 || frameMaxWords <= 1)
        return;

    GenState gs =
    {
        .dest = &dest,
        .frameMaxWords = frameMaxWords,
        .crateIndex = crateIndex,
        .eventIndex = eventIndex,
        .stackFrameState = {},
        .blockFrameState = {},
    };

    dest.setType(ConnectionType::USB);
    start_new_stack_frame(gs);

    for (unsigned mi=0; mi<moduleCount; ++mi)
        write_module_data(gs, moduleDataList[mi]);

    assert(!has_open_block_frame(gs));

    if (has_open_stack_frame(gs))
        close_cur_stack_frame(gs);
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
        push_frame_header(frameState, dest, header);
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

    assert(is_frame_open(frameState));
    if (is_frame_open(frameState))
        close_frame(frameState, dest);
}

}
}
}
