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
    static const size_t MaxLoops = 1;

    const auto ErasedPage = make_erased_page();
    const auto TestPage = make_test_page_incrementing();

    // TODO:
    // - erase calib section
    // - verify all pages in calib section to be 0xff
    // - write all pages in the calib section using some test pattern
    // - verify all pages in the calib section to be equal to the test pattern

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
            // erase
            if (auto ec = enable_flash_write(mvlc, moduleBase))
                throw ec;

            if (auto ec = erase_section(mvlc, moduleBase, CalibSection))
                throw ec;

            // read and verify the whole calib section has been erased
            for (size_t pageIndex=0; pageIndex<CalibPages; ++pageIndex)
            {
                u32 byteOffset = pageIndex * PageSize;
                FlashAddress addr =
                {
                    static_cast<u8>((byteOffset & 0x0000ff) >>  0),
                    static_cast<u8>((byteOffset & 0x00ff00) >>  8),
                    static_cast<u8>((byteOffset & 0xff0000) >> 16),
                };

                pageReadBuffer.clear();

                spdlog::info("Reading and verifying page {} of {}",
                             pageIndex+1, CalibPages);
                if (auto ec = read_page(mvlc, moduleBase, addr, CalibSection, PageSize, pageReadBuffer))
                    throw ec;

                if (ErasedPage != pageReadBuffer)
                {
                    spdlog::error("Unexpected page contents after erasing");
                    log_page_buffer(pageReadBuffer);
                    break;
                }
            }

            // write test data to all pages in the calib section
            for (size_t pageIndex=0; pageIndex<CalibPages; ++pageIndex)
            {
                u32 byteOffset = pageIndex * PageSize;
                FlashAddress addr =
                {
                    static_cast<u8>((byteOffset & 0x0000ff) >>  0),
                    static_cast<u8>((byteOffset & 0x00ff00) >>  8),
                    static_cast<u8>((byteOffset & 0xff0000) >> 16),
                };

                if (auto ec = enable_flash_write(mvlc, moduleBase))
                    throw ec;

                spdlog::info("Writing page {} of {}, addr={:02x}",
                             pageIndex+1, CalibPages, fmt::join(addr, ", "));
                if (auto ec = write_page(mvlc, moduleBase, addr, CalibSection, TestPage))
                {
                    spdlog::error("Error writing page: {}", ec.message());
                    break;
                }
            }

            // verify all pages in the calib section
            for (size_t pageIndex=0; pageIndex<CalibPages; ++pageIndex)
            {
                u32 byteOffset = pageIndex * PageSize;
                FlashAddress addr =
                {
                    static_cast<u8>((byteOffset & 0x0000ff) >>  0),
                    static_cast<u8>((byteOffset & 0x00ff00) >>  8),
                    static_cast<u8>((byteOffset & 0xff0000) >> 16),
                };

                pageReadBuffer.clear();

                spdlog::info("Reading and verifying page {} of {}",
                             pageIndex+1, CalibPages);
                if (auto ec = read_page(mvlc, moduleBase, addr, CalibSection, PageSize, pageReadBuffer))
                    throw ec;

                if (TestPage != pageReadBuffer)
                {
                    spdlog::error("Unexpected page contents after writing test pages");
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
