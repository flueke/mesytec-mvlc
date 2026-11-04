#include "mvlc_mvp.h"

#include <algorithm>
#include <chrono>

#include "mvlc_command_builders.h"
#include "mvlc_constants.h"
#include "mvlc_threading.h"
#include "util/fmt.h"
#include "util/logging.h"
#include "vme_constants.h"

namespace mesytec::mvlc::mvp
{

static const std::chrono::milliseconds MaxResponseWaitTime(2500);

namespace
{
    std::error_code write_reg(MVLC &mvlc, u32 address, u32 value)
    {
        return mvlc.vmeWrite(address, value, vme_amods::A32, VMEDataWidth::D16);
    }

    std::error_code read_reg(MVLC &mvlc, u32 address, u32 &value)
    {
        return mvlc.vmeRead(address, value, vme_amods::A32, VMEDataWidth::D16);
    }
}

std::error_code enable_flash_interface(MVLC &mvlc, u32 moduleBase)
{
    get_logger("mvlc_mvp")->debug("enabling flash interface on {:#010x}", moduleBase);
    return write_reg(mvlc, moduleBase + EnableFlashRegister, 1);
}

std::error_code disable_flash_interface(MVLC &mvlc, u32 moduleBase)
{
    get_logger("mvlc_mvp")->debug("disabling flash interface on {:#010x}", moduleBase);
    return write_reg(mvlc, moduleBase + EnableFlashRegister, 0);
}

std::error_code clear_output_fifo(MVLC &mvlc, u32 moduleBase)
{
    auto logger = get_logger("mvlc_mvp");
    auto tStart = std::chrono::steady_clock::now();

    for (unsigned i = 0; i < OutputFifoMaxClearReads; ++i)
    {
        u32 fifoValue = 0;

        if (auto ec = read_reg(mvlc, moduleBase + OutputFifoRegister, fifoValue))
            return ec;

        if (fifoValue & output_fifo_flags::InvalidRead)
            return {};

        logger->trace("clear_output_fifo: discarding {:#06x}", fifoValue);

        if (std::chrono::steady_clock::now() - tStart >= MaxResponseWaitTime)
            return std::make_error_code(std::errc::timed_out);
    }

    logger->debug("clear_output_fifo: fifo not empty after {} reads", OutputFifoMaxClearReads);
    return std::make_error_code(std::errc::protocol_error);
}

std::error_code write_instruction(MVLC &mvlc, u32 moduleBase, const std::vector<u8> &instruction)
{
    get_logger("mvlc_mvp")->debug("write_instruction: moduleBase={:#010x}, instr={:#04x}",
                                  moduleBase, fmt::join(instruction, ", "));

    for (u8 arg: instruction)
    {
        if (auto ec = write_reg(mvlc, moduleBase + InputFifoRegister, arg))
            return ec;
    }

    return {};
}

std::error_code read_response(MVLC &mvlc, u32 moduleBase, std::vector<u8> &dest)
{
    auto tStart = std::chrono::steady_clock::now();

    while (true)
    {
        u32 fifoValue = 0;

        if (auto ec = read_reg(mvlc, moduleBase + OutputFifoRegister, fifoValue))
            return ec;

        if (fifoValue & output_fifo_flags::InvalidRead)
        {
            if (!dest.empty())
                break;
        }
        else
            dest.push_back(fifoValue & output_fifo_flags::DataMask);

        if (std::chrono::steady_clock::now() - tStart >= MaxResponseWaitTime)
            return std::make_error_code(std::errc::timed_out);
    }

    get_logger("mvlc_mvp")->debug("read_response: moduleBase={:#010x}, response={:#04x}",
                                  moduleBase, fmt::join(dest, ", "));
    return {};
}

bool check_response(const std::vector<u8> &request, const std::vector<u8> &response)
{
    auto logger = get_logger("mvlc_mvp");

    if (response.size() < 2 || response.size() < request.size())
    {
        logger->warn("check_response: response too short (len={}) for request (len={})",
                     response.size(), request.size());
        return false;
    }

    // Workaround for MVLC flash interface issues: the response sometimes starts
    // with an additional byte.
    auto responseBegin = std::begin(response);

    if (!request.empty() && response[0] != request[0] && response[1] == request[0])
        ++responseBegin;

    auto mirrors = [&](auto reqBegin)
    {
        return static_cast<size_t>(std::distance(responseBegin, std::end(response)))
                   >= static_cast<size_t>(std::distance(reqBegin, std::end(request)))
            && std::equal(reqBegin, std::end(request), responseBegin);
    };

    // Workaround for some MDPP-32 modules: the response is missing the first
    // byte of the request.
    if (!mirrors(std::begin(request)) && !mirrors(std::begin(request) + 1))
    {
        logger->warn("check_response: response does not mirror the request: request={:#04x}, response={:#04x}",
                     fmt::join(request, ", "), fmt::join(response, ", "));
        return false;
    }

    u8 codeStart = *(response.end() - 2);
    u8 status = *(response.end() - 1);

    if (codeStart != 0xff)
    {
        logger->warn("check_response: invalid response code start {:#04x} (expected 0xff)", codeStart);
        return false;
    }

    if (!(status & 0x01))
    {
        logger->warn("check_response: instruction failed (status bit 0 not set): {:#04x}", status);
        return false;
    }

    return true;
}

std::error_code nop(MVLC &mvlc, u32 moduleBase, u8 *statusByte)
{
    const std::vector<u8> instr = { opcodes::NOP };
    std::vector<u8> response;

    if (auto ec = write_instruction(mvlc, moduleBase, instr))
        return ec;

    if (auto ec = read_response(mvlc, moduleBase, response))
        return ec;

    if (!check_response(instr, response))
        return std::make_error_code(std::errc::protocol_error);

    if (statusByte)
        *statusByte = response.back();

    return {};
}

std::error_code read_area_index(MVLC &mvlc, u32 moduleBase, u8 &area)
{
    const std::vector<u8> instr = { opcodes::RAI };
    std::vector<u8> response;

    if (auto ec = write_instruction(mvlc, moduleBase, instr))
        return ec;

    if (auto ec = read_response(mvlc, moduleBase, response))
        return ec;

    // Expected response: RAI, area, 0xff, status
    if (response.size() < 4 || !check_response(instr, response))
        return std::make_error_code(std::errc::protocol_error);

    area = *(response.end() - 3);
    return {};
}

std::vector<u32> make_boot_command_buffer(u32 moduleBase, u8 area, u16 superRef, u32 stackRef)
{
    StackCommandBuilder stack;
    stack.addWriteMarker(stackRef);

    for (u8 arg: { opcodes::BFP, AccessCode[0], AccessCode[1], area })
        stack.addVMEWrite(moduleBase + InputFifoRegister, arg, vme_amods::A32, VMEDataWidth::D16);

    SuperCommandBuilder super;
    super.addReferenceWord(superRef);
    super.addStackUpload(stack, static_cast<u8>(Pipe::Command), stacks::ImmediateStackStartOffsetBytes);
    super.addWriteLocal(stacks::Stack0OffsetRegister, stacks::ImmediateStackStartOffsetBytes);
    super.addWriteLocal(stacks::Stack0TriggerRegister, 1u << stacks::ImmediateShift);

    return make_command_buffer(super);
}

std::error_code send_boot_instruction(MVLC &mvlc, u32 moduleBase, u8 area)
{
    if (area > AreaMax)
        return std::make_error_code(std::errc::invalid_argument);

    // Arbitrary reference values. A late response, if any, is logged and
    // dropped by the cmd pipe reader.
    auto cmdBuffer = make_boot_command_buffer(moduleBase, area, 0xb007u, 0xb007b007u);

    get_logger("mvlc_mvp")->debug("send_boot_instruction: moduleBase={:#010x}, area={}, buffer={:#010x}",
                                  moduleBase, area, fmt::join(cmdBuffer, " "));

    auto guard = mvlc.getLocks().lockCmd();
    size_t bytesWritten = 0;

    return mvlc.getImpl()->write(
        Pipe::Command,
        reinterpret_cast<const u8 *>(cmdBuffer.data()),
        cmdBuffer.size() * sizeof(u32),
        bytesWritten);
}

}
