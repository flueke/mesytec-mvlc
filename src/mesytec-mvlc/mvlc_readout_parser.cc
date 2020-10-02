/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "mvlc_readout_parser.h"

#include <cassert>
#include <iostream>

#include "mvlc_buffer_validators.h"
#include "mvlc_constants.h"
#include "mvlc_impl_eth.h"
#include "util/io_util.h"
#include "util/storage_sizes.h"
#include "util/string_view.hpp"
#include "vme_constants.h"

#define LOG_LEVEL_OFF     0
#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_READOUT_PARSER_LOG_LEVEL
#define MVLC_READOUT_PARSER_LOG_LEVEL LOG_LEVEL_WARN
#endif

#define LOG_LEVEL_SETTING MVLC_READOUT_PARSER_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_rdo_parser ", fmt, ##__VA_ARGS__)

using namespace nonstd;
using std::cout;
using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

GroupReadoutStructure parse_group_readout_commands(const std::vector<StackCommand> &commands)
{
    using StackCT = StackCommand::CommandType;

    enum State { Prefix, Dynamic, Suffix };
    State state = Prefix;
    GroupReadoutStructure modParts = {};

    for (const auto &cmd: commands)
    {
        if ((cmd.type == StackCT::VMERead
             && !vme_amods::is_block_mode(cmd.amod))
            || cmd.type == StackCT::WriteMarker)
            // TODO: WriteSpecial if and when it is used by the MVLC
        {
            switch (state)
            {
                case Prefix:
                    modParts.prefixLen++;
                    break;
                case Dynamic:
                    modParts.suffixLen++;
                    state = Suffix;
                    break;
                case Suffix:
                    modParts.suffixLen++;
                    break;
            }
        }
        else if (cmd.type == StackCT::VMERead || cmd.type == StackCT::VMEMBLTSwapped)
        {
            assert(vme_amods::is_block_mode(cmd.amod));

            switch (state)
            {
                case Prefix:
                    modParts.hasDynamic = true;
                    state = Dynamic;
                    break;
                case Dynamic:
                    throw std::runtime_error("multiple block reads in module readout");
                case Suffix:
                    throw std::runtime_error("block read the suffix part in module readout");
            }
        }
    }

    return modParts;
}

ReadoutParserState::ReadoutStructure build_readout_structure(
    const std::vector<StackCommandBuilder> &readoutStacks)
{
    ReadoutParserState::ReadoutStructure result;

    for (const auto &stack: readoutStacks)
    {
        std::vector<GroupReadoutStructure> groupStructure;

        for (const auto &group: stack.getGroups())
        {
            groupStructure.emplace_back(parse_group_readout_commands(group.commands));
        }

        result.emplace_back(groupStructure);
    }

    return result;
}

const char *get_parse_result_name(const ParseResult &pr)
{
    switch (pr)
    {
        case ParseResult::Ok:
            return "Ok";

        case ParseResult::NoHeaderPresent:
            return "NoHeaderPresent";

        case ParseResult::NoStackFrameFound:
            return "NoStackFrameFound";

        case ParseResult::NotAStackFrame:
            return "NotAStackFrame";

        case ParseResult::NotABlockFrame:
            return "NotABlockFrame";

        case ParseResult::NotAStackContinuation:
            return "NotAStackContinuation";

        case ParseResult::StackIndexChanged:
            return "StackIndexChanged";

        case ParseResult::StackIndexOutOfRange:
            return "StackIndexOutOfRange";

        case ParseResult::GroupIndexOutOfRange:
            return "GroupIndexOutOfRange";

        case ParseResult::EmptyStackFrame:
            return "EmptyStackFrame";

        case ParseResult::UnexpectedOpenBlockFrame:
            return "UnexpectedOpenBlockFrame";


        case ParseResult::ParseReadoutContentsNotAdvancing:
            return "ParseReadoutContentsNotAdvancing";

        case ParseResult::ParseEthBufferNotAdvancing:
            return "ParseEthBufferNotAdvancing";

        case ParseResult::ParseEthPacketNotAdvancing:
            return "ParseEthPacketNotAdvancing";

        case ParseResult::UnexpectedEndOfBuffer:
            return "UnexpectedEndOfBuffer";

        case ParseResult::UnhandledException:
            return "UnhandledException";

        case ParseResult::ParseResultMax:
            break;
    }

    return "UnknownParseResult";
}

