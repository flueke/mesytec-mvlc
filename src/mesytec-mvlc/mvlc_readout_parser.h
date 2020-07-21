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
#ifndef __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__
#define __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__

#include <functional>
#include <limits>
#include <unordered_map>

#include "mesytec-mvlc_export.h"
#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_util.h"
#include "util/protected.h"

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

// Purpose: The reaodut_parser system is used to parse a possibly lossfull
// sequence of MVLC readout buffers into complete readout event data
// and make this data available to a consumer.
//
// StackCommands that produce output:
//   marker         -> one word
//   single_read    -> one word
//   block_read     -> dynamic part (0xF5 framed)
//
// Restrictions applying to the structure of each stack command group:
//   - an optional fixed size prefix part (single value read and marker commands)
//   - an optional dynamic block part (a single block read command)
//   - an optional fixed size suffix part (single value read and marker commands)


struct GroupReadoutStructure
{
    u8 prefixLen; // length in 32 bit words of the fixed part prefix
    u8 suffixLen; // length in 32 bit words of the fixed part suffix
    bool hasDynamic; // true if a dynamic part (block read) is present
};

inline bool is_empty(const GroupReadoutStructure &mrp)
{
    return mrp.prefixLen == 0 && mrp.suffixLen == 0 && !mrp.hasDynamic;
}

struct Span
{
    u32 offset;
    u32 size;
};

struct GroupReadoutSpans
{
    Span prefixSpan;
    Span dynamicSpan;
    Span suffixSpan;
};

inline bool is_empty(const GroupReadoutSpans &spans)
{
    return (spans.prefixSpan.size == 0
            && spans.dynamicSpan.size == 0
            && spans.suffixSpan.size == 0);
}

struct end_of_frame: public std::exception {};

struct ReadoutParserCallbacks
{
    // functions taking an event index
    std::function<void (int eventIndex)>
        beginEvent = [] (int) {},
        endEvent   = [] (int) {};

    // Parameters: event index, group (aka module) index, pointer to first data word, number of words
    std::function<void (int eventIndex, int groupIndex, const u32 *data, u32 size)>
        groupPrefix  = [] (int, int, const u32*, u32) {},
        groupDynamic = [] (int, int, const u32*, u32) {},
        groupSuffix  = [] (int, int, const u32*, u32) {};

    // Parameters: pointer to first word of the system event data, number of words
    std::function<void (const u32 *header, u32 size)>
        systemEvent = [] (const u32 *, u32) {};
};

enum class ParseResult
{
    Ok,
    NoHeaderPresent,
    NoStackFrameFound,

    NotAStackFrame,
    NotABlockFrame,
    NotAStackContinuation,
    StackIndexChanged,
    StackIndexOutOfRange,
    GroupIndexOutOfRange,
    EmptyStackFrame,
    UnexpectedOpenBlockFrame,

    // IMPORTANT: These should not happen and be fixed in the code if they
    // happen. They indicate that the parser algorithm did not advance through
    // the buffer but is stuck in place, parsing the same data again.
    ParseReadoutContentsNotAdvancing,
    ParseEthBufferNotAdvancing,
    ParseEthPacketNotAdvancing,
    UnexpectedEndOfBuffer,
    UnhandledException,

    ParseResultMax
};

MESYTEC_MVLC_EXPORT const char *get_parse_result_name(const ParseResult &pr);

// Helper enabling the use of std::pair as the key in std::unordered_map.
struct PairHash
{
    template <typename T1, typename T2>
        std::size_t operator() (const std::pair<T1, T2> &pair) const
        {
            return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
        }
};

struct MESYTEC_MVLC_EXPORT ReadoutParserCounters
{
    // Counts internal buffer loss across calls to parse_readout_buffer()
    u32 internalBufferLoss = 0;

    // Total number of buffers processes so far.
    u32 buffersProcessed = 0;

    // Number of bytes skipped by the parser. This can happen due to internal
    // buffer loss, ethernet packet loss or unexpected/corrupted incoming data.
    u64 unusedBytes = 0;

    // Ethernet specific packet and loss counters.
    u32 ethPacketsProcessed = 0;
    u32 ethPacketLoss = 0;

    // Counts the number of system events seen per system event type.
    using SystemEventCounts = std::array<u32, system_event::subtype::SubtypeMax + 1>;

