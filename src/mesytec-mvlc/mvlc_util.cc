/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (C) 2020-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include <iostream>
#include <sstream>

#include "mvlc_constants.h"
#include "util/fmt.h"
#include "util/io_util.h"
#include "util/logging.h"
#include "util/string_util.h"
#include "util/string_view.hpp"
#include "mvlc_buffer_validators.h"
#include "mvlc_eth_interface.h"

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
    std::ostringstream ss;

    auto headerInfo = extract_frame_info(header);

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::SuperFrame:
            ss << "SuperFrame (len=" << headerInfo.len;
            break;

        case frame_headers::SuperContinuation:
            ss << "SuperFrameContinuation (len=" << headerInfo.len;
            break;

        case frame_headers::StackFrame:
            ss << "StackResultFrame (len=" << headerInfo.len;
            break;

        case frame_headers::BlockRead:
            ss << "BlockReadFrame (len=" << headerInfo.len;
            break;

        case frame_headers::StackError:
            ss << "StackErrorFrame (len=" << headerInfo.len;
            break;

        case frame_headers::StackContinuation:
            ss << "StackResultContinuation Frame (len=" << headerInfo.len;
            break;

        case frame_headers::SystemEvent:
            ss << "SystemEvent (len=" << headerInfo.len;
            break;

        case frame_headers::SystemEvent2:
            ss << "SystemEvent2 (len=" << headerInfo.len;
            break;

        default:
            ss << "<unknown frame type>";
            break;
    }

    switch (static_cast<frame_headers::FrameTypes>(headerInfo.type))
    {
        case frame_headers::SuperFrame:
        case frame_headers::SuperContinuation:
            {
                u16 ctrlId = (header >> frame_headers::CtrlIdShift) & frame_headers::CtrlIdMask;
                ss << ", ctrlId=" << ctrlId;
            }
            break;

        case frame_headers::StackFrame:
        case frame_headers::StackError:
        case frame_headers::StackContinuation:
            {
                u16 stackNum = (header >> frame_headers::StackNumShift) & frame_headers::StackNumMask;
                ss << ", stackNum=" << stackNum;

                u16 ctrlId = (header >> frame_headers::CtrlIdShift) & frame_headers::CtrlIdMask;
                ss << ", ctrlId=" << ctrlId;
            }
            break;

        case frame_headers::SystemEvent:
        case frame_headers::SystemEvent2:
            {
                u16 subType = (header >> system_event::SubtypeShift) & system_event::SubtypeMask;
                u16 ctrlId = (header >> system_event::CtrlIdShift) & system_event::CtrlIdMask;

                ss << ", subType=" << subType << " (" << system_event_type_to_string(subType) << ")";
                ss << ", ctrlId=" << ctrlId;
            }
            break;

        case frame_headers::BlockRead:
            break;
    }

    if (is_known_frame_header(header))
    {
        if (static_cast<frame_headers::FrameTypes>(headerInfo.type) != frame_headers::SystemEvent)
        {
            u8 frameFlags = (header >> frame_headers::FrameFlagsShift) & frame_headers::FrameFlagsMask;
            ss << ", frameFlags=" << format_frame_flags(frameFlags) << ")";
        }
        else
        {
            if ((header >> system_event::ContinueShift) & system_event::ContinueMask)
                ss << ", frameFlags=Continue" << ")";
            else
                ss << ", frameFlags=none" << ")";
        }
    }

    return ss.str();
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
        case T::StackErrors:
            return "MVLCStackErrors";
        case T::EndOfFile:
            return "EndOfFile";
        default:
            break;
    }

    return fmt::format("custom (0x{:02x})", eventType);
}

std::string trigger_type_to_string(const u8 &tt)
{
    switch (tt)
    {
        case stacks::NoTrigger:     return "NoTrigger";
        case stacks::IRQWithIACK:   return "IrqWithIack";
        case stacks::IRQNoIACK:     return "IrqNoIack";
        case stacks::External:      return "TriggerIO";
    }

    return {};
}

