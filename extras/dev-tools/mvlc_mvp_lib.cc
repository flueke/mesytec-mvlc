#include "mvlc_mvp_lib.h"
#include <chrono>
#include <stdexcept>

namespace mesytec
{
namespace mvlc
{

std::error_code enable_flash_interface(MVLC &mvlc, u32 moduleBase)
{
    spdlog::info("Enabling flash interface on 0x{:08x}", moduleBase);
    return mvlc.vmeWrite(moduleBase + EnableFlashRegister, 1, vme_amods::A32, VMEDataWidth::D16);
}

std::error_code disable_flash_interface(MVLC &mvlc, u32 moduleBase)
{
    spdlog::info("Disabling flash interface on 0x{:08x}", moduleBase);
    return mvlc.vmeWrite(moduleBase + EnableFlashRegister, 0, vme_amods::A32, VMEDataWidth::D16);
}

std::error_code read_output_fifo(MVLC &mvlc, u32 moduleBase, u32 &dest)
{
    return mvlc.vmeRead(moduleBase + OutputFifoRegister, dest, vme_amods::A32, VMEDataWidth::D16);
}

std::error_code clear_output_fifo(MVLC &mvlc, u32 moduleBase)
{
    spdlog::info("Clearing output fifo on 0x{:08x}", moduleBase);

    size_t cycles = 0;
    //std::vector<u32> readBuffer;

    while (true)
    {
        u32 fifoValue = 0;

        auto ec = mvlc.vmeRead(
            moduleBase + OutputFifoRegister,
            fifoValue, vme_amods::A32, VMEDataWidth::D16);

        //readBuffer.push_back(fifoValue);

        ++cycles;

        if (ec) return ec;

        if (fifoValue & output_fifo_flags::InvalidRead)
            break;

        spdlog::info("  clear_output_fifo: 0x{:04x} = 0x{:08x}", OutputFifoRegister, fifoValue);
    }

    spdlog::info("clear_output_fifo returned after {} read cycles", cycles);

    //if (cycles > 1)
    //  log_buffer(std::cout, readBuffer, "clear_output_fifo read buffer");

    return {};
}

template<typename C>
std::error_code perform_writes(MVLC &mvlc, const C &writes)
{
    for (auto write: writes)
    {
        u32 addr = write.first;
        u32 val = write.second;

        if (auto ec = mvlc.vmeWrite(addr, val, vme_amods::A32, VMEDataWidth::D16))
            return ec;
    }

    return {};
}

std::error_code command_transaction(
    MVLC &mvlc, u32 moduleBase,
    const std::vector<u8> &instruction,
    std::vector<u8> &responseBuffer)
{
    if (auto ec = write_instruction(mvlc, moduleBase, instruction))
        return ec;

    if (auto ec = read_response(mvlc, moduleBase, responseBuffer))
        return ec;

    if (!check_response(instruction, responseBuffer))
        return make_error_code(std::errc::protocol_error);

    return {};
}


std::error_code set_area_index(MVLC &mvlc, u32 moduleBase, unsigned area)
{
    spdlog::info("Setting area index on 0x{:08x} to {}", moduleBase, area);

    std::vector<u8> instr = { 0x20, 0xCD, 0xAB, static_cast<u8>(area) };
    std::vector<u8> response;

    return command_transaction(mvlc, moduleBase, instr, response);
}

std::error_code enable_flash_write(MVLC &mvlc, u32 moduleBase)
{
    spdlog::info("Enabling flash write on 0x{:08x}", moduleBase);

    std::vector<u8> instr = { 0x80, 0xCD, 0xAB };
    std::vector<u8> response;

    return command_transaction(mvlc, moduleBase, instr, response);
}

std::error_code write_instruction(MVLC &mvlc, u32 moduleBase, const std::vector<u8> &instruction)
{
    spdlog::info("write_instruction: moduleBase=0x{:08x}, instr={:02x}",
                  moduleBase, fmt::join(instruction, ", "));

    for (u8 arg: instruction)
    {
        if (auto ec = mvlc.vmeWrite(
                moduleBase + InputFifoRegister, arg,
                vme_amods::A32, VMEDataWidth::D16))
        {
            return ec;
        }
    }

    return {};
}

std::error_code read_response(MVLC &mvlc, u32 moduleBase, std::vector<u8> &dest)
{
    while (true)
    {
        u32 fifoValue = 0;

        if (auto ec = mvlc.vmeRead(
                moduleBase + OutputFifoRegister, fifoValue,
                vme_amods::A32, VMEDataWidth::D16))
        {
            return ec;
        }

        if (fifoValue & output_fifo_flags::InvalidRead)
            break;

        dest.push_back(fifoValue & 0xff);
    }

    spdlog::info("read_response: moduleBase=0x{:08x}, got {} bytes: {:02x}",
                 moduleBase, dest.size(), fmt::join(dest, ", "));
    return {};
}

bool check_response(const std::vector<u8> &request,
                    const std::vector<u8> &response)
{
    if (response.size() < request.size())
    {
        spdlog::warn("response too short (len={}) for request (len={})",
                     response.size(), request.size());
        return false;
    }

    if (!std::equal(std::begin(request), std::end(request), std::begin(response)))
    {
        spdlog::warn("request contents != response contents");
        return false;
    }

    if (response.size() < 2)
    {
        spdlog::warn("short response (size<2)");
        return false;
    }

    u8 codeStart = *(response.end() - 2);
    u8 status    = *(response.end() - 1);

    if (codeStart != 0xff)
    {
        spdlog::warn("invalid response code start 0x{:02} (expected 0xff)", codeStart);
        return false;
    }

    if (!(status & 0x01))
    {
        spdlog::warn("instruction failed (status bit 0 not set)");
        return false;
    }

    return true;
}

std::error_code set_verbose_mode(MVLC &mvlc, u32 moduleBase, bool verbose)
{
    spdlog::info("Setting verbose mode to {}", verbose);

    u8 veb = verbose ? 0 : 1;

    std::vector<u8> instr = { 0x60, 0xCD, 0xAB, veb };
    std::vector<u8> resp;

    return command_transaction(mvlc, moduleBase, instr, resp);
}

void fill_page_buffer_from_stack_output(std::vector<u8> &pageBuffer, const std::vector<u32> stackOutput)
{
    assert(stackOutput.size() > 3);
    assert(is_stack_buffer(stackOutput.at(0)));
    assert(stackOutput.at(1) == 0x13370001u);

    pageBuffer.clear();
    auto view = basic_string_view<u32>(stackOutput.data(), stackOutput.size());

    while (!view.empty())
    {
        u32 word = view[0];

        if (is_stack_buffer(word))
        {
            assert(view.size() >= 2);
            assert(view[1] == 0x13370001u);
            view.remove_prefix(2); // skip over the stack buffer header and the marker
        }
        else if (is_stack_buffer_continuation(word) || is_blockread_buffer(word))
        {
            view.remove_prefix(1); // skip over the header
        }
        else
        {
            view.remove_prefix(1);

            if (word & (output_fifo_flags::InvalidRead))
            {
                spdlog::info("fill_page_buffer_from_stack_output: first non-data word: 0x{:08x}", word);
                break;
            }

            pageBuffer.push_back(word & 0xffu);
        }
    }

    if (!view.empty())
    {
        spdlog::warn("fill_page_buffer_from_stack_output: {} words left in stackOutput data", view.size());
        log_buffer(std::cout, view);
        spdlog::warn("fill_page_buffer_from_stack_output: end of {} words left in stackOutput data", view.size());
    }
}

std::error_code read_page(
    MVLC &mvlc, u32 moduleBase,
    const FlashAddress &addr, u8 section, unsigned bytesToRead,
    std::vector<u8> &pageBuffer)
{
    if (bytesToRead > PageSize)
        throw std::invalid_argument("read_page: bytesToRead > PageSize");

    // Note: the REF instruction does not mirror itself to the output fifo.
    // Instead the page data starts immediately.

    //u8 len = static_cast<u8>(bytesToRead == PageSize ? 0 : bytesToRead);
    //std::vector<u8> instr = { 0xB0, addr[0], addr[1], addr[2], section, len };

    StackCommandBuilder sb;
    sb.addWriteMarker(0x13370001u);
    sb.addVMEWrite(moduleBase + InputFifoRegister, 0xB0, vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[0], vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[1], vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[2], vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, section, vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, bytesToRead == PageSize ? 0 : bytesToRead,
                   vme_amods::A32, VMEDataWidth::D16);

    // Waiting is required, otherwise the response data will start with the
    // InvalidRead flag set.
    sb.addWait(100000);
    // Turn the next vme read into a fake block read. Read one more word than
    // expected to get the first flash interface status word after the payload.
    sb.addSetAccu(bytesToRead + 1);
    // This single read is turned into a block read due to the accu being set.
    sb.addVMERead(moduleBase + OutputFifoRegister, vme_amods::A32, VMEDataWidth::D16);

    std::vector<u32> readBuffer; // stores raw read data

    if (auto ec = mvlc.stackTransaction(sb, readBuffer))
    {
        spdlog::error("read_page(): mvlc.stackTransaction: {}", ec.message());
        return ec;
    }

    fill_page_buffer_from_stack_output(pageBuffer, readBuffer);

    return {};
}

std::error_code write_page(
    MVLC &mvlc, u32 moduleBase,
    const FlashAddress &addr, u8 section,
    const std::vector<u8> &pageBuffer)
{
    if (pageBuffer.empty())
        throw std::invalid_argument("write_page: empty data given");

    if (pageBuffer.size() > PageSize)
        throw std::invalid_argument("write_page: data size > page size");

    //if (auto ec = enable_flash_write(mvlc, moduleBase))
    //    return ec;

    u8 lenByte = pageBuffer.size() == PageSize ? 0 : pageBuffer.size();

    std::vector<std::pair<u32, u16>> writes =
    {
        { moduleBase + InputFifoRegister, 0xA0 },
        { moduleBase + InputFifoRegister, addr[0] },
        { moduleBase + InputFifoRegister, addr[1] },
        { moduleBase + InputFifoRegister, addr[2] },
        { moduleBase + InputFifoRegister, section },
        { moduleBase + InputFifoRegister, lenByte },
    };

    if (auto ec = perform_writes(mvlc, writes))
        return ec;

    for (u8 data: pageBuffer)
    {
        if (auto ec = mvlc.vmeWrite(moduleBase + InputFifoRegister, data, vme_amods::A32, VMEDataWidth::D16))
            return ec;
    }

    if (auto ec = clear_output_fifo(mvlc, moduleBase))
        return ec;

    return {};
}

std::error_code erase_section(
    MVLC &mvlc, u32 moduleBase, u8 index)
{
    if (auto ec = enable_flash_write(mvlc, moduleBase))
        return ec;

    std::vector<u8> instr = { 0x90, 0, 0, 0, index };
    std::vector<u8> response;

    if (auto ec = write_instruction(mvlc, moduleBase, instr))
        return ec;

    if (auto ec = read_response(mvlc, moduleBase, response))
        return ec;

    spdlog::info("Response from erase instruction: {:02x}", fmt::join(response, ", "));

    if (instr != response)
    {
        throw std::runtime_error(fmt::format(
                "Unexpected response from erase command: {:02x}",
                fmt::join(response, ", ")));
    }

    auto tStart = std::chrono::steady_clock::now();
    u32 outputFifoValue = 0u;
    u32 loops = 0u;

    // Poll until InvalidRead flag is set
    spdlog::info("Polling until InvalidRead is set");
    do
    {
        if (auto ec = read_output_fifo(mvlc, moduleBase, outputFifoValue))
            return ec;

        ++loops;
    } while (!(outputFifoValue & output_fifo_flags::InvalidRead));

    spdlog::info("Done polling until InvalidRead is set, loops={}", loops);
    loops = 0u;

    // Now poll until InvalidRead is not set anymore
    spdlog::info("Polling until InvalidRead is cleared");
    do
    {
        if (auto ec = read_output_fifo(mvlc, moduleBase, outputFifoValue))
            return ec;

        ++loops;
    } while (outputFifoValue & output_fifo_flags::InvalidRead);

    spdlog::info("Done polling until InvalidRead is cleared, loops={}", loops);
    loops = 0u;

    // outputFifofValue should now contain the flash response code 0xff
    if (outputFifoValue != 0xff)
        throw std::runtime_error(fmt::format(
                "Invalid flash response code 0x{:02x}, expected 0xff", outputFifoValue));

    spdlog::info("flash response code ok");

    // Read the flash response status
    if (auto ec = mvlc.vmeRead(moduleBase + OutputFifoRegister, outputFifoValue,
                               vme_amods::A32, VMEDataWidth::D16))
        return ec;

    if (!(outputFifoValue & FlashInstructionSuccess))
        throw std::runtime_error(fmt::format("Flash instruction not successful, code = 0x{:02x}",
                                             outputFifoValue));

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

    spdlog::info("flash response status ok, erasing took {} ms", elapsed.count());

    return {};
}

}
}