    SystemEventCounts systemEvents;

    // The count of each ParseResult returned by the parser.
    using ParseResultCounts = std::array<u32, static_cast<size_t>(ParseResult::ParseResultMax)>;

    ParseResultCounts parseResults;

    // Number of exceptions thrown by the parser.
    u32 parserExceptions = 0;

    // Number of stack frames with length zero. This should not happen but the
    // MVLC sometimes generates them.
    u32 emptyStackFrames = 0;

    struct PartSizeInfo
    {
        size_t min = std::numeric_limits<size_t>::max();
        size_t max = 0u;
        size_t sum = 0u;
    };

    using GroupPartHits = std::unordered_map<std::pair<int, int>, size_t, PairHash>;
    using GroupPartSizes = std::unordered_map<std::pair<int, int>, PartSizeInfo, PairHash>;

    // Event hit counts by eventIndex
    std::unordered_map<int, size_t> eventHits;

    // Part specific hit counts by (eventIndex, groupIndex)
    GroupPartHits groupPrefixHits;
    GroupPartHits groupDynamicHits;
    GroupPartHits groupSuffixHits;

    // Part specific event size information by (eventIndex, groupIndex)
    GroupPartSizes groupPrefixSizes;
    GroupPartSizes groupDynamicSizes;
    GroupPartSizes groupSuffixSizes;
};

struct MESYTEC_MVLC_EXPORT ReadoutParserState
{
    // Helper structure keeping track of the number of words left in a MVLC
    // style data frame.
    struct FrameParseState
    {
        FrameParseState(u32 frameHeader = 0)
            : header(frameHeader)
            , wordsLeft(extract_frame_info(frameHeader).len)
        {}

        inline explicit operator bool() const { return wordsLeft; }
        inline FrameInfo info() const { return extract_frame_info(header); }

        inline void consumeWord()
        {
            if (wordsLeft == 0)
                throw end_of_frame();
            --wordsLeft;
        }

        inline void consumeWords(size_t count)
        {
            if (wordsLeft < count)
                throw end_of_frame();

            wordsLeft -= count;
        }

        u32 header;
        u16 wordsLeft;
    };

    enum GroupParseState { Prefix, Dynamic, Suffix };

    using ReadoutStructure = std::vector<std::vector<GroupReadoutStructure>>;

    struct WorkBuffer
    {
        std::vector<u32> buffer;
        size_t used = 0;

        inline size_t free() const { return buffer.size() - used; }
    };

    // The readout workers start with buffer number 1 so buffer 0 can only
    // occur after wrapping the counter. By using 0 as a starting value the
    // buffer loss calculation will work without special cases.
    u32 lastBufferNumber = 0;

    // Space to assemble linear readout data.
    WorkBuffer workBuffer;

    // Per module offsets and sizes into the workbuffer. This is a map of the
    // current layout of the workbuffer.
    std::vector<GroupReadoutSpans> readoutDataSpans;

    // Per event preparsed group/module readout info.
    ReadoutStructure readoutStructure;

    int eventIndex = -1;
    int groupIndex = -1;
    GroupParseState groupParseState = Prefix;

    // Parsing state of the current 0xF3 stack frame. This is always active
    // when parsing readout data.
    FrameParseState curStackFrame = {};

    // Parsing state of the current 0xF5 block readout frame. This is only
    // active when parsing the dynamic part of a module readout.
    FrameParseState curBlockFrame = {};

    // ETH parsing only. The transmitted packet number type is u16. Using an
    // s32 here to represent the "no previous packet" case by storing a -1.
    s32 lastPacketNumber = -1;
};

// Create a readout parser from a list of readout stack defintions.
//
// This function assumes that the first element in the vector contains the
// definition for the readout stack with id 1, the second the one for stack id
// 2 and so on. Stack 0 (the direct exec stack) must not be included.
MESYTEC_MVLC_EXPORT ReadoutParserState make_readout_parser(
    const std::vector<StackCommandBuilder> &readoutStacks);

// Functions for steering the parser. These should be called repeatedly with
// complete MVLC readout buffers. The input buffer sequence may be lossfull
// which is useful when snooping parts of the data during a DAQ run.

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer(
    ConnectionType bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    Protected<ReadoutParserCounters> &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__ */
