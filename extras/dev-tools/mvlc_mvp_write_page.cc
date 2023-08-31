#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_mvp_lib.h>

using namespace mesytec::mvlc;

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

        if (auto ec = set_verbose_mode(mvlc, moduleBase, false))
            throw ec;

        if (auto ec = enable_flash_write(mvlc, moduleBase))
            throw ec;

        std::vector<u8> writePage;

        for (unsigned i=0; i<PageSize; ++i)
            writePage.push_back(i);

        if (auto ec = write_page2(mvlc, moduleBase, addr, section, writePage))
            throw ec;

        std::vector<u8> readPage;

        if (auto ec = read_page(mvlc, moduleBase, addr, section, PageSize, readPage))
            throw ec;

        log_page_buffer(readPage);
    }
    catch (const std::error_code &ec)
    {
        spdlog::error("caught std::error_code: {}", ec.message());
        return 1;
    }

    return 0;
}