std::string trigger_subtype_to_string(const u8 &st)
{
    using T = stacks::TriggerSubtype;

    switch (st)
    {
        case T::IRQ1      : return "IRQ1";
        case T::IRQ2      : return "IRQ2";
        case T::IRQ3      : return "IRQ3";
        case T::IRQ4      : return "IRQ4";
        case T::IRQ5      : return "IRQ5";
        case T::IRQ6      : return "IRQ6";
        case T::IRQ7      : return "IRQ7";
        case T::IRQ8      : return "IRQ8";
        case T::IRQ9      : return "IRQ9";
        case T::IRQ10     : return "IRQ10";
        case T::IRQ11     : return "IRQ11";
        case T::IRQ12     : return "IRQ12";
        case T::IRQ13     : return "IRQ13";
        case T::IRQ14     : return "IRQ14";
        case T::IRQ15     : return "IRQ15";
        case T::IRQ16     : return "IRQ16";
        case T::Slave0    : return "Slave0";
        case T::Slave1    : return "Slave1";
        case T::Slave2    : return "Slave2";
        case T::Slave3    : return "Slave3";
        case T::Timer0    : return "Timer0";
        case T::Timer1    : return "Timer1";
        case T::Timer2    : return "Timer2";
        case T::Timer3    : return "Timer3";
    }

    return {};
}

std::string trigger_to_string(const stacks::Trigger &trigger)
{
    auto result = fmt::format("type={}, subtype={}",
        trigger_type_to_string(trigger.type), trigger_subtype_to_string(trigger.subtype));

    if (trigger.immediate)
    {
        result += ", immediate=true";
    }

    return result;
}

std::optional<int> MESYTEC_MVLC_EXPORT get_trigger_irq_value(const stacks::Trigger &trigger)
{
    if (trigger.type == stacks::TriggerType::IRQNoIACK
        || trigger.type == stacks::TriggerType::IRQWithIACK)
    {
        return trigger.subtype + 1;
    }

    return {};
}

std::optional<int> MESYTEC_MVLC_EXPORT get_trigger_irq_value(const u16 triggerValue)
{
    return get_trigger_irq_value(stacks::Trigger{.value=triggerValue});
}

// Follows the framing structure inside the buffer until an incomplete frame
// which doesn't fit into the buffer is detected. The incomplete data is moved
// over to the tempBuffer so that the readBuffer ends with a complete frame.
//
// The input buffer must start with a frame header (skip_count will be called
// with the first word of the input buffer on the first iteration).
//
// The SkipCountFunc must return the number of bytes to skip to get to the next
// frame header or 0 if there is not enough data left in the input iterator to
// determine the frames size.
// Signature of SkipCountFunc:  u32 skip_count(const basic_string_view<const u8> &view);
//
// Returns the number of trailing bytes copied from msgBuf into tmpBuf.
template<typename SkipCountFunc>
size_t fixup_buffer(
    const u8 *msgBuf, size_t msgUsed,
    std::vector<u8> &tmpBuf,
    SkipCountFunc skip_count)
{
    using namespace nonstd;
    auto view = basic_string_view<const u8>(msgBuf, msgUsed);

    while (!view.empty())
    {
        if (view.size() >= sizeof(u32))
        {
            u32 wordsToSkip = skip_count(view) * sizeof(u32);

            //std::cout << "   wordsToSkip=" << wordsToSkip << ", view.size()=" << view.size() << ", in words:" << view.size() / sizeof(u32) << "\n";

            if (wordsToSkip == 0 || wordsToSkip > view.size() / sizeof(u32))
            {
                tmpBuf.reserve(tmpBuf.size() + view.size());
                std::copy(std::begin(view), std::end(view), std::back_inserter(tmpBuf));
                #if not defined(NDEBUG) and 0
                auto viewU32 = basic_string_view<const u32>(reinterpret_cast<const u32 *>(view.data()), view.size() / sizeof(u32));
                mvlc::util::log_buffer(std::cout, viewU32, "fixup_buffer: partial data left in view and moved to tmpBuf:");
                #endif
                return view.size();
            }

            // Skip over the SystemEvent frame or the ETH packet data.
            view.remove_prefix(wordsToSkip * sizeof(u32));
        }
    }

    return 0u;
}

