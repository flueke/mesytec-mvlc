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

#include <array>
#include <functional>
#include <limits>
#include <unordered_map>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/mvlc_util.h"
#include "mesytec-mvlc/readout_buffer.h"

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

// Purpose: The reaodut_parser system is used to parse a possibly lossfull
// sequence of MVLC readout buffers into complete readout event data and make
// this data available to a consumer via callbacks.
//
// MVLC StackCommands that produce output:
//   marker                         -> one word
//   single_read                    -> one word
//   block_read                     -> dynamic part (0xF5 framed)
//   single_read when accu is used  -> dynamic part (0xF5 framed)
//
// Restrictions applying to the structure of each stack command group:
//   - an optional fixed size prefix part (single value read and marker commands)
//   - an optional dynamic block part (a single block read command)
//   - an optional fixed size suffix part (single value read and marker commands)
//
// A stack group is typically used to read out a single VME module, so groups
// are synonymous with VME modules in the parser code.
//
// Note: the {prefix, dynamic, suffix} structure is not present in the output
// callbacks anymore. Instead the parsed module/group data is presented as a
// linear buffer (DataBlock structure).
//
// The restrictions on the readout structure are in place to keep the
// readout_parser code simple and to make it easy to pass around readout data
// (pointer + size is enough). If more complex readout schemes are needed one
// can add more readout groups to achieve the desired effect, e.g. one group
// for a fixed prefix, one group for the dynamic part and another group for the
// suffix.

struct DataBlock
{
    const u32 *data;
    u32 size;
};

struct ModuleData
{
    // TODO: maybe add these to carry source information for the module event.
    //u64 bufferNumber;
    //u64 eventNumber;
    DataBlock data;
};

// Callbacks invoked by the parser once a full event has been parsed and assembled.
struct ReadoutParserCallbacks
{
    // Parameters: index of the VME event the data belongs to, pointer to an
    // array of ModuleData allowing access to the actual module readout data and
    // the moduleCount, specifying the number of elements in the groups array.

    // Invoked for each readout event that was fully parsed.
    // Parameters are:
    // - userContext as passed to make_readout_parser()
    // - crateIndex as passed to make_readout_parser(), starts from 0
    // - eventIndex starting from 0
    // - moduleDataList: pointer to ModuleData structures
    // - number of ModuleData structures stored in moduleDataList

    std::function<void (void *userContext, int crateIndex, int eventIndex,
                        const ModuleData *moduleDataList, unsigned moduleCount)>
        eventData = [] (void *, int, int, const ModuleData *, unsigned) {};

    // Parameters: pointer to the system event header, number of words in the
    // system event


    // Invoked for each system event that was parsed.
    // Parameters are:
    // - userContext as passed to make_readout_parser()
    // - crateIndex as passed to make_readout_parser(), starts from 0
    // - header: pointer to the system_event header
    // - number of words in the system event
    std::function<void (void *userContext, int crateIndex, const u32 *header, u32 size)>
        systemEvent = [] (void *, int, const u32 *, u32) {};
};

struct ModuleReadoutStructure
{
    u8 prefixLen; // length in 32 bit words of the fixed part prefix
    u8 suffixLen; // length in 32 bit words of the fixed part suffix
    bool hasDynamic; // true if a dynamic part (block read) is present
};

inline bool is_empty(const ModuleReadoutStructure &mrs)
{
    return mrs.prefixLen == 0 && mrs.suffixLen == 0 && !mrs.hasDynamic;
}

struct Span
{
    u32 offset;
    u32 size;
};

struct ModuleReadoutSpans
{
    Span prefixSpan;
    Span dynamicSpan;
    Span suffixSpan;
};

inline bool is_empty(const ModuleReadoutSpans &spans)
{
    return (spans.prefixSpan.size == 0
            && spans.dynamicSpan.size == 0
            && spans.suffixSpan.size == 0);
}

struct end_of_frame: public std::exception {};

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
    UnknownBufferType,

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
    ReadoutParserCounters()
    {
        systemEvents.fill(0);
        parseResults.fill(0);
    }

    // Counts internal buffer loss across calls to parse_readout_buffer()
    u32 internalBufferLoss = 0;

    // Total number of buffers processed so far.
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

    // Part specific hit counts by (eventIndex, moduleIndex)
    GroupPartHits groupHits;

    // Part specific event size information by (eventIndex, moduleIndex)
    GroupPartSizes groupSizes;
};

struct MESYTEC_MVLC_EXPORT ReadoutParserState
{
    // Helper structure keeping track of the number of words left in a MVLC
    // style data frame.
    struct FrameParseState
    {
        explicit FrameParseState(u32 frameHeader = 0)
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

    //enum GroupParseState { Initial, Fixed, Dynamic };
    enum GroupParseState { Prefix, Dynamic, Suffix };

    using ReadoutStructure = std::vector<std::vector<ModuleReadoutStructure>>;

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
    std::vector<ModuleReadoutSpans> readoutDataSpans;

    // Same information as in readoutDataSpans but this time using pointers
    // into the current workBuffer instead of offsets. Used to invoke the
    // eventData callback.
    std::vector<ModuleData> moduleDataBuffer;

    // Per event preparsed group/module readout info.
    ReadoutStructure readoutStructure;

    int eventIndex = -1;
    int moduleIndex = -1;
    GroupParseState groupParseState = Prefix;

    // Parsing state of the current 0xF3 stack frame. This is always active
    // when parsing readout data.
    FrameParseState curStackFrame{};

    // Parsing state of the current 0xF5 block readout frame. This is only
    // active when parsing the dynamic part of a module readout.
    FrameParseState curBlockFrame{};

    // ETH parsing only. The transmitted packet number type is u16. Using an
    // s32 here to represent the "no previous packet" case by storing a -1.
    s32 lastPacketNumber = -1;

    std::exception_ptr eptr;

    int crateIndex = 0;
    void *userContext = nullptr;
};

// Create a readout parser from a list of readout stack defintions.
//
// This function assumes that the first element in the vector contains the
// definition for the readout stack with id 1, the second the one for stack id
// 2 and so on. Stack 0 (the direct exec stack) must not be included.
MESYTEC_MVLC_EXPORT ReadoutParserState make_readout_parser(
    const std::vector<StackCommandBuilder> &readoutStacks,
    int crateIndex = 0,
    void *userContext = nullptr);

// Functions for steering the parser. These should be called repeatedly with
// complete MVLC readout buffers. The input buffer sequence may be lossfull
// which is useful when snooping parts of the data during a DAQ run.

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer(
    ConnectionType bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

inline ParseResult parse_readout_buffer(
    s32 bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords)
{
    return parse_readout_buffer(
        static_cast<ConnectionType>(bufferType),
        state, callbacks, counters, bufferNumber, buffer, bufferWords);
}

inline ParseResult parse_readout_buffer(
    const ReadoutBuffer &buffer,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters)
{
    auto bufferView = buffer.viewU32();
    return parse_readout_buffer(
        buffer.type(), state, callbacks, counters,
        buffer.bufferNumber(), bufferView.data(), bufferView.size());
}

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    ReadoutParserCounters &counters,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ReadoutParserState::ReadoutStructure build_readout_structure(
    const std::vector<StackCommandBuilder> &readoutStacks);

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__ */