namespace
{

class end_of_buffer: public std::runtime_error
{
    public:
        explicit end_of_buffer(const char *arg): std::runtime_error(arg) {}
        end_of_buffer(): std::runtime_error("end_of_buffer") {}
};

using WorkBuffer = ReadoutParserState::WorkBuffer;

inline void ensure_free_space(WorkBuffer &workBuffer, size_t freeWords)
{
    if (workBuffer.free() < freeWords)
        workBuffer.buffer.resize(workBuffer.buffer.size() + freeWords);
}

inline void copy_to_workbuffer(
    ReadoutParserState &state, basic_string_view<u32> &source, size_t wordsToCopy)
{
    assert(source.size() >= wordsToCopy);

    if (source.size() < wordsToCopy)
        throw end_of_buffer();

    auto &dest = state.workBuffer;

    ensure_free_space(dest, wordsToCopy);

    std::copy(
        std::begin(source), std::begin(source) + wordsToCopy,
        dest.buffer.data() + dest.used);

    source.remove_prefix(wordsToCopy);
    state.workBuffer.used += wordsToCopy;
    state.curStackFrame.wordsLeft -= wordsToCopy;
}

} // end anon namespace

static const size_t InitialWorkerBufferSize = util::Megabytes(1) / sizeof(u32);

MESYTEC_MVLC_EXPORT ReadoutParserState make_readout_parser(
    const std::vector<StackCommandBuilder> &readoutStacks)
{
    ReadoutParserState result = {};
    result.readoutStructure = build_readout_structure(readoutStacks);

    size_t maxGroupCount = 0;

    for (const auto &groupReadoutStructure: result.readoutStructure)
    {
        maxGroupCount = std::max(maxGroupCount, groupReadoutStructure.size());
    }

    result.readoutDataSpans.resize(maxGroupCount);

    ensure_free_space(result.workBuffer, InitialWorkerBufferSize);

    return result;
}

inline void clear_readout_data_spans(std::vector<GroupReadoutSpans> &spans)
{
    std::fill(spans.begin(), spans.end(), GroupReadoutSpans{});
}

inline bool is_event_in_progress(const ReadoutParserState &state)
{
    return state.eventIndex >= 0;
}

inline void parser_clear_event_state(ReadoutParserState &state)
{
    state.eventIndex = -1;
    state.groupIndex = -1;
    state.curStackFrame = ReadoutParserState::FrameParseState{};
    state.curBlockFrame = ReadoutParserState::FrameParseState{};
    state.groupParseState = ReadoutParserState::Prefix;
    assert(!is_event_in_progress(state));
}

inline ParseResult parser_begin_event(ReadoutParserState &state, u32 frameHeader)
{
    assert(!is_event_in_progress(state));

    auto frameInfo = extract_frame_info(frameHeader);

    if (frameInfo.type != frame_headers::StackFrame)
    {
        LOG_WARN("NotAStackFrame: 0x%08x", frameHeader);
        return ParseResult::NotAStackFrame;
    }

    int eventIndex = frameInfo.stack - 1;

    if (eventIndex < 0 || static_cast<unsigned>(eventIndex) >= state.readoutStructure.size())
        return ParseResult::StackIndexOutOfRange;

    state.workBuffer.used = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = eventIndex;
    state.groupIndex = 0;
    state.groupParseState = ReadoutParserState::Prefix;
    state.curStackFrame = ReadoutParserState::FrameParseState{ frameHeader };
    state.curBlockFrame = ReadoutParserState::FrameParseState{};

    assert(is_event_in_progress(state));
    return ParseResult::Ok;
}

inline s64 calc_buffer_loss(u32 bufferNumber, u32 lastBufferNumber)
{
    s64 diff = bufferNumber - lastBufferNumber;

    if (diff < 1) // overflow
    {
        diff = std::numeric_limits<u32>::max() + diff;
        return diff;
    }
    return diff - 1;
}

// Checks if the input iterator points to a system frame header. If true the
// systemEvent callback is invoked with the frame header + frame data and true
// is returned. Also the iterator will be placed on the next word after the
// system frame.
// Otherwise the iterator is left unmodified and false is returned.
//
// Throws end_of_buffer() if the system frame exceeeds the input buffer size.
inline bool try_handle_system_event(
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    basic_string_view<u32> &input)
{
    if (!input.empty())
    {
        u32 frameHeader = input[0];

        if (get_frame_type(frameHeader) == frame_headers::SystemEvent)
        {
            auto frameInfo = extract_frame_info(frameHeader);

            // It should be guaranteed that the whole frame fits into the buffer.
            if (input.size() <= frameInfo.len)
                throw end_of_buffer("SystemEvent frame size exceeds input buffer size.");

            u8 subtype = system_event::extract_subtype(frameHeader);
            ++counters.systemEvents[subtype];

            // Pass the frame header itself and the contents to the system event
            // callback.
            callbacks.systemEvent(input.data(), frameInfo.len + 1);
            input.remove_prefix(frameInfo.len + 1);
            return true;
        }
    }

    return false;
}

