#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_mvp_lib.h>

using namespace mesytec::mvlc;

std::error_code read_fixed_size_response(MVLC &mvlc, u32 moduleBase, std::vector<u8> &dest, size_t bytesToRead)
{
    dest.clear();

    while (dest.size() < bytesToRead) // FIXME: dangerous. do time out at some point
    {
        u32 fifoValue = 0;

        if (auto ec = mvlc.vmeRead(
                moduleBase + OutputFifoRegister, fifoValue,
                vme_amods::A32, VMEDataWidth::D16))
        {
            return ec;
        }

        if (fifoValue & output_fifo_flags::InvalidRead)
        {
            if (dest.size() == bytesToRead - 1)
            {
                spdlog::warn("read_fixed_size_response: InvalidRead after reading {} bytes: {:02x}",
                             dest.size(), fmt::join(dest, ", "));
                assert(false);
            }
            continue;
        }

        dest.push_back(fifoValue & 0xff);
    }

    spdlog::info("read_fixed_size_response: moduleBase=0x{:08x}, got {} bytes: {:02x}",
                 moduleBase, dest.size(), fmt::join(dest, ", "));
    return {};
}

// Write a full page or less by uploading and executing command stacks
// containing the write commands.
std::error_code erase_section_write_one_page(
    MVLC &mvlc, u32 moduleBase,
    const FlashAddress &addr, u8 section,
    const std::vector<u8> &pageBuffer)
{
    static const bool useVerbose = false;

    if (pageBuffer.empty())
        throw std::invalid_argument("write_page2: empty data given");

    if (pageBuffer.size() > PageSize)
        throw std::invalid_argument("write_page2: data size > page size");

    u8 lenByte = pageBuffer.size() == PageSize ? 0 : pageBuffer.size();

    auto tStart = std::chrono::steady_clock::now();

    if (useVerbose)
        if (auto ec = set_verbose_mode(mvlc, moduleBase, true))
            return ec;

    {
        // debugging the fpga code: first stack erases the section, disables
        // verbose and enables flash writing. Response should be 23 bytes long.
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        // EFW - enable flash write
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x80,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xCD,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xAB,  vme_amods::A32, VMEDataWidth::D16);
        // Erase the whole section
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x90,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x00,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x00,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x00,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, section,  vme_amods::A32, VMEDataWidth::D16);
        // Disable verbose mode
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x60,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xCD,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xAB,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x01,  vme_amods::A32, VMEDataWidth::D16);
        // EFW - enable flash write
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0x80,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xCD,  vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xAB,  vme_amods::A32, VMEDataWidth::D16);

        std::vector<u32> stackResponse;
        if (auto ec = mvlc.stackTransaction(sb, stackResponse))
        {
            spdlog::error("write_page2(): stackTransaction failed: prepare stack: {}",
                          ec.message());
            return ec;
        }

        // read and parse the response
        {
            const std::vector<u8> expected = {
                0x80, 0xcd, 0xab, 0xff, 0x0f, 0x90, 0x00, 0x00,
                0x00, 0x03, 0xff, 0x0f, 0x60, 0xcd, 0xab, 0x01,
                0xff, 0x0f, 0x80, 0xcd, 0xab, 0xff, 0x0f
            };

            std::vector<u8> response;
            const size_t expectedResponseSize = 23;

            if (auto ec = read_fixed_size_response(mvlc, moduleBase, response, expectedResponseSize))
                return ec;

            assert(response  == expected);

#if 0
            if (response.size() < 2)
                throw std::runtime_error("short response");

            u8 codeStart = *(response.end() - 2);
            u8 status    = *(response.end() - 1);

            if (codeStart != 0xff)
            {
                spdlog::warn("invalid response code start 0x{:02} (expected 0xff)", codeStart);
                throw std::runtime_error("invalid response code");
            }

            if (!(status & 0x01))
            {
                spdlog::warn("instruction failed (status bit 0 not set): 0x{02x}", status);
                throw std::runtime_error("instruction failed");
            }
#endif
        }
    }

    StackCommandBuilder sb;
    sb.addWriteMarker(0x13370002u);

    // EFW - enable flash write
    /*
    sb.addVMEWrite(moduleBase + InputFifoRegister, 0x80,  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, 0xCD,  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, 0xAB,  vme_amods::A32, VMEDataWidth::D16);
    */
    // WRF - write flash
    sb.addVMEWrite(moduleBase + InputFifoRegister, 0xA0,     vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[0],  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[1],  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, addr[2],  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, section,  vme_amods::A32, VMEDataWidth::D16);
    sb.addVMEWrite(moduleBase + InputFifoRegister, lenByte,  vme_amods::A32, VMEDataWidth::D16);

    auto pageIter = pageBuffer.begin();

    while (pageIter != pageBuffer.end())
    {
        while (get_encoded_stack_size(sb) < MirrorTransactionMaxContentsWords / 2 - 2
               && pageIter != pageBuffer.end())
        {
            sb.addVMEWrite(moduleBase + InputFifoRegister, *pageIter++,
                           vme_amods::A32, VMEDataWidth::D16);
        }

        std::vector<u32> stackResponse;

        spdlog::info("write_page2(): performing stackTransaction with stack of size {}",
                     get_encoded_stack_size(sb));

        if (auto ec = mvlc.stackTransaction(sb, stackResponse))
        {
            spdlog::error("write_page2(): stackTransaction failed: write stack: {}",
                          ec.message());
            return ec;
        }

        spdlog::trace("write_page(): response from stackTransaction: size={}, data={:08x}",
                      stackResponse.size(), fmt::join(stackResponse, ", "));

        //  Expect the 0xF3 stack frame and the marker word
        if (stackResponse.size() != 2)
            return make_error_code(MVLCErrorCode::UnexpectedResponseSize);

        if (extract_frame_info(stackResponse[0]).flags & frame_flags::AllErrorFlags)
        {
            if (extract_frame_info(stackResponse[0]).flags & frame_flags::Timeout)
                return MVLCErrorCode::NoVMEResponse;

            if (extract_frame_info(stackResponse[0]).flags & frame_flags::SyntaxError)
                return MVLCErrorCode::StackSyntaxError;

            // Note: BusError can not happen as there's no block read in the stack
        }

        sb = {};
        sb.addWriteMarker(0x13370002u);
    }

    assert(pageIter == pageBuffer.end());

    // Read all the response data. Check the response code and status in the
    // final two words.
    if (useVerbose)
    {
        std::vector<u8> response;

        if (auto ec = read_response(mvlc, moduleBase, response))
            return ec;

        if (response.size() < 2)
            throw std::runtime_error("short response");

        u8 codeStart = *(response.end() - 2);
        u8 status    = *(response.end() - 1);

        if (codeStart != 0xff)
        {
            spdlog::warn("invalid response code start 0x{:02} (expected 0xff)", codeStart);
            throw std::runtime_error("invalid response code");
        }

        if (!(status & 0x01))
        {
            spdlog::warn("instruction failed (status bit 0 not set): 0x{02x}", status);
            throw std::runtime_error("instruction failed");
        }
    }

    if (auto ec = clear_output_fifo(mvlc, moduleBase))
        return ec;

    if (useVerbose)
        if (auto ec = set_verbose_mode(mvlc, moduleBase, false))
            return ec;

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
    spdlog::info("write_page2(): took {} ms to write {} bytes of data",
                 elapsed.count(), pageBuffer.size());

    return {};
}

