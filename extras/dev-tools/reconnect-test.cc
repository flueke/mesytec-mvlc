#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>

using std::cout;
using std::endl;
using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    //{
    //    auto logger = apiv2::setup_logger();
    //    spdlog::register_logger(logger);
    //}

    //SPDLOG_TRACE("SPDLOG_TRACE");
    //SPDLOG_DEBUG("SPDLOG_INFO");
    //SPDLOG_INFO("SPDLOG_INFO");

    std::string host;
    bool showHelp = false;
    bool logDebug = false;
    bool logTrace = false;
    unsigned secondsToRun = 2;

    auto cli
        = lyra::help(showHelp)
        | lyra::opt(host, "hostname")["--eth"]("mvlc hostname")
        | lyra::opt(logDebug)["--debug"]("enable debug logging")
        | lyra::opt(logTrace)["--trace"]("enable trace logging")
        | lyra::arg(secondsToRun, "secondsToRun")
        ;
    auto parseResult = cli.parse({ argc, argv});

    if (!parseResult)
        return 1;

    if (showHelp)
    {
        cout << cli << "\n";
        return 0;
    }

    if (logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (logTrace)
        spdlog::set_level(spdlog::level::trace);

    std::unique_ptr<MVLCBasicInterface> impl;

    if (host.empty())
    {
        impl = std::make_unique<usb::Impl>();
    }
    else
    {
        impl = std::make_unique<eth::Impl>(host);
    }

    assert(impl);

    MVLC mvlc(std::move(impl));

    mvlc.setDisableTriggersOnConnect(true);

    try
    {
        // first connection attempt

        if (auto ec = mvlc.connect())
        {
            spdlog::error("First connection attempt: {}", ec.message());
            throw ec;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (auto ec = mvlc.disconnect())
        {
            spdlog::error("First disconnect attempt: {}", ec.message());
            throw ec;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));


        // second connection attempt

        if (auto ec = mvlc.connect())
        {
            spdlog::error("Second connection attempt: {}", ec.message());
            throw ec;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (auto ec = mvlc.disconnect())
        {
            spdlog::error("Second disconnect attempt: {}", ec.message());
            throw ec;
        }

    }
    catch (const std::error_code &ec)
    {
        cout << "Error: " << ec.message() << "\n";
        throw;
    }

    spdlog::info("end of main()");
}
