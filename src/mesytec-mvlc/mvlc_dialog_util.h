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
#ifndef __MESYTEC_MVLC_MVLC_DIALOG_UTIL_H__
#define __MESYTEC_MVLC_MVLC_DIALOG_UTIL_H__

#include <array>
#include <cstdlib>
#include <iostream>
#include <iomanip>

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/logging.h"

namespace mesytec
{
namespace mvlc
{

struct StackInfo
{
    u32 triggerValue; // value of the trigger register
    u32 triggerAddress; // address of the trigger register
    u32 offset;
    u32 offsetAddress; // address of the offset register
    u16 startAddress;
    std::vector<u32> contents;
};

template<typename DIALOG_API>
std::pair<std::vector<u32>, std::error_code>
read_stack_contents(DIALOG_API &mvlc, u16 startAddress)
{
    using namespace stack_commands;

    u32 stackHeader = 0u;

    if (auto ec = mvlc.readRegister(startAddress, stackHeader))
        return std::make_pair(std::vector<u32>{}, ec);

    std::vector<u32> contents;

    if (stackHeader == 0)
    {
        return { contents, {} };
    }

    contents.reserve(64);
    contents.push_back(stackHeader);

    u8 headerType = (stackHeader >> CmdShift) & CmdMask; // 0xF3

    if (headerType != static_cast<u8>(StackCommandType::StackStart))
        return { contents, make_error_code(MVLCErrorCode::InvalidStackHeader) };

    u32 addr  = startAddress + AddressIncrement;
    u32 value = 0u;

    do
    {
        if (addr >= stacks::StackMemoryEnd)
            return { contents, make_error_code(MVLCErrorCode::StackMemoryExceeded) };

        if (auto ec = mvlc.readRegister(addr, value))
            return { contents, ec };

        contents.push_back(value);
        addr += AddressIncrement;

    } while (((value >> CmdShift) & CmdMask) != static_cast<u8>(StackCommandType::StackEnd)); // 0xF4

    return { contents, {} };
}

template<typename DIALOG_API>
std::pair<StackInfo, std::error_code>
read_stack_info(DIALOG_API &mvlc, u8 id)
{
    StackInfo result = {};

    if (id >= stacks::StackCount)
        return { result, make_error_code(MVLCErrorCode::StackCountExceeded) };

    result.triggerAddress = stacks::get_trigger_register(id);
    result.offsetAddress = stacks::get_offset_register(id);

    if (auto ec = mvlc.readRegister(result.triggerAddress, result.triggerValue))
        return { result, ec };

    if (auto ec = mvlc.readRegister(result.offsetAddress, result.offset))
        return { result, ec };

    result.startAddress = stacks::StackMemoryBegin + result.offset;

    auto sc = read_stack_contents(mvlc, result.startAddress);

    result.contents = sc.first;

    return { result, sc.second };
}

template<typename DIALOG_API>
std::error_code enable_daq_mode(DIALOG_API &mvlc)
{
    return mvlc.writeRegister(registers::daq_mode, 1);
}

template<typename DIALOG_API>
std::error_code disable_daq_mode(DIALOG_API &mvlc)
{
    return mvlc.writeRegister(registers::daq_mode, 0);
}

template<typename DIALOG_API>
std::error_code read_daq_mode(DIALOG_API &mvlc, u32 &daqMode)
{
    return mvlc.readRegister(registers::daq_mode, daqMode);
}

inline SuperCommandBuilder get_disable_daq_mode_and_triggers_commands()
{
    SuperCommandBuilder sb;
    sb.addReferenceWord(std::rand() % 0xffff);
    sb.addWriteLocal(registers::daq_mode, 0);

    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_trigger_register(stackId);
        sb.addWriteLocal(addr, stacks::NoTrigger);
    }

