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
#include "util/logging.h"
#include "util/storage_sizes.h"
#include "util/string_view.hpp"
#include "vme_constants.h"

using namespace nonstd;

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

    // TODO: check that all (block) reads are handled including the stack accumulator one

namespace
{
    ModuleReadoutStructure parse_module_readout_commands(const std::vector<StackCommand> &commands)
    {
        using StackCT = StackCommand::CommandType;

        enum State { Initial, Fixed, Dynamic };
        State state = Initial;
        ModuleReadoutStructure modParts = {};

        // Tracks state of the MVLC stack accumulator functionality.
        // This is set to true on encountering SetAccu, ReadToAccu or CompareLoopAccu.
        // It is reset once a vme read is encountered.
        // The reason why this needs to be tracked is that using the accu turns
        // the following single(!) read vme instruction into a fake block
        // transfer: the MVLC takes the number of single reads to perform from
        // the accu and packages the resulting data into a standard 0xF5 block
        // frame.
        bool accumulatorActive = false;

        for (const auto &cmd: commands)
        {
            if ((cmd.type == StackCT::VMERead
                 && !vme_amods::is_block_mode(cmd.amod)
                 && !accumulatorActive)
                || cmd.type == StackCT::WriteMarker)
            {
                switch (state)
                {
                    case Initial:
                        state = Fixed;
                        ++modParts.len;
                        break;
                    case Fixed:
                        ++modParts.len;
                        break;
                    case Dynamic:
                        throw std::runtime_error("block read after fixed reads in module reaodut");
                }
            }
            else if (cmd.type == StackCT::VMERead || cmd.type == StackCT::VMEMBLTSwapped)
            {
                switch (state)
                {
                    case Initial:
                        state = Dynamic;
                        assert(modParts.len == 0);
                        modParts.len = -1;
                        break;
                    case Fixed:
                        throw std::runtime_error("fixed read after block read in module readout");
                    case Dynamic:
                        throw std::runtime_error("multiple block reads in module readout");
                }
                accumulatorActive = false;
            }
            else if (cmd.type == StackCT::Custom)
            {
                // Note: cmd.transfers contains the number of data words
                // produced by the custom stack command.
                switch (state)
                {
                    case Initial:
                        state = Fixed;
                        modParts.len += cmd.transfers;
                        break;
                    case Fixed:
                        modParts.len += cmd.transfers;
                        break;
                    case Dynamic:
                        throw std::runtime_error("custom stack command after block read in module readout");
                }
            }
            else if (cmd.type == StackCT::SetAccu
                     || cmd.type == StackCT::ReadToAccu
                     || cmd.type == StackCT::CompareLoopAccu)
            {
                accumulatorActive = true;
            }
        }

        return modParts;
    }
}

