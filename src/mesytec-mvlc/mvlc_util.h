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
#ifndef __MESYTEC_MVLC_MVME_MVLC_UTIL_H__
#define __MESYTEC_MVLC_MVME_MVLC_UTIL_H__

#include <iomanip>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{

struct FrameInfo
{
    u16 len;
    u8 type;
    u8 flags;
    u8 stack;
    u8 ctrl;
    u8 sysEventSubType;
};

inline FrameInfo extract_frame_info(u32 header)
{
    using namespace frame_headers;

    FrameInfo result = {};

    result.type  = (header >> TypeShift) & TypeMask;
    result.len   = (header >> LengthShift) & LengthMask;

    if (result.type == frame_headers::SystemEvent)
    {
        result.ctrl = (header >> system_event::CtrlIdShift) & system_event::CtrlIdMask;
        result.sysEventSubType = (header >> system_event::SubtypeShift) & system_event::SubtypeMask;
        result.flags = ((header >> system_event::ContinueShift) & system_event::ContinueMask) << frame_flags::shifts::Continue;
    }
    else
    {
        result.flags = (header >> FrameFlagsShift) & FrameFlagsMask;
        result.stack = (header >> StackNumShift) & StackNumMask;
        result.ctrl = (header >> CtrlIdShift) & CtrlIdMask;
    }

    return result;
}

MESYTEC_MVLC_EXPORT std::string decode_frame_header(u32 header);
MESYTEC_MVLC_EXPORT std::string format_frame_flags(u8 frameFlags);

inline bool has_error_flag_set(u8 frameFlags)
{
    return (frameFlags & frame_flags::AllErrorFlags) != 0u;
}

inline u32 get_frame_length(u32 header)
{
    return extract_frame_info(header).len;
}

MESYTEC_MVLC_EXPORT const char *get_frame_flag_shift_name(u8 flag);

stacks::TimerBaseUnit MESYTEC_MVLC_EXPORT timer_base_unit_from_string(const std::string &str);

// String representation for the known system_event::subtype flags.
// Returns "unknown/custom" for user defined flags.
std::string MESYTEC_MVLC_EXPORT system_event_type_to_string(u8 eventType);

inline u32 trigger_value(stacks::TriggerType triggerType, u8 irqLevel = 0)
{
    u32 triggerVal = triggerType << stacks::TriggerTypeShift;

    if ((triggerType == stacks::TriggerType::IRQNoIACK
         || triggerType == stacks::TriggerType::IRQWithIACK)
        && irqLevel > 0)
    {
        triggerVal |= (irqLevel - 1) & stacks::TriggerBitsMask;
    }

    return triggerVal;
}

// Returns a pair consisting of (TriggerType, irqLevel).
inline std::pair<stacks::TriggerType, u8> decode_trigger_value(const u32 triggerVal)
{
    stacks::TriggerType triggerType = static_cast<stacks::TriggerType>(
        (triggerVal >> stacks::TriggerTypeShift) & stacks::TriggerTypeMask);

    u8 irqLevel = 0;

    if (triggerType == stacks::TriggerType::IRQNoIACK
        || triggerType == stacks::TriggerType::IRQWithIACK)
    {
        irqLevel = 1 + (triggerVal & stacks::TriggerBitsMask);
    }

    return std::make_pair(triggerType, irqLevel);
}


} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_UTIL_H__ */
