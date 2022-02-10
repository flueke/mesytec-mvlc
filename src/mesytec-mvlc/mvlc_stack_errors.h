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
#ifndef __MESYTEC_MVLC_MVLC_STACK_ERRORS_H__
#define __MESYTEC_MVLC_MVLC_STACK_ERRORS_H__

#include <array>
#include <cassert>
#include <unordered_map>
#include "mvlc_constants.h"
#include "mvlc_util.h"

namespace mesytec
{
namespace mvlc
{

struct StackErrorInfo
{
    u16 line; // the number of the stack line that caused the error
    u8 flags; // frame_flags from mvlc_constants
};

inline bool operator==(const StackErrorInfo &a, const StackErrorInfo &b)
{
    return a.line == b.line && a.flags == b.flags;
}

} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct hash<mesytec::mvlc::StackErrorInfo>
    {
        std::size_t operator()(const mesytec::mvlc::StackErrorInfo &ei) const noexcept
        {
            auto h1 = std::hash<mesytec::mvlc::u16>{}(ei.line);
            auto h2 = std::hash<mesytec::mvlc::u8>{}(ei.flags);
            return h1 ^ (h2 << 1);
        }
    };
} // end namespace std

namespace mesytec
{
namespace mvlc
{

// Records the number of errors for each distinct combination of
// (error_line, error_flags).
using ErrorInfoCounts = std::unordered_map<StackErrorInfo, size_t>;

struct StackErrorCounters
{
    std::array<ErrorInfoCounts, stacks::StackCount> stackErrors;
    size_t nonErrorFrames = 0u;
    std::unordered_map<u32, size_t> nonErrorHeaderCounts; // headerValue -> count
};

// This function expects C to be a container of u32 values holding a single
// mvlc stack error frame.
template<typename C>
void update_stack_error_counters(StackErrorCounters &counters, const C &errorFrame)
{
    assert(errorFrame.size() > 0);

    bool isErrorFrame = false;
    FrameInfo frameInfo = {};

    // Error frames consist of the frame header and a second word containing
    // the stack number and the stack line number where the error occured.
    if (errorFrame.size() == 2)
    {
        frameInfo = extract_frame_info(errorFrame[0]);

        if (frameInfo.type == frame_headers::StackError
            && frameInfo.stack < stacks::StackCount)
        {
            isErrorFrame = true;
        }
    }

    if (isErrorFrame)
    {
        assert(errorFrame.size() == 2);
        u16 stackLine = errorFrame[1] & stack_error_info::StackLineMask;
        StackErrorInfo ei = { stackLine, frameInfo.flags };
        ++counters.stackErrors[frameInfo.stack][ei];
    }
    else if (errorFrame.size() > 0)
    {
        ++counters.nonErrorFrames;
        ++counters.nonErrorHeaderCounts[errorFrame[0]];
    }
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_STACK_ERRORS_H__ */