ReadoutParserState::ReadoutStructure build_readout_structure(
    const std::vector<StackCommandBuilder> &readoutStacks)
{
    ReadoutParserState::ReadoutStructure result;

    for (const auto &stack: readoutStacks)
    {
        std::vector<ModuleReadoutStructure> groupStructure;

        for (const auto &group: stack.getGroups())
        {
            groupStructure.emplace_back(parse_module_readout_commands(group.commands));
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
    const std::vector<StackCommandBuilder> &readoutStacks,
    void *userContext)
{
    ReadoutParserState result = {};
    result.readoutStructure = build_readout_structure(readoutStacks);

    size_t maxGroupCount = 0;

    for (const auto &groupReadoutStructure: result.readoutStructure)
    {
        maxGroupCount = std::max(maxGroupCount, groupReadoutStructure.size());
    }

    result.readoutDataSpans.resize(maxGroupCount);
    result.moduleDataBuffer.resize(maxGroupCount);

    ensure_free_space(result.workBuffer, InitialWorkerBufferSize);

    result.userContext = userContext;

    return result;
}

inline void clear_readout_data_spans(std::vector<ModuleReadoutSpans> &spans)
{
    std::fill(spans.begin(), spans.end(), ModuleReadoutSpans{});
}

inline bool is_event_in_progress(const ReadoutParserState &state)
{
    return state.eventIndex >= 0;
}

inline void parser_clear_event_state(ReadoutParserState &state)
{
    state.eventIndex = -1;
    state.moduleIndex = -1;
    state.curStackFrame = ReadoutParserState::FrameParseState{};
    state.curBlockFrame = ReadoutParserState::FrameParseState{};
    state.groupParseState = ReadoutParserState::Initial;
    assert(!is_event_in_progress(state));
}

inline ParseResult parser_begin_event(ReadoutParserState &state, u32 frameHeader)
{
    assert(!is_event_in_progress(state));

    auto frameInfo = extract_frame_info(frameHeader);

    if (frameInfo.type != frame_headers::StackFrame)
    {
        auto logger = get_logger("readout_parser");
        logger->warn("NotAStackFrame: 0x{:008x}", frameHeader);
        return ParseResult::NotAStackFrame;
    }

    int eventIndex = frameInfo.stack - 1;

    if (eventIndex < 0 || static_cast<unsigned>(eventIndex) >= state.readoutStructure.size())
    {
        auto logger = get_logger("readout_parser");
        logger->warn("parser_begin_event: StackIndexOutOfRange ({})", eventIndex);
        return ParseResult::StackIndexOutOfRange;
    }

    state.workBuffer.used = 0;
    clear_readout_data_spans(state.readoutDataSpans);

    state.eventIndex = eventIndex;
    state.moduleIndex = 0;
    state.groupParseState = ReadoutParserState::Initial;
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
    ReadoutParserState &state,
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
            const int crateIndex = 0;
            callbacks.systemEvent(state.userContext, crateIndex, input.data(), frameInfo.len + 1);
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
    auto logger = get_logger("readout_parser");

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
                if (!is_eth && try_handle_system_event(state, callbacks, counters, input))
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
                    {
                        logger->trace("NotAStackContinuation:"
                                 " curStackFrame.wordsLeft={}"
                                 " , curBlockFrame.wordsLeft={}"
                                 ", eventIndex={}, moduleIndex={}"
                                 ", inputOffset={}",
                                 state.curStackFrame.wordsLeft,
                                 state.curBlockFrame.wordsLeft,
                                 state.eventIndex,
                                 state.moduleIndex,
                                 input.data() - inputBegin
                                 );
                        return ParseResult::NotAStackContinuation;
                    }

                    if (frameInfo.stack - 1 != state.eventIndex)
                        return ParseResult::StackIndexChanged;

                    // The stack frame is ok and can now be extracted from the
                    // buffer.
                    state.curStackFrame = ReadoutParserState::FrameParseState{ input[0] };
                    logger->trace("new curStackFrame: 0x{:008x}", state.curStackFrame.header);
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
                        logger->warn("got an empty stack frame: 0x{:008x}",
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
                    logger->trace("found next StackFrame: @{} 0x{:008x} (searchOffset={})",
                              reinterpret_cast<const void *>(nextStackFrame),
                              *nextStackFrame, stackFrameOffset);

                    auto unusedWords = nextStackFrame - prevIterPtr;

                    counters.unusedBytes += unusedWords * sizeof(u32);

                    if (unusedWords)
                        logger->debug("skipped over {} words while searching for the next"
                                  " stack frame header", unusedWords);

                    if (input.empty())
                        throw end_of_buffer("stack frame header of new event");

                    auto pr = parser_begin_event(state, *nextStackFrame);

                    if (pr != ParseResult::Ok)
                    {
                        logger->warn("error from parser_begin_event, iter offset={}, bufferNumber={}: {}",
                                 nextStackFrame - inputBegin,
                                 bufferNumber,
                                 get_parse_result_name(pr)
                                 );
                        return pr;
                    }

                    input.remove_prefix(1); // eat the StackFrame marking the beginning of the event

                    assert(is_event_in_progress(state));
                }
            }

            assert(is_event_in_progress(state));
            assert(0 <= state.eventIndex
                   && static_cast<size_t>(state.eventIndex) < state.readoutStructure.size());

            const auto &moduleReadoutInfos = state.readoutStructure[state.eventIndex];
            const auto moduleCount = moduleReadoutInfos.size();

            // Check for the case where a stack frame for an event is produced but
            // the event does not contain any modules. This can happen for example
            // when a periodic event is added without any modules.
            // The frame header for the event should have length 0.
            if (moduleReadoutInfos.empty())
            {
                auto fi = extract_frame_info(state.curStackFrame.header);
                if (fi.len != 0u)
                {
                    logger->warn("No modules in event {} but got a non-empty "
                             "stack frame of len {} (header=0x{:008x})",
                             state.eventIndex, fi.len, state.curStackFrame.header);
                }

                logger->trace("parser_clear_event_state because moduleReadoutInfos.empty(), eventIndex={}",
                          state.eventIndex);
                parser_clear_event_state(state);
                return ParseResult::Ok;
            }

            if (static_cast<size_t>(state.moduleIndex) >= moduleCount)
                return ParseResult::GroupIndexOutOfRange;


            const auto &moduleParts = moduleReadoutInfos[state.moduleIndex];

            if (is_empty(moduleParts))
            {
                // Skip over groups/modules which have no data producing
                // readout commands.
                ++state.moduleIndex;
            }
            else
            {
                assert(!is_empty(moduleReadoutInfos[state.moduleIndex]));

                if (state.groupParseState == ReadoutParserState::Initial)
                {
                    if (!is_dynamic(moduleReadoutInfos[state.moduleIndex]))
                        state.groupParseState = ReadoutParserState::Fixed;
                    else
                        state.groupParseState = ReadoutParserState::Dynamic;
                }

                auto &moduleSpans = state.readoutDataSpans[state.moduleIndex];

                if (state.groupParseState == ReadoutParserState::Fixed)
                {
                    assert(!is_dynamic(moduleReadoutInfos[state.moduleIndex]));
                    assert(moduleParts.len >= 0);

                    if (moduleSpans.dataSpan.size < static_cast<u32>(moduleParts.len))
                    {
                        // record the offset of the first word of this span
                        if (moduleSpans.dataSpan.size == 0)
                            moduleSpans.dataSpan.offset = state.workBuffer.used;

                        u32 wordsLeftInSpan = moduleParts.len - moduleSpans.dataSpan.size;
                        assert(wordsLeftInSpan);
                        u32 wordsToCopy = std::min({
                            wordsLeftInSpan,
                                static_cast<u32>(state.curStackFrame.wordsLeft),
                                static_cast<u32>(input.size())});

                        copy_to_workbuffer(state, input, wordsToCopy);
                        moduleSpans.dataSpan.size += wordsToCopy;
                    }

                    assert(moduleSpans.dataSpan.size <= static_cast<u32>(moduleParts.len));

                    if (moduleSpans.dataSpan.size == static_cast<u32>(moduleParts.len))
                    {
                        // We're done with this module.
                        state.moduleIndex++;
                        state.groupParseState = ReadoutParserState::Initial;
                    }
                }
                else if (state.groupParseState == ReadoutParserState::Dynamic)
                {
                    assert(is_dynamic(moduleReadoutInfos[state.moduleIndex]));

                    if (state.curStackFrame.wordsLeft > 0 && !state.curBlockFrame)
                    {
                        if (input.empty())
                        {
                            // Need more data to read in the next block frame header.
                            return ParseResult::Ok;
                        }

                        // Peek the potential block frame header
                        state.curBlockFrame = ReadoutParserState::FrameParseState{ input[0] };

                        logger->trace("state.curBlockFrame.header=0x{:x}", state.curBlockFrame.header);

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

                            logger->warn("NotABlockFrame: type=0x{:x}, frameHeader=0x{:008x}",
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
                    if (moduleSpans.dataSpan.size == 0)
                        moduleSpans.dataSpan.offset = state.workBuffer.used;

                    u32 wordsToCopy = std::min(
                        static_cast<u32>(state.curBlockFrame.wordsLeft),
                        static_cast<u32>(input.size()));

                    copy_to_workbuffer(state, input, wordsToCopy);
                    moduleSpans.dataSpan.size += wordsToCopy;
                    state.curBlockFrame.wordsLeft -= wordsToCopy;

                    if (state.curBlockFrame.wordsLeft == 0
                        && !(state.curBlockFrame.info().flags & frame_flags::Continue))
                    {
                        // We're done with the module
                        state.moduleIndex++;
                        state.groupParseState = ReadoutParserState::Initial;
                    }
                }
            }

            // Skip over modules that do not have any readout data.
            // Note: modules that are disabled in the vme config are handled this way.
            while (state.moduleIndex < static_cast<int>(moduleCount)
                   && is_empty(moduleReadoutInfos[state.moduleIndex]))
            {
                ++state.moduleIndex;
            }

            auto update_part_size_info = [] (ReadoutParserCounters::PartSizeInfo &sizeInfo, size_t size)
            {
                sizeInfo.min = std::min(sizeInfo.min, static_cast<size_t>(size));
                sizeInfo.max = std::max(sizeInfo.max, static_cast<size_t>(size));
                sizeInfo.sum += size;
            };

            if (state.moduleIndex >= static_cast<int>(moduleCount))
            {
                assert(!state.curBlockFrame);

                // All modules have been processed and the event can be flushed.

                // Transform the offset based ModuleReadoutSpans into pointer
                // based ModuleData structures, then invoke the eventData()
                // callback.
                for (unsigned mi = 0; mi < moduleCount; ++mi)
                {
                    const auto &moduleSpans = state.readoutDataSpans[mi];
                    auto &moduleData = state.moduleDataBuffer[mi];

                    moduleData.data =
                    {
                        state.workBuffer.buffer.data() + moduleSpans.dataSpan.offset,
                        moduleSpans.dataSpan.size
                    };

                    const auto partIndex = std::make_pair(state.eventIndex, mi);

                    if (moduleSpans.dataSpan.size)
                    {
                        ++counters.groupHits[partIndex];
                        update_part_size_info(
                            counters.groupSizes[partIndex], moduleSpans.dataSpan.size);
                    }
                }

                int crateIndex = 0;
                callbacks.eventData(state.userContext, crateIndex, state.eventIndex, state.moduleDataBuffer.data(), moduleCount);

                ++counters.eventHits[state.eventIndex];

                logger->trace("parser_clear_event_state because event is done, eventIndex={}",
                          state.eventIndex);

                parser_clear_event_state(state);
            }

            if (input.data() == lastIterPosition)
                return ParseResult::ParseReadoutContentsNotAdvancing;
        }

        return ParseResult::Ok;
    }
    catch (const end_of_buffer &e)
    {
        logger->debug("caught end_of_buffer: {}", e.what());
        log_buffer(logger, spdlog::level::trace, originalInputView, "originalInputView");
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
    auto logger = get_logger("readout_parser");

    if (input.size() < eth::HeaderWords)
        throw end_of_buffer("ETH header words");

    eth::PayloadHeaderInfo ethHdrs{ input[0], input[1] };

    logger->trace("begin parsing packet {}, dataWords={}, packetLen={} bytes",
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
            logger->debug("skipped {} words ({} bytes) of eth packet data to jump to the next header",
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

            logger->trace("end parsing packet {}, dataWords={}",
                      ethHdrs.packetNumber(), ethHdrs.dataWordCount());

            if (input.data() == lastInputPosition)
                return ParseResult::ParseEthPacketNotAdvancing;
        }
    }
    catch (const std::exception &e)
    {
        logger->debug("end parsing packet {}, dataWords={}, exception={}",
                  ethHdrs.packetNumber(), ethHdrs.dataWordCount(),
                  e.what());
        throw;
    }
    catch (...)
    {
        logger->debug("end_parsing packet {}, dataWords={}, caught an unknown exception!",
                  ethHdrs.packetNumber(), ethHdrs.dataWordCount());
        throw;
    }

    return {};
}

ParseResult parse_readout_buffer(
    ConnectionType bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    auto logger = get_logger("readout_parser");

    logger->trace("begin: bufferNumber={}, buffer={}, bufferWords={}",
              bufferNumber, reinterpret_cast<const void *>(buffer), bufferWords);
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
        return ParseResult::UnexpectedEndOfBuffer;
    }
    catch (...)
    {
        return ParseResult::UnhandledException;
    }

    logger->trace("end: bufferNumber={}, buffer={}, bufferWords={}, result={}",
              bufferNumber, reinterpret_cast<const void *>(buffer),
              bufferWords, get_parse_result_name(result));

    return result;
}

ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    const size_t bufferBytes = bufferWords * sizeof(u32);

    auto logger = get_logger("readout_parser");

    logger->trace("begin parsing ETH buffer {}, size={} bytes", bufferNumber, bufferBytes);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far is discarded.
        parser_clear_event_state(state);
        counters.internalBufferLoss += bufferLoss;
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

            if (try_handle_system_event(state, callbacks, counters, input))
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
                    counters.ethPacketLoss += loss;
                    logger->debug("packet loss detected: lastPacketNumber={}, packetNumber={}, loss={}",
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
                pr = parse_eth_packet(state, callbacks, counters, packetInput, bufferNumber);
                count_parse_result(counters, pr);
            }
            catch (const std::exception &e)
            {
                logger->warn("exception from parse_eth_packet(): {}, skipping packet",
                             e.what());
                exceptionSeen = true;
            }
            catch (...)
            {
                logger->warn("unknown exception from parse_eth_packet(), skipping packet");
                exceptionSeen = true;
                // XXX: Rethrowing here makes the parser abort parsing of the
                // rest of the buffer. If the throw is commented out the
                // exception is just counted via exceptionSeen, the packet is
                // skipped and parsing continues with the next packet.
                //throw;

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
                ++counters.ethPacketsProcessed;
                counters.unusedBytes += packetWords * sizeof(u32);

                if (exceptionSeen)
                    ++counters.parserExceptions;

                if (input.size() >= packetWords)
                {
                    input.remove_prefix(packetWords);

                    logger->debug("skipping {} words of eth packet data due to an error result from the parser",
                             packetWords);
                }

                continue;
            }

            ++counters.ethPacketsProcessed;

            logger->trace("parse_packet result: {}", (int)pr);

            // Skip over the packet ending up either on the next SystemEvent
            // frame or on the next packets header0.
            input.remove_prefix(packetWords);

            if (input.data() == lastInputPosition)
                return ParseResult::ParseEthBufferNotAdvancing;
        }
    }
    catch (const std::exception &e)
    {
        logger->warn("end parsing ETH buffer {}, size={} bytes, exception={}",
                 bufferNumber, bufferBytes, e.what());

        parser_clear_event_state(state);
        counters.unusedBytes += input.size() * sizeof(u32);
        ++counters.parserExceptions;
        throw;
    }
    catch (...)
    {
        logger->warn("end parsing ETH buffer {}, size={} bytes, unknown exception",
                 bufferNumber, bufferBytes);

        parser_clear_event_state(state);
        counters.unusedBytes += input.size() * sizeof(u32);
        ++counters.parserExceptions;
        throw;
    }

    ++counters.buffersProcessed;
    auto unusedBytes = input.size() * sizeof(u32);
    counters.unusedBytes += unusedBytes;

    logger->trace("end parsing ETH buffer {}, size={} bytes, unused bytes={}",
              bufferNumber, bufferBytes, unusedBytes);

    return {};
}

ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    const size_t bufferBytes = bufferWords * sizeof(u32);

    auto logger = get_logger("readout_parser");

    logger->trace("begin parsing USB buffer {}, size={} bytes", bufferNumber, bufferBytes);

    s64 bufferLoss = calc_buffer_loss(bufferNumber, state.lastBufferNumber);
    state.lastBufferNumber = bufferNumber;

    if (bufferLoss != 0)
    {
        // Clear processing state/workBuffer, restart at next 0xF3.
        // Any output data prepared so far will be discarded.
        parser_clear_event_state(state);
        counters.internalBufferLoss += bufferLoss;
    }

    basic_string_view<u32> input(buffer, bufferWords);

    try
    {
        while (!input.empty())
        {
            auto pr = parse_readout_contents(state, callbacks, counters, input, false, bufferNumber);
            count_parse_result(counters, pr);

            if (pr != ParseResult::Ok)
            {
                parser_clear_event_state(state);
                counters.unusedBytes += input.size() * sizeof(u32);
                return pr;
            }
        }
    }
    catch (const std::exception &e)
    {
        logger->warn("end parsing USB buffer {}, size={} bytes, exception={}",
                 bufferNumber, bufferBytes, e.what());

        parser_clear_event_state(state);
        counters.unusedBytes += input.size() * sizeof(u32);
        ++counters.parserExceptions;
        throw;
    }
    catch (...)
    {
        logger->warn("end parsing USB buffer {}, size={} bytes, unknown exception",
                 bufferNumber, bufferBytes);

        parser_clear_event_state(state);
        counters.unusedBytes += input.size() * sizeof(u32);
        ++counters.parserExceptions;
        throw;
    }

    ++counters.buffersProcessed;
    counters.unusedBytes += input.size() * sizeof(u32);
    logger->trace("end parsing USB buffer {}, size={} bytes", bufferNumber, bufferBytes);

    return {};
}

} // end namespace readout_parser
} // end namespace mesytec
} // end namespace mvlc
