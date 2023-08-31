#include <algorithm>
#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_mvp_lib.h>

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
    unsigned area = 3;
    unsigned section = 3; // 3=calib, 12=firmware
    FlashAddress addr = { 0, 0, 0 };
    static const size_t MaxLoops = 100;

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

        const auto ErasedPage = make_erased_page();
        const auto TestPage = make_test_page_incrementing();
        std::vector<u8> pageReadBuffer;

        for (size_t loop=0; loop<MaxLoops; ++loop)
        {
            // erase
            if (auto ec = enable_flash_write(mvlc, moduleBase))
                throw ec;

            if (auto ec = erase_section(mvlc, moduleBase, section))
                throw ec;

            // read erased and verify
            pageReadBuffer.clear();

            if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, pageReadBuffer))
                throw ec;

            if (ErasedPage != pageReadBuffer)
            {
                spdlog::error("Unexpected page contents after erasing");
                log_page_buffer(pageReadBuffer);
                break;
            }

            // write TestPage
            if (auto ec = enable_flash_write(mvlc, moduleBase))
                throw ec;

            if (auto ec = write_page(mvlc, moduleBase, addr, section, TestPage))
            {
                spdlog::error("Error writing page: {}", ec.message());
                break;
            }

            // read page and verify
            pageReadBuffer.clear();

            if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, pageReadBuffer))
                throw ec;

            if (TestPage != pageReadBuffer)
            {
                spdlog::error("Unexpected page contents after writing");
                log_page_buffer(pageReadBuffer);
                break;
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