// Search forward until a header with the wanted frame type is found.
// Only StackFrame and StackContinuation headers are accepted as valid frame
// types. If any other value is encountered nullptr is returned immediately
// (this case is to protect from interpreting faulty data as valid frames and
// extracting bogus lengths).
//
// The precondition is that the iterator is placed on a frame header. The
// search is started from there.
inline const u32 *find_stack_frame_header(
    basic_string_view<u32> &input, u8 wantedFrameType)
{
    auto is_accepted_frame_type = [] (u8 frameType) -> bool
    {
        return (frameType == frame_headers::StackFrame
                || frameType == frame_headers::StackContinuation);
    };

    while (!input.empty())
    {
        auto frameInfo = extract_frame_info(input[0]);

        if (frameInfo.type == wantedFrameType)
            return input.data();

        if (!is_accepted_frame_type(frameInfo.type))
            return nullptr;

        if (input.size() <= frameInfo.len)
            throw end_of_buffer("find_stack_frame_header: buffer size exceeded");

        input.remove_prefix(frameInfo.len + 1);
    }

    return nullptr;
}

inline const u32 *find_stack_frame_header(
    u32 *firstFrameHeader, const u32 *endOfData, u8 wantedFrameType)
{
    basic_string_view<u32> input(firstFrameHeader, endOfData - firstFrameHeader);
    return find_stack_frame_header(input, wantedFrameType);
}