u32 skip_one_frame_usb(const std::basic_string_view<const u8> &view)
{
    if (view.size() < sizeof(u32))
        return 0u;

    u32 header = *reinterpret_cast<const u32 *>(view.data());
    spdlog::trace("skip_one_frame_usb: header=0x{:08x}, frameInfo={}", header, decode_frame_header(header));
    u32 result = 1u + extract_frame_info(header).len;
    return result;
}

u32 skip_one_frame_eth(const std::basic_string_view<const u8> &view)
{
    if (view.size() < sizeof(u32))
        return 0u;

    // Either a SystemEvent header or the first of the two ETH packet headers
    u32 header = *reinterpret_cast<const u32 *>(view.data());

    if (get_frame_type(header) == frame_headers::SystemEvent)
    {
        spdlog::trace("skip_one_frame_eth: SystemEvent header=0x{:08x}, frameInfo={}", header, decode_frame_header(header));
        return 1u + extract_frame_info(header).len;
    }

    if (view.size() >= 2 * sizeof(u32))
    {
        u32 header1 = *reinterpret_cast<const u32 *>(view.data() + sizeof(u32));
        eth::PayloadHeaderInfo ethHdrs{ header, header1 };
        spdlog::trace("skip_one_frame_eth: ethHdrs: packetChannel={}, packetNumber={}, crateId={}, dataWordCount={}, nextHeaderPointer=0x{:04x}",
            ethHdrs.packetChannel(), ethHdrs.packetNumber(), ethHdrs.controllerId(), ethHdrs.dataWordCount(), ethHdrs.nextHeaderPointer());
        return eth::HeaderWords + ethHdrs.dataWordCount();
    }

    // Not enough data to get the 2nd ETH header word.
    return 0u;
}

size_t fixup_buffer_mvlc_usb(const u8 *buf, size_t bufUsed, std::vector<u8> &tmpBuf)
{
    return fixup_buffer(buf, bufUsed, tmpBuf, skip_one_frame_usb);
}

size_t fixup_buffer_mvlc_eth(const u8 *buf, size_t bufUsed, std::vector<u8> &tmpBuf)
{
    return fixup_buffer(buf, bufUsed, tmpBuf, skip_one_frame_eth);
}

std::optional<ConnectionType> detect_buffer_type(const u8 *buffer, size_t bufferBytes)
{
    return detect_buffer_type(
        reinterpret_cast<const u32 *>(buffer),
        bufferBytes / sizeof(u32));
}

std::optional<ConnectionType> detect_buffer_type(const u32 *buffer, size_t bufferWords)
{
    if (bufferWords == 0)
        return std::nullopt;

    auto view = std::basic_string_view<u32>(buffer, bufferWords);

    // Check if it's the first of the two eth header words. The 0b00 prefix does
    // not collide with anything else the MVLC or our software emits.
    if (is_eth_header0(view[0]))
        return ConnectionType::ETH;

    // It's a known header but not a software generated system event => USB format.
    if (is_known_frame_header(view[0]) && !is_system_event(view[0]))
        return ConnectionType::USB;

    // Still ambiguous: system event frames are part of the listfile/streaming
    // format and appear alongside ETH packets and readout frames in the data stream.
    // Walk the buffer until we find a non-system-event frame header or eth header0.
    while (!view.empty())
    {
        if (is_system_event(view[0]))
        {
            auto frameInfo = extract_frame_info(view[0]);
            if (view.size() < frameInfo.len + 1u)
                return std::nullopt;
            view.remove_prefix(frameInfo.len + 1);
        }
        else if (is_eth_header0(view[0]))
        {
            return ConnectionType::ETH;
        }
        else if (is_known_frame_header(view[0]))
        {
            return ConnectionType::USB;
        }
        else
        {
            break;
        }
    }

    return std::nullopt;
}

