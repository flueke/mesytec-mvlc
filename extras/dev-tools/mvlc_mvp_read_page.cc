#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvlc_mvp_lib.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    //get_logger("cmd_pipe_reader")->set_level(spdlog::level::trace);

    std::string hostname = "mvlc-0056";
    u32 moduleBase = 0;
    unsigned area = 3;
    unsigned section = 3;
    FlashAddress addr = { 0, 0, 0 };

    //try
    //{
        //auto mvlc = make_mvlc_eth(hostname);
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

        if (auto ec = set_verbose_mode(mvlc, moduleBase, false))
            throw ec;

        if (auto ec = clear_output_fifo(mvlc, moduleBase))
        {
            spdlog::error("clear_output_fifo: {}", ec.message());
            throw ec;
        }

        if (auto ec = set_area_index(mvlc, moduleBase, area))
            throw ec;

        if (auto ec = clear_output_fifo(mvlc, moduleBase))
        {
            spdlog::error("clear_output_fifo: {}", ec.message());
            throw ec;
        }

        std::vector<u8> pageBuffer; // store processed flash page data

        if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, pageBuffer))
            throw ec;

        log_page_buffer(pageBuffer);

        return 0;

        std::vector<u32> readBuffer; // store raw read data
        pageBuffer.reserve(PageSize);
        readBuffer.clear();
#if 0 // single read variant

        for (int readCycle=0; readCycle<1; ++readCycle)
        {
            readBuffer.clear();
            pageBuffer.clear();

            auto tStart = std::chrono::steady_clock::now();

            // Read the page using single, direct commands. Poll until !read_valid flag is set.
            spdlog::info("Reading flash page");
            mvlc.vmeWrite(moduleBase + InputFifoRegister, 0xB0, vme_amods::A32, VMEDataWidth::D16);
            mvlc.vmeWrite(moduleBase + InputFifoRegister, addr[0], vme_amods::A32, VMEDataWidth::D16);
            mvlc.vmeWrite(moduleBase + InputFifoRegister, addr[1], vme_amods::A32, VMEDataWidth::D16);
            mvlc.vmeWrite(moduleBase + InputFifoRegister, addr[2], vme_amods::A32, VMEDataWidth::D16);
            mvlc.vmeWrite(moduleBase + InputFifoRegister, section, vme_amods::A32, VMEDataWidth::D16);
            mvlc.vmeWrite(moduleBase + InputFifoRegister, 0, vme_amods::A32, VMEDataWidth::D16); // XXX: pagesize

            while (true)
            {
                u32 fifoValue = 0;
                mvlc.vmeRead(moduleBase + OutputFifoRegister, fifoValue, vme_amods::A32, VMEDataWidth::D16);

                readBuffer.push_back(fifoValue);

                //spdlog::info("0x{:04x} = 0x{:08x}", OutputFifoRegister, fifoValue);

                if (fifoValue & (1u << 9)) // Checks for the !read_valid flag to be set.
                    break;

                u8 byteValue = fifoValue & 0xff;
                pageBuffer.push_back(byteValue);
            }

            log_buffer(std::cout, readBuffer, "read buffer", 30, 10);
            log_page_buffer(pageBuffer);

            auto tEnd = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

            spdlog::info("Done reading flash page in {} ms. Received {} bytes.",
                         elapsed.count(),
                         pageBuffer.size());
        }
#elif 0 // stackTransaction with accu variant

        // Read the page using a single stack transaction with a fake block read.
        unsigned bytesToRead = 256;
        const int maxReadCycles = 1000;
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        sb.addVMEWrite(moduleBase + InputFifoRegister, 0xB0, vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, addr[0], vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, addr[1], vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, addr[2], vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, section, vme_amods::A32, VMEDataWidth::D16);
        sb.addVMEWrite(moduleBase + InputFifoRegister, bytesToRead == 256 ? 0 : bytesToRead,
                       vme_amods::A32, VMEDataWidth::D16);

        readBuffer.clear();

        sb.addWait(100000);
        sb.addSetAccu(bytesToRead + 1);
        sb.addVMERead(moduleBase + OutputFifoRegister, vme_amods::A32, VMEDataWidth::D16);

        auto tOuterStart = std::chrono::steady_clock::now();

        for (int readCycle=0; readCycle<maxReadCycles; ++readCycle)
        {
            readBuffer.clear();

            auto tStart = std::chrono::steady_clock::now();

            if (auto ec = mvlc.stackTransaction(sb, readBuffer))
            {
                spdlog::error("mvlc.stackTransaction: {}", ec.message());
                throw ec;
            }

            //log_buffer(std::cout, readBuffer, "stack read buffer");

            fill_page_buffer_from_stack_output(pageBuffer, readBuffer);

            log_page_buffer(pageBuffer);

            auto tEnd = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

            spdlog::info("Done reading flash page in {} ms. Received {} bytes. cycle={}",
                         elapsed.count(),
                         pageBuffer.size(),
                         readCycle
                         );
        }

        if (auto ec = clear_output_fifo(mvlc, moduleBase))
            throw ec;

        auto tOuterEnd = std::chrono::steady_clock::now();
        auto outerElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tOuterEnd - tOuterStart);

        spdlog::info("{} cycles done in {} ms", maxReadCycles, outerElapsed.count());
#else
        pageBuffer.clear();
        if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, pageBuffer))
            throw ec;
        log_page_buffer(pageBuffer);
        spdlog::info("Done reading flash page. Received {} bytes", pageBuffer.size());
        if (auto ec = clear_output_fifo(mvlc, moduleBase))
            throw ec;
#endif
    //}
    //catch (const std::error_code &ec)
    //{
    //    spdlog::error("Caught std::error_code: {}", ec.message());
    //    throw;
    //}

    return 0;
}