// This is called with an iterator over a full USB buffer or with an iterator
// limited to the payload of a single UDP packet.
// A precondition is that the iterator is placed on a mvlc frame header word.
ParseResult parse_readout_contents(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    basic_string_view<u32> &input,
    bool is_eth,
    u32 bufferNumber)
{
    auto originalInputView = input;

    try
    {
        const u32 *inputBegin = input.data();

        while (!input.empty())
        {
            const u32 *lastIterPosition = input.data();

            // Find a stack frame matching the current parser state. Return if no
            // matching frame is detected at the current iterator position.
            if (!state.curStackFrame)
            {
                // If there's no open stack frame there should be no open block
                // frame either. Also data from any open blocks must've been
                // consumed previously or the block frame should have been manually
                // invalidated.
                assert(!state.curBlockFrame);
                if (state.curBlockFrame)
                    return ParseResult::UnexpectedOpenBlockFrame;

                // USB buffers from replays can contain system frames alongside
                // readout generated frames. For ETH buffers the system frames are
                // handled further up in parse_readout_buffer() and may not be
                // handled here because the packets payload can start with
                // continuation data from the last frame right away which could
                // match the signature of a system frame (0xFA) whereas data from
                // USB buffers always starts on a frame header.
                if (!is_eth && try_handle_system_event(callbacks, counters, input))
                    continue;

                if (is_event_in_progress(state))
                {
                    // Leave the frame header in the buffer for now. In case of an
                    // 'early error return' the caller can modify the state and
                    // retry parsing from the same position.

                    if (input.empty())
                        throw end_of_buffer("next stack frame header in event");

                    auto frameInfo = extract_frame_info(input[0]);

                    if (frameInfo.type != frame_headers::StackContinuation)
                        return ParseResult::NotAStackContinuation;

                    if (frameInfo.stack - 1 != state.eventIndex)
                        return ParseResult::StackIndexChanged;

                    // The stack frame is ok and can now be extracted from the
                    // buffer.
                    state.curStackFrame = ReadoutParserState::FrameParseState{ input[0] };
                    LOG_TRACE("new curStackFrame: 0x%08x", state.curStackFrame.header);
                    input.remove_prefix(1);

                    if (state.curStackFrame.wordsLeft == 0)
                    {
                        // This is the case with a StackContinuation header of
                        // length 0 (e.g. 0xF9010000 for stack 1).
                        // This implicitly ends the current block frame if any.
                        // This means that although the current block frame has
                        // the continue bit set there's no follow up frame
                        // present.
                        // The code below (switch-case over the group/module
                        // parts) checks the current block frame for the
                        // continue bit and tries to read the next part if it
                        // is set. To avoid this the block frame is cleared
                        // here.
                        //cout << "Hello empty stackFrame!" << endl;
                        LOG_WARN("got an empty stack frame: 0x%08x",
                                 state.curStackFrame.header);
                        ++counters.emptyStackFrames;
                        state.curBlockFrame = ReadoutParserState::FrameParseState{};
                    }
                }
                else
                {
                    // No event is in progress either because the last one was
                    // parsed completely or because of internal buffer loss during
                    // a DAQ run or because of external network packet loss.
                    // We now need to find the next StackFrame header starting from
                    // the current iterator position and hand that to
                    // parser_begin_event().
                    const u32 *prevIterPtr = input.data();

                    const u32 *nextStackFrame = find_stack_frame_header(
                        input, frame_headers::StackFrame);

                    if (!nextStackFrame)
                        return ParseResult::NoStackFrameFound;

                    assert(input.data() == nextStackFrame);

                    auto stackFrameOffset = nextStackFrame - prevIterPtr;
                    LOG_TRACE("found next StackFrame: @%p 0x%08x (searchOffset=%lu)",
                              nextStackFrame, *nextStackFrame, stackFrameOffset);

                    auto unusedWords = nextStackFrame - prevIterPtr;

                    counters.unusedBytes += unusedWords * sizeof(u32);

                    if (unusedWords)
                        LOG_DEBUG("skipped over %lu words while searching for the next"
                                  " stack frame header", unusedWords);

                    if (input.empty())
                        throw end_of_buffer("stack frame header of new event");

                    auto pr = parser_begin_event(state, *nextStackFrame);

                    if (pr != ParseResult::Ok)
                    {
                        LOG_WARN("error from parser_begin_event, iter offset=%ld, bufferNumber=%u",
                                 nextStackFrame - inputBegin,
                                 bufferNumber);
                        return pr;
                    }

                    input.remove_prefix(1); // eat the StackFrame marking the beginning of the event

                    assert(is_event_in_progress(state));
                }
            }

            assert(is_event_in_progress(state));
            assert(0 <= state.eventIndex
                   && static_cast<size_t>(state.eventIndex) < state.readoutStructure.size());

            const auto &groupReadoutInfos = state.readoutStructure[state.eventIndex];

            // Check for the case where a stack frame for an event is produced but
            // the event does not contain any modules. This can happen for example
            // when a periodic event is added without any modules.
            // The frame header for the event should have length 0.
            if (groupReadoutInfos.empty())
            {
                auto fi = extract_frame_info(state.curStackFrame.header);
                if (fi.len != 0u)
                {
                    LOG_WARN("No modules in event %d but got a non-empty "
                             "stack frame of len %u (header=0x%08x)",
                             state.eventIndex, fi.len, state.curStackFrame.header);
                }

                parser_clear_event_state(state);
                return ParseResult::Ok;
            }

            if (static_cast<size_t>(state.groupIndex) >= groupReadoutInfos.size())
                return ParseResult::GroupIndexOutOfRange;


            const auto &moduleParts = groupReadoutInfos[state.groupIndex];

            if (moduleParts.prefixLen == 0 && !moduleParts.hasDynamic && moduleParts.suffixLen == 0)
            {
                // The group/module does not have any of the three parts, its
                // readout is completely empty.
                ++state.groupIndex;
            }
            else
            {
                auto &moduleSpans = state.readoutDataSpans[state.groupIndex];

                switch (state.groupParseState)
                {
                    case ReadoutParserState::Prefix:
                        if (moduleSpans.prefixSpan.size < moduleParts.prefixLen)
                        {
                            // record the offset of the first word of this span
                            if (moduleSpans.prefixSpan.size == 0)
                                moduleSpans.prefixSpan.offset = state.workBuffer.used;

                            u32 wordsLeftInSpan = moduleParts.prefixLen - moduleSpans.prefixSpan.size;
                            assert(wordsLeftInSpan);
                            u32 wordsToCopy = std::min({
                                wordsLeftInSpan,
                                    static_cast<u32>(state.curStackFrame.wordsLeft),
                                    static_cast<u32>(input.size())});

                            copy_to_workbuffer(state, input, wordsToCopy);
                            moduleSpans.prefixSpan.size += wordsToCopy;
                        }

                        assert(moduleSpans.prefixSpan.size <= moduleParts.prefixLen);

                        if (moduleSpans.prefixSpan.size == moduleParts.prefixLen)
                        {
                            if (moduleParts.hasDynamic)
                            {
                                state.groupParseState = ReadoutParserState::Dynamic;
                                continue;
                            }
                            else if (moduleParts.suffixLen != 0)
                            {
                                state.groupParseState = ReadoutParserState::Suffix;
                                continue;
                            }
                            else
                            {
                                // We're done with this module as it does have neither
                                // dynamic nor suffix parts.
                                state.groupIndex++;
                                state.groupParseState = ReadoutParserState::Prefix;
                            }
                        }

                        break;

                    case ReadoutParserState::Dynamic:
                        {
                            assert(moduleParts.hasDynamic);

                            if (state.curStackFrame.wordsLeft > 0 && !state.curBlockFrame)
                            {
                                if (input.empty())
                                {
                                    // Need more data to read in the next block frame header.
                                    return ParseResult::Ok;
                                    //throw end_of_buffer("next module dynamic block frame header");
                                }

                                // Peek the potential block frame header
                                state.curBlockFrame = ReadoutParserState::FrameParseState{ input[0] };

                                LOG_TRACE("state.curBlockFrame.header=0x%x", state.curBlockFrame.header);

                                if (state.curBlockFrame.info().type != frame_headers::BlockRead)
                                {
                                    // Verbose debug output
#if 0
                                    s64 currentOffset = input.data() - originalInputView.data();
                                    s64 logStartOffset = std::max(currentOffset - 400, static_cast<s64>(0));
                                    size_t logWordCount = std::min(input.size(), static_cast<size_t>(400u + 100lu));

                                    basic_string_view<u32> logView(
                                        originalInputView.data() + logStartOffset,
                                        logWordCount);

                                    util::log_buffer(std::cout, logView, "input around the non block frame header");

                                    //util::log_buffer(std::cout, input, "input (the part that's left to parse)");
                                    //cout << "offset=" << (input.data() - originalInputView.data()) << endl;
                                    //std::terminate();
#endif

                                    LOG_WARN("NotABlockFrame: type=0x%x, frameHeader=0x%08x",
                                              state.curBlockFrame.info().type,
                                              state.curBlockFrame.header);

                                    state.curBlockFrame = ReadoutParserState::FrameParseState{};
                                    parser_clear_event_state(state);
                                    return ParseResult::NotABlockFrame;
                                }

                                // Block frame header is ok, consume it taking care of
                                // the outer stack frame word count as well.
                                input.remove_prefix(1);
                                state.curStackFrame.consumeWord();
                            }

                            // record the offset of the first word of this span
                            if (moduleSpans.dynamicSpan.size == 0)
                                moduleSpans.dynamicSpan.offset = state.workBuffer.used;

                            u32 wordsToCopy = std::min(
                                static_cast<u32>(state.curBlockFrame.wordsLeft),
                                static_cast<u32>(input.size()));

                            copy_to_workbuffer(state, input, wordsToCopy);
                            moduleSpans.dynamicSpan.size += wordsToCopy;
                            state.curBlockFrame.wordsLeft -= wordsToCopy;

                            if (state.curBlockFrame.wordsLeft == 0
                                && !(state.curBlockFrame.info().flags & frame_flags::Continue))
                            {

                                if (moduleParts.suffixLen == 0)
                                {
                                    // No suffix, we're done with the module
                                    state.groupIndex++;
                                    state.groupParseState = ReadoutParserState::Prefix;
                                }
                                else
                                {
                                    state.groupParseState = ReadoutParserState::Suffix;
                                    continue;
                                }
                            }
                        }
                        break;

                    case ReadoutParserState::Suffix:
                        if (moduleSpans.suffixSpan.size < moduleParts.suffixLen)
                        {
                            // record the offset of the first word of this span
                            if (moduleSpans.suffixSpan.size == 0)
                                moduleSpans.suffixSpan.offset = state.workBuffer.used;

                            u32 wordsLeftInSpan = moduleParts.suffixLen - moduleSpans.suffixSpan.size;
                            assert(wordsLeftInSpan);
                            u32 wordsToCopy = std::min({
                                wordsLeftInSpan,
                                    static_cast<u32>(state.curStackFrame.wordsLeft),
                                    static_cast<u32>(input.size())});

                            copy_to_workbuffer(state, input, wordsToCopy);
                            moduleSpans.suffixSpan.size += wordsToCopy;
                        }

                        if (moduleSpans.suffixSpan.size >= moduleParts.suffixLen)
                        {
                            // Done with the module
                            state.groupIndex++;
                            state.groupParseState = ReadoutParserState::Prefix;
                        }

                        break;
                }
            }

            // Skip over modules that do not have any readout data.
            // Note: modules that are disabled in the vme config are handled this way.
            while (state.groupIndex < static_cast<int>(groupReadoutInfos.size())
                   && is_empty(groupReadoutInfos[state.groupIndex]))
            {
                ++state.groupIndex;
            }

            auto update_part_size_info = [] (ReadoutParserCounters::PartSizeInfo &sizeInfo, size_t size)
            {
                sizeInfo.min = std::min(sizeInfo.min, static_cast<size_t>(size));
                sizeInfo.max = std::max(sizeInfo.max, static_cast<size_t>(size));
                sizeInfo.sum += size;
            };

            if (state.groupIndex >= static_cast<int>(groupReadoutInfos.size()))
            {
                assert(!state.curBlockFrame);
                // All modules have been processed and the event can be flushed.

                callbacks.beginEvent(state.eventIndex);

                for (int mi = 0; mi < static_cast<int>(groupReadoutInfos.size()); mi++)
                {
                    const auto &moduleSpans = state.readoutDataSpans[mi];
                    const auto partIndex = std::make_pair(state.eventIndex, mi);

                    if (moduleSpans.prefixSpan.size)
                    {
                        callbacks.groupPrefix(
                            state.eventIndex, mi,
                            state.workBuffer.buffer.data() + moduleSpans.prefixSpan.offset,
                            moduleSpans.prefixSpan.size);

                        ++counters.groupPrefixHits[partIndex];
                        update_part_size_info(
                            counters.groupPrefixSizes[partIndex], moduleSpans.prefixSpan.size);
                    }

                    if (moduleSpans.dynamicSpan.size)
                    {
                        callbacks.groupDynamic(
                            state.eventIndex, mi,
                            state.workBuffer.buffer.data() + moduleSpans.dynamicSpan.offset,
                            moduleSpans.dynamicSpan.size);

                        ++counters.groupDynamicHits[partIndex];
                        update_part_size_info(
                            counters.groupDynamicSizes[partIndex], moduleSpans.dynamicSpan.size);
                    }

                    if (moduleSpans.suffixSpan.size)
                    {
                        callbacks.groupSuffix(
                            state.eventIndex, mi,
                            state.workBuffer.buffer.data() + moduleSpans.suffixSpan.offset,
                            moduleSpans.suffixSpan.size);

                        ++counters.groupSuffixHits[partIndex];
                        update_part_size_info(
                            counters.groupSuffixSizes[partIndex], moduleSpans.suffixSpan.size);
                    }
                }

                ++counters.eventHits[state.eventIndex];
                callbacks.endEvent(state.eventIndex);
                parser_clear_event_state(state);
            }

            if (input.data() == lastIterPosition)
                return ParseResult::ParseReadoutContentsNotAdvancing;
        }

        return ParseResult::Ok;
    }
    catch (const end_of_buffer &e)
    {
        LOG_DEBUG("caught end_of_buffer: %s", e.what());
        if (LOG_LEVEL_SETTING >= LOG_LEVEL_TRACE)
            util::log_buffer(std::cout, originalInputView, "originalInputView");
        throw;
    }

    return ParseResult::Ok;
}