    return sb;
}

// Compatibility alias
inline SuperCommandBuilder get_disable_all_triggers_and_daq_mode_commands()
{
    return get_disable_daq_mode_and_triggers_commands();
}

// Disables MVLC DAQ mode and all stack triggers, all in a single stack
// transaction. Used to end a DAQ run.
template<typename DIALOG_API>
std::error_code disable_daq_mode_and_triggers(DIALOG_API &mvlc)
{
    auto sb = get_disable_all_triggers_and_daq_mode_commands();
    std::vector<u32> responseBuffer;
    auto ec = mvlc.superTransaction(sb, responseBuffer);
    log_buffer(get_logger("dialog_util"), spdlog::level::trace,
               responseBuffer, "response from disable_daq_mode_and_triggers()");
    return ec;
}

// Compatibility alias
template<typename DIALOG_API>
std::error_code disable_all_triggers_and_daq_mode(DIALOG_API &mvlc)
{
    return disable_daq_mode_and_triggers(mvlc);
}

inline SuperCommandBuilder get_reset_stack_offsets_commands()
{
    SuperCommandBuilder sb;
    sb.addReferenceWord(std::rand() % 0xffff);

    for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
    {
        u16 addr = stacks::get_offset_register(stackId);
        sb.addWriteLocal(addr, 0);
    }

    return sb;
}

template<typename DIALOG_API>
std::error_code reset_stack_offsets(DIALOG_API &mvlc)
{
    auto sb = get_reset_stack_offsets_commands();
    std::vector<u32> responseBuffer;
    auto ec = mvlc.superTransaction(sb, responseBuffer);
    log_buffer(get_logger("dialog_util"), spdlog::level::trace,
               responseBuffer, "response from reset_stack_offsets()");
    return ec;
}

// Builds, uploads and sets up the readout stack for each event in the vme
// config. Stacks are written in order to MVLC stack memory with a 1 word gap
// between stacks.
template<typename MVLC_API>
std::error_code setup_readout_stacks(
    MVLC_API &mvlc,
    const std::vector<StackCommandBuilder> &readoutStacks)
{
    // Stack0 is reserved for immediate exec
    u8 stackId = stacks::FirstReadoutStackID;

    // 1 word gap between immediate stack and first readout stack
    u16 uploadWordOffset = stacks::ImmediateStackStartOffsetWords + stacks::ImmediateStackReservedWords + 1;

    for (const auto &stackBuilder: readoutStacks)
    {
        if (stackId >= mvlc.getStackCount())
            return make_error_code(MVLCErrorCode::StackCountExceeded);

        // need to convert to a buffer to determine the size
        auto stackBuffer = make_stack_buffer(stackBuilder);

        u16 uploadAddress = uploadWordOffset * AddressIncrement;
        u16 endAddress    = uploadAddress + stackBuffer.size() * AddressIncrement;

        if (mvlc::stacks::StackMemoryBegin + endAddress >= mvlc::stacks::StackMemoryEnd)
            return make_error_code(MVLCErrorCode::StackMemoryExceeded);

        u8 stackOutputPipe = stackBuilder.suppressPipeOutput() ? SuppressPipeOutput : DataPipe;

        if (auto ec = mvlc.uploadStack(stackOutputPipe, uploadAddress, stackBuffer))
            return ec;

        u16 offsetRegister = stacks::get_offset_register(stackId);

        //uploadAddress = uploadAddress & mvlc::stacks::StackOffsetBitMaskBytes;

        if (auto ec = mvlc.writeRegister(offsetRegister, uploadAddress))
            return ec;

        stackId++;

        // again leave a 1 word gap between stacks and account for the F3/F4 stack begin/end words
        uploadWordOffset += stackBuffer.size() + 1 + 2;
    }

    return {};
}

template<typename DIALOG_API>
std::error_code write_stack_trigger_value(
    DIALOG_API &mvlc, u8 stackId, u32 triggerVal)
{
    u16 triggerReg = stacks::get_trigger_register(stackId);
    return mvlc.writeRegister(triggerReg, triggerVal);
}

// Combines uploading the command stack, setting up the stack memory offset
// register and the stack trigger register.
// Stack output is directed to the DataPipe unless
// stackBuilder.suppressPipeOutput() is true.
// Assumes a memory layout where the stack memory is divided into equal sized
// parts of 128 words each. So stack1 is written to the memory starting at
// offset 128, stack2 at offset 256.
// This function is not intended to be used for stack0, the stack reserved for
// immediate command execution.
template<typename DIALOG_API>
std::error_code setup_readout_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stackBuilder,
    u8 stackId,
    u32 stackTriggerValue)
{
    if (stackId == 0)
        return make_error_code(MVLCErrorCode::Stack0IsReserved);

    if (get_encoded_stack_size(stackBuilder) > stacks::StackMemorySegmentSize)
        return make_error_code(MVLCErrorCode::StackMemoryExceeded);

    u16 uploadWordOffset = stackId * stacks::StackMemorySegmentSize;
    u16 uploadAddress = uploadWordOffset * AddressIncrement;
    u8 stackOutputPipe = stackBuilder.suppressPipeOutput() ? SuppressPipeOutput : DataPipe;

    if (auto ec = mvlc.uploadStack(stackOutputPipe, uploadAddress, stackBuilder))
        return ec;

    if (auto ec = mvlc.writeRegister(
            stacks::get_offset_register(stackId),
            uploadAddress))
            //uploadAddress & stacks::StackOffsetBitMaskBytes))
    {
        return ec;
    }

    return write_stack_trigger_value(mvlc, stackId, stackTriggerValue);
}

