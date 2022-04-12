#include <algorithm>
#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvlc_mvp_lib.h"

using namespace mesytec::mvlc;

static const std::vector<u8> make_erased_page()
{
    return std::vector<u8>(PageSize, 0xff);
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
    unsigned area = 3; // does not matter for the calib section
    static const unsigned CalibSection = 3;
    static const unsigned CalibSectors = 8;
    static const unsigned CalibPages = CalibSectors * PagesPerSector;
    static const size_t MaxLoops = 100;

    const auto ErasedPage = make_erased_page();
    auto TestPage = make_test_page_incrementing();

    // Assumes test pattern has been written previously by
    // mvlc_mvp_flash_write_test2
    // loop:
    //  - verify all pages in the calib section to be equal to the test pattern

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

        if (auto ec = set_verbose_mode(mvlc, moduleBase, false))
            throw ec;

        if (auto ec = set_area_index(mvlc, moduleBase, area))
            throw ec;

        std::vector<u8> pageReadBuffer;

        for (size_t testLoop=0; testLoop<MaxLoops; ++testLoop)
        {
            // verify all pages in the calib section
            for (size_t pageIndex=0; pageIndex<CalibPages; ++pageIndex)
            {
                u32 byteOffset = pageIndex * PageSize;
                auto addr = flash_address_from_byte_offset(byteOffset);


                spdlog::info("Reading and verifying page {} of {}",
                             pageIndex+1, CalibPages);
                pageReadBuffer.clear();
                if (auto ec = read_page(mvlc, moduleBase, addr, CalibSection, PageSize, pageReadBuffer))
                    throw ec;

                // Again set the first bytes to the current page address, then
                // verify the contents match.
                std::copy(addr.begin(), addr.end(), TestPage.begin());

                if (TestPage != pageReadBuffer)
                {
                    spdlog::error("Unexpected page contents");
                    log_page_buffer(pageReadBuffer);
                    break;
                }
            }
        }
    }
    catch (const std::error_code &ec)
    {
        spdlog::error("caught std::error_code: {}", ec.message());
        return 1;
    }

    return 0;
}