inline void count_parse_result(ReadoutParserCounters &counters, const ParseResult &pr)
{
    ++counters.parseResults[static_cast<size_t>(pr)];
}

// IMPORTANT: This function assumes that packet loss is handled on the outside
// (parsing state should be reset on loss).
// The iterator must be bounded by the packets data.
ParseResult parse_eth_packet(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    basic_string_view<u32> input,
    u32 bufferNumber)
{
    if (input.size() < eth::HeaderWords)
        throw end_of_buffer("ETH header words");

    eth::PayloadHeaderInfo ethHdrs{ input[0], input[1] };

    LOG_TRACE("begin parsing packet %u, dataWords=%u, packetLen=%lu bytes",
              ethHdrs.packetNumber(), ethHdrs.dataWordCount(), input.size() * sizeof(u32));

    // Skip to the first payload contents word, right after the two ETH
    // headers. This can be trailing data words from an already open stack
    // frame or it can be the next stack frame (continuation) header.
    input.remove_prefix(eth::HeaderWords);

    if (!is_event_in_progress(state))
    {
        // Special case for the ETH readout: find the start of a new event by
        // interpreting the packets nextHeaderPointer value and searching from
        // there.

        if (!ethHdrs.isNextHeaderPointerPresent())
        {
            // Not currently parsing an event and no frame header present
            // inside the packet data which means we cannot start a new event
            // using this packets data.
            return ParseResult::NoHeaderPresent;
        }

        // Place the iterator on the packets first header word pointed to by
        // the eth headers. parse_readout_contents() will be called with this
        // iterator position and will be able to find a StackFrame from there.
        if (input.size() < ethHdrs.nextHeaderPointer())
            throw end_of_buffer("ETH next header pointer");

        input.remove_prefix(ethHdrs.nextHeaderPointer());
        counters.unusedBytes += ethHdrs.nextHeaderPointer() * sizeof(u32);

        if (ethHdrs.nextHeaderPointer() > 0)
            LOG_DEBUG("skipped %u words (%lu bytes) of eth packet data to jump to the next header",
                      ethHdrs.nextHeaderPointer(),
                      ethHdrs.nextHeaderPointer() * sizeof(u32));
    }

    try
    {
        while (!input.empty())
        {
            const u32 *lastInputPosition = input.data();

            auto pr = parse_readout_contents(
                state, callbacks, counters, input, true, bufferNumber);

            if (pr != ParseResult::Ok)
                return pr;

            LOG_TRACE("end parsing packet %u, dataWords=%u",
                      ethHdrs.packetNumber(), ethHdrs.dataWordCount());

            if (input.data() == lastInputPosition)
                return ParseResult::ParseEthPacketNotAdvancing;
        }
    }
    catch (const std::exception &e)
    {
        LOG_DEBUG("end parsing packet %u, dataWords=%u, exception=%s",
                  ethHdrs.packetNumber(), ethHdrs.dataWordCount(),
                  e.what());
        throw;
    }

    return {};
}