size_t calculate_complete_frames_bytes(ConnectionType bufferType, const u8 *buf, size_t bufUsed)
{
    auto view = std::basic_string_view<const u8>(buf, bufUsed);
    size_t totalSize = 0;

    spdlog::trace("calculate_complete_frames_bytes: bufferType={}, bufUsed={} bytes ({} words)",
                  (bufferType == ConnectionType::ETH) ? "ETH" : "USB", bufUsed,
                  bufUsed / sizeof(u32));

    while (!view.empty())
    {
        u32 bytesToSkip = 0;

        if (bufferType == ConnectionType::USB)
            bytesToSkip = skip_one_frame_usb(view) * sizeof(u32);
        else
            bytesToSkip = skip_one_frame_eth(view) * sizeof(u32);

        if (bytesToSkip == 0 || bytesToSkip > view.size())
            break;

        totalSize += bytesToSkip;
        view.remove_prefix(bytesToSkip);
    }

    spdlog::trace("calculate_complete_frames_bytes: frameSize={} bytes ({} words), leftover={} bytes ({} words)", totalSize,
                  totalSize / sizeof(u32), view.size(), view.size() / sizeof(u32));

    return totalSize;
}

size_t calculate_complete_frames_bytes(ConnectionType bufferType, std::basic_string_view<const u32> view)
{
    return calculate_complete_frames_bytes(bufferType, reinterpret_cast<const u8 *>(view.data()), view.size() * sizeof(u32));
}

bool is_super_command(u32 dataWord)
{
    using namespace super_commands;
    using SuperCT = SuperCommandType;

    u16 v = (dataWord >> SuperCmdShift) & SuperCmdMask;

    return (v == static_cast<u16>(SuperCT::CmdBufferStart)
            || v == static_cast<u16>(SuperCT::CmdBufferEnd)
            || v == static_cast<u16>(SuperCT::ReferenceWord)
            || v == static_cast<u16>(SuperCT::ReadLocal)
            || v == static_cast<u16>(SuperCT::ReadLocalBlock)
            || v == static_cast<u16>(SuperCT::WriteLocal)
            || v == static_cast<u16>(SuperCT::WriteReset));
}

bool is_stack_command(u32 dataWord)
{
    using namespace stack_commands;
    using StackCT = StackCommandType;

    u8 v = (dataWord >> CmdShift) & CmdMask;

    return (v == static_cast<u8>(StackCT::StackStart)
            || v == static_cast<u8>(StackCT::StackEnd)
            || v == static_cast<u8>(StackCT::VMEWrite)
            || v == static_cast<u8>(StackCT::VMERead)
            || v == static_cast<u8>(StackCT::VMEReadSwapped)
            || v == static_cast<u8>(StackCT::VMEReadMem)
            || v == static_cast<u8>(StackCT::VMEReadMemSwapped)
            || v == static_cast<u8>(StackCT::WriteMarker)
            || v == static_cast<u8>(StackCT::WriteSpecial)
            || v == static_cast<u8>(StackCT::Wait)
            || v == static_cast<u8>(StackCT::SignalAccu)
            || v == static_cast<u8>(StackCT::MaskShiftAccu)
            || v == static_cast<u8>(StackCT::SetAccu)
            || v == static_cast<u8>(StackCT::ReadToAccu)
            || v == static_cast<u8>(StackCT::CompareLoopAccu)
            );
}

} // end namespace mvlc
} // end namespace mesytec