static const std::vector<u8> make_test_page_incrementing()
{
    std::vector<u8> result;

    for (unsigned i=0; i<PageSize; ++i)
        result.push_back(i);

    return result;
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);

    std::string hostname = "mvlc-0056";
    u32 moduleBase = 0;
    unsigned area = 3;
    unsigned section = 3;
    FlashAddress addr = { 0, 0, 0 };

    try
    {
        auto mvlc = make_mvlc_usb();

        if (auto ec = mvlc.connect())
        {
            spdlog::error("mvlc.connect(): {}", ec.message());
            throw ec;
        }

        if (auto ec = enable_flash_interface(mvlc, moduleBase))
            throw ec;

        if (auto ec = clear_output_fifo(mvlc, moduleBase))
        {
            spdlog::error("clear_output_fifo: {}", ec.message());
            throw ec;
        }

        if (auto ec = set_area_index(mvlc, moduleBase, area))
            throw ec;

        const auto TestPage = make_test_page_incrementing();
        std::vector<u8> pageReadBuffer;

        while (true)
        {
            if (auto ec = erase_section_write_one_page(mvlc, moduleBase, addr, section, TestPage))
                throw ec;

            pageReadBuffer.clear();
            if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, pageReadBuffer))
                    throw ec;

            assert(TestPage == pageReadBuffer);
        }
    }
    catch (const std::error_code &ec)
    {
        spdlog::error("caught std::error_code: {}", ec.message());
        return 1;
    }

    return 0;
}