ParseResult parse_readout_buffer(
    ConnectionType bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    LOG_TRACE("begin: bufferNumber=%u, buffer=%p, bufferWords=%lu",
              bufferNumber, buffer, bufferWords);
    ParseResult result = {};

    try
    {
        switch (bufferType)
        {
            case ConnectionType::ETH:
                result = parse_readout_buffer_eth(
                    state, callbacks, counters, bufferNumber, buffer, bufferWords);
                break;

            case ConnectionType::USB:
                result =  parse_readout_buffer_usb(
                    state, callbacks, counters, bufferNumber, buffer, bufferWords);
                break;
        }
    }
    catch (const end_of_buffer &e)
    {
        ++counters.access()->parserExceptions;
        return ParseResult::UnexpectedEndOfBuffer;
    }
    catch (...)
    {
        ++counters.access()->parserExceptions;
        return ParseResult::UnhandledException;
    }

    LOG_TRACE("end: bufferNumber=%u, buffer=%p, bufferWords=%lu, result=%s",
              bufferNumber, buffer, bufferWords, get_parse_result_name(result));

    return result;
}

ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters_,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    const size_t bufferBytes = bufferWords * sizeof(u32);

    LOG_TRACE("begin parsing ETH buffer %u, size=%lu bytes", bufferNumber, bufferBytes);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    auto counters = counters_.access();

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far is discarded.
        parser_clear_event_state(state);
        counters->internalBufferLoss += bufferLoss;
        // Also clear the last packet number so that we do not end up with huge
        // packet loss counts on the parsing side which are entirely caused by
        // internal buffer loss.
        state.lastPacketNumber = -1;
    }

    basic_string_view<u32> input(buffer, bufferWords);
    std::vector<basic_string_view<u32>> packetViews;

    try
    {
        while (!input.empty())
        {
            const u32 *lastInputPosition = input.data();

            // ETH readout data consists of a mix of SystemEvent frames and raw
            // packet data starting with ETH header0.

            if (try_handle_system_event(callbacks, counters.ref(), input))
                continue;

            if (input.size() < eth::HeaderWords)
                throw end_of_buffer("ETH header words");

            // At this point the buffer iterator is positioned on the first of the
            // two ETH payload header words.
            eth::PayloadHeaderInfo ethHdrs{ input[0], input[1] };

#if 0
            if (state.firstPacketDebugDump)
            {
                cout << "first parsed readout eth packet:" << endl;
                cout << fmt::format("header0=0x{:08x}", input[0]) << endl;
                cout << fmt::format("header1=0x{:08x}", input[1]) << endl;
                cout << "  packetNumber=" << ethHdrs.packetNumber() << endl;
                cout << "  dataWordCount=" << ethHdrs.dataWordCount() << endl;
                cout << "  nextHeaderPointer=" << ethHdrs.nextHeaderPointer() << endl;

                state.firstPacketDebugDump = false;
            }
#endif

            // Ensure that the packet is fully contained in the input buffer.
            // This is a requirement for the buffer producer.
            size_t packetWords = eth::HeaderWords + ethHdrs.dataWordCount();

            if (input.size() < packetWords)
                throw end_of_buffer("ETH packet data exceeds input buffer size");

            if (state.lastPacketNumber >= 0)
            {
                // Check for packet loss. If there is loss clear the parsing
                // state before attempting to parse the packet.
                if (auto loss = eth::calc_packet_loss(state.lastPacketNumber,
                                                      ethHdrs.packetNumber()))
                {
                    parser_clear_event_state(state);
                    counters->ethPacketLoss += loss;
                    LOG_DEBUG("packet loss detected: lastPacketNumber=%d, packetNumber=%d, loss=%d",
                             state.lastPacketNumber,
                             ethHdrs.packetNumber(),
                             loss);
                }
            }

            // Record the current packet number
            state.lastPacketNumber = ethHdrs.packetNumber();

            basic_string_view<u32> packetInput(input.data(), packetWords);
            packetViews.push_back(packetInput);

            ParseResult pr = {};
            bool exceptionSeen = false;

            try
            {
                pr = parse_eth_packet(state, callbacks, counters.ref(), packetInput, bufferNumber);
            }
            catch (...)
            {
                exceptionSeen = true;

// very verbose printf debugging code
#if 0
                auto errorPacketNumber = ethHdrs.packetNumber();

                cout << fmt::format("begin parsed packet view for buffer#{} until exception:", bufferNumber) << endl;
                for (const auto &pv: packetViews)
                {
                    eth::PayloadHeaderInfo ethHdrs{ pv[0], pv[1] };

                    auto logHeader = fmt::format("  packet {}, dataWords={}, packetView.size()={}",
                                                 ethHdrs.packetNumber(), ethHdrs.dataWordCount(), pv.size());

                    if (ethHdrs.packetNumber() == errorPacketNumber)
                        logHeader = "!!! " + logHeader;

                    util::log_buffer(cout, pv, logHeader);
                }
                cout << fmt::format("end parsed packet view for buffer#{}", bufferNumber) << endl;

                cout << fmt::format("begin unparsed contents of buffer#{}", bufferNumber) << endl;
                while (!input.empty())
                {
                    u32 header0 = input[0];

                    if (get_frame_type(header0) == frame_headers::SystemEvent)
                    {
                        auto len = extract_frame_info(header0).len;
                        cout << fmt::format("skipping over system event of size {}", len) << endl;
                        input.remove_prefix(len + 1);
                    }
                    else if (input.size() >= eth::HeaderWords)
                    {
                        eth::PayloadHeaderInfo ethHdrs{ input[0], input[1] };
                        size_t packetWords = eth::HeaderWords + ethHdrs.dataWordCount();

                        if (input.size() < packetWords)
                            std::terminate();

                        auto logHeader = fmt::format("  unparsed packet {}, dataWords={}",
                                                     ethHdrs.packetNumber(), ethHdrs.dataWordCount());
                        basic_string_view<u32> pv(input.data(), packetWords);
                        util::log_buffer(cout, pv, logHeader);
                        input.remove_prefix(packetWords);
                    }
                }
                cout << fmt::format("end unparsed contents of buffer#{}", bufferNumber) << endl;

                std::terminate();
#endif
            }

            // Either an error or an exception from parse_eth_packet. Clear the
            // parsing state and advance the outer buffer iterator past the end
            // of the current packet. Then reenter the loop.
            if (pr != ParseResult::Ok || exceptionSeen)
            {
                parser_clear_event_state(state);
                ++counters->ethPacketsProcessed;
                counters->unusedBytes += packetWords * sizeof(u32);

                if (exceptionSeen)
                    ++counters->parserExceptions;
                else
                    count_parse_result(counters.ref(), pr);

                if (input.size() >= packetWords)
                {
                    input.remove_prefix(packetWords);

                    LOG_DEBUG("skipping %lu words of eth packet data due to an error result from the parser",
                             packetWords);
                }

                continue;
            }

            ++counters->ethPacketsProcessed;

            LOG_TRACE("parse_packet result: %d\n", (int)pr);

            // Skip over the packet ending up either on the next SystemEvent
            // frame or on the next packets header0.
            input.remove_prefix(packetWords);

            if (input.data() == lastInputPosition)
                return ParseResult::ParseEthBufferNotAdvancing;
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN("end parsing ETH buffer %u, size=%lu bytes, exception=%s",
                 bufferNumber, bufferBytes, e.what());

        parser_clear_event_state(state);
        counters->unusedBytes += input.size() * sizeof(u32);
        ++counters->parserExceptions;
        throw;
    }
    catch (...)
    {
        LOG_WARN("end parsing ETH buffer %u, size=%lu bytes, unknown exception",
                 bufferNumber, bufferBytes);

        parser_clear_event_state(state);
        counters->unusedBytes += input.size() * sizeof(u32);
        ++counters->parserExceptions;
        throw;
    }

    ++counters->buffersProcessed;
    auto unusedBytes = input.size() * sizeof(u32);
    counters->unusedBytes += unusedBytes;

    LOG_TRACE("end parsing ETH buffer %u, size=%lu bytes, unused bytes=%lu",
              bufferNumber, bufferBytes, unusedBytes);

    return {};
}

ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters_,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    const size_t bufferBytes = bufferWords * sizeof(u32);

    LOG_TRACE("begin parsing USB buffer %u, size=%lu bytes", bufferNumber, bufferBytes);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    auto counters = counters_.access();

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far will be discarded.
        parser_clear_event_state(state);
        counters->internalBufferLoss += bufferLoss;
    }

    basic_string_view<u32> input(buffer, bufferWords);

    try
    {
        while (!input.empty())
        {
            auto pr = parse_readout_contents(state, callbacks, counters.ref(), input, false, bufferNumber);
            count_parse_result(counters.ref(), pr);

            if (pr != ParseResult::Ok)
            {
                parser_clear_event_state(state);
                counters->unusedBytes += input.size() * sizeof(u32);
                return pr;
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN("end parsing USB buffer %u, size=%lu bytes, exception=%s",
                 bufferNumber, bufferBytes, e.what());

        parser_clear_event_state(state);
        counters->unusedBytes += input.size() * sizeof(u32);
        ++counters->parserExceptions;
        throw;
    }
    catch (...)
    {
        LOG_WARN("end parsing USB buffer %u, size=%lu bytes, unknown exception",
                 bufferNumber, bufferBytes);

        parser_clear_event_state(state);
        counters->unusedBytes += input.size() * sizeof(u32);
        ++counters->parserExceptions;
        throw;
    }

    ++counters->buffersProcessed;
    counters->unusedBytes += input.size() * sizeof(u32);
    LOG_TRACE("end parsing USB buffer %u, size=%lu bytes", bufferNumber, bufferBytes);

    return {};
}

} // end namespace readout_parser
} // end namespace mesytec
} // end namespace mvlc
