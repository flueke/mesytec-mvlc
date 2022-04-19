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
    unsigned section = 12;

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
            throw ec;

        if (auto ec = set_verbose_mode(mvlc, moduleBase, false))
            throw ec;

        if (auto ec = set_area_index(mvlc, moduleBase, area))
            throw ec;

        spdlog::info("Erasing section {}", section);

        if (auto ec = erase_section(mvlc, moduleBase, section))
            throw ec;
    //}
    //catch (const std::error_code &ec)
    //{
    //    spdlog::error("Caught std::error_code: {}", ec.message());
    //    throw;
    //}

    return 0;
}