template<typename DIALOG_API>
std::error_code setup_readout_stack(
    DIALOG_API &mvlc,
    const StackCommandBuilder &stackBuilder,
    u8 stackId,
    const stacks::Trigger &trigger)
{
    return setup_readout_stack(
        mvlc,
        stackBuilder,
        stackId,
        trigger.value);
}

// Writes the raw stack trigger values using a single stack transaction.
template<typename DIALOG_API>
std::error_code setup_readout_triggers(
    DIALOG_API &mvlc,
    const std::array<u32, stacks::ReadoutStackCount> &triggerValues)
{
    SuperCommandBuilder sb;
    sb.addReferenceWord(std::rand() % 0xffff);
    u8 stackId = stacks::FirstReadoutStackID;

    for (unsigned triggerIdx = 0; triggerIdx < mvlc.getReadoutStackCount(); ++triggerIdx)
    {
        u32 triggerVal = triggerValues.at(triggerIdx);

        #if 0
        std::cerr << "setup_readout_triggers: stackId=" << static_cast<unsigned>(stackId)
            << ", triggerVal=0x" << std::hex << triggerVal << std::dec
            << std::endl;
        #endif

        u16 triggerReg = stacks::get_trigger_register(stackId);
        sb.addWriteLocal(triggerReg, triggerVal);
        ++stackId;
    }

    std::vector<u32> responseBuffer;

    return mvlc.superTransaction(sb, responseBuffer);
}

template<typename DIALOG_API>
std::error_code setup_readout_triggers(
    DIALOG_API &mvlc,
    const std::vector<u32> &triggerValues)
{
    std::array<u32, stacks::ReadoutStackCount> triggersArray;
    triggersArray.fill(0);

    std::copy_n(std::begin(triggerValues),
                std::min(triggerValues.size(), triggersArray.size()),
                std::begin(triggersArray));

    return setup_readout_triggers(mvlc, triggersArray);
}

template<typename DIALOG_API>
std::error_code setup_readout_triggers(
    DIALOG_API &mvlc,
    const std::array<stacks::Trigger, stacks::ReadoutStackCount> &triggers)
{
    std::array<u32, stacks::ReadoutStackCount> triggerValues;

    std::transform(std::begin(triggers), std::end(triggers), std::begin(triggerValues),
                   [] (const stacks::Trigger &st) {
                        return st.value;
                   });

    return setup_readout_triggers(mvlc, triggerValues);
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_DIALOG_UTIL_H__ */
