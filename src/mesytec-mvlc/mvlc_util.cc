/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc_util.h"

#include <cassert>
#include <fmt/format.h>
#include <iostream>
#include <sstream>

#include "mvlc_constants.h"
#include "util/string_util.h"

using namespace mesytec::mvlc;

namespace mesytec
{
namespace mvlc
{

std::string format_frame_flags(u8 frameFlags)
{
    if (!frameFlags)
        return "none";

    std::vector<std::string> buffer;

    if (frameFlags & frame_flags::Continue)
        buffer.emplace_back("continue");

    if (frameFlags & frame_flags::SyntaxError)
        buffer.emplace_back("syntax");

    if (frameFlags & frame_flags::BusError)
        buffer.emplace_back("BERR");

    if (frameFlags & frame_flags::Timeout)
        buffer.emplace_back("timeout");

    return util::join(buffer, ", ");
}

std::string decode_frame_header(u32 header)
{
    std::string result;
    std::ostringstream ss(result);

    auto headerInfo = extract_frame_info(header);

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::SuperFrame:
            ss << "Super Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackFrame:
            ss << "Stack Result Frame (len=" << headerInfo.len;
            break;

        case frame_headers::BlockRead:
            ss << "Block Read Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackError:
            ss << "Stack Error Frame (len=" << headerInfo.len;
            break;

        case frame_headers::StackContinuation:
            ss << "Stack Result Continuation Frame (len=" << headerInfo.len;
            break;

        case frame_headers::SystemEvent:
            ss << "System Event (len=" << headerInfo.len;
            break;

        case frame_headers::DSOBuffer:
            ss << "DSO Buffer";
            break;
    }

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::StackFrame:
        case frame_headers::BlockRead:
        case frame_headers::StackError:
        case frame_headers::StackContinuation:
            {
                u16 stackNum = (header >> frame_headers::StackNumShift) & frame_headers::StackNumMask;
                ss << ", stackNum=" << stackNum;
            }
            break;

        case frame_headers::SuperFrame:
        case frame_headers::SystemEvent:
        case frame_headers::DSOBuffer:
            break;
    }

    u8 frameFlags = (header >> frame_headers::FrameFlagsShift) & frame_headers::FrameFlagsMask;

    ss << ", frameFlags=" << format_frame_flags(frameFlags) << ")";

    return result;
}

const char *get_frame_flag_shift_name(u8 flag_shift)
{
    if (flag_shift == frame_flags::shifts::Timeout)
        return "Timeout";

    if (flag_shift == frame_flags::shifts::BusError)
        return "BusError";

    if (flag_shift == frame_flags::shifts::SyntaxError)
        return "SyntaxError";

    if (flag_shift == frame_flags::shifts::Continue)
        return "Continue";

    return "Unknown";
}

stacks::TimerBaseUnit timer_base_unit_from_string(const std::string &str_)
{
    auto str = util::str_tolower(str_);

    if (str == "ns")
        return stacks::TimerBaseUnit::ns;

    if (str == "us" || str == "µs")
        return stacks::TimerBaseUnit::us;

    if (str == "ms")
        return stacks::TimerBaseUnit::ms;

    if (str == "s")
        return stacks::TimerBaseUnit::s;

    return {};
}

std::string system_event_type_to_string(u8 eventType)
{
    namespace T = system_event::subtype;

    switch (eventType)
    {
        case T::EndianMarker:
            return "EndianMarker";
        case T::BeginRun:
            return "BeginRun";
        case T::EndRun:
            return "EndRun";
        case T::MVMEConfig:
            return "MVMEConfig";
        case T::UnixTimetick:
            return "UnixTimetick";
        case T::Pause:
            return "Pause";
        case T::Resume:
            return "Resume";
        case T::MVLCCrateConfig:
            return "MVLCCrateConfig";
        case T::EndOfFile:
            return "EndOfFile";
        default:
            break;
    }

    return fmt::format("unknown/custom (0x{:02x})", eventType);
}


} // end namespace mvlc
} // end namespace mesytec
