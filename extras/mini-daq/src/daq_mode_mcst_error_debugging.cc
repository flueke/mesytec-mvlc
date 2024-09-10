#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

const char *CrateConfigFile = "usb-mcst-daq-start-bug_mdpp.yaml";

enum class DataReaderAction
{
    Run,
    Quit,
    QuitWhenEmpty
};

std::future<size_t> start_usb_data_reader(usb::MVLC_USB_Interface *mvlc, std::atomic<DataReaderAction> &action_)
{
    return std::async(
        std::launch::async, [&] ()
        {
            spdlog::info("begin reading from data pipe");
            const size_t DataBufferSize = usb::USBStreamPipeReadSize;
            std::array<u8, DataBufferSize> buffer;
            size_t totalBytesTransferred = 0;

            while (true)
            {
                auto action = action_.load(std::memory_order_relaxed);

                if (action == DataReaderAction::Quit)
                    break;

                size_t bytesTransferred = 0;
                auto ec = mvlc->read_unbuffered(Pipe::Data, buffer.data(), buffer.size(), bytesTransferred);
                totalBytesTransferred += bytesTransferred;
                spdlog::debug("read from data pipe result: ec={}, bytesTransferred={}", ec.message(), bytesTransferred);

                if (ec == ErrorType::ConnectionError)
                {
                    spdlog::error("read from data type got a connection error: {}, quitting", ec.message());
                    break;
                }

                if (action == DataReaderAction::QuitWhenEmpty && bytesTransferred == 0)
                {
                    spdlog::info("usb_data_reader: QuitWhenEmpty satisfied, quitting");
                    break;
                }
            }

            spdlog::info("stopped reading from data pipe");
            return totalBytesTransferred;
        });
};

int main(int argc, char *argv[])
{
    set_global_log_level(spdlog::level::trace);

    CrateConfig crateConfig = {};

    {
        std::ifstream configIn(CrateConfigFile);

        if (!configIn.is_open())
        {
            spdlog::error("Error opening crate config {} for reading", CrateConfigFile);
            return 1;
        }

        try
        {
            crateConfig = crate_config_from_yaml(configIn);
        }
        catch (const std::runtime_error &e)
        {
            spdlog::error("Error parsing crateconfig from {}: {}", CrateConfigFile, e.what());
            return 1;
        }
    }

    auto mvlc = make_mvlc_usb();

    if (auto ec = mvlc.connect())
    {
        return 1;
    }

    while (true)
    {
        spdlog::info("mvlc.connect() done, running readout init sequence");

        set_global_log_level(spdlog::level::debug);
#if 0
        auto initResults = init_readout(mvlc, crateConfig);
        if (initResults.ec)
        {
            spdlog::error("Error running daq init sequence: {}", initResults.ec.message());
            return 1;
        }
#else
        {
            if (auto ec = disable_daq_mode_and_triggers(mvlc))
            {
                spdlog::error("Error from disable_daq_mode_and_triggers: {}", ec.message());
                return 1;
            }
            else
                spdlog::info("disable_daq_mode_and_triggers() done");
        }
        {
            if (auto ec = get_first_error(run_commands(mvlc, crateConfig.initTriggerIO)))
            {
                spdlog::error("Error running initTriggerIO commands: {}", ec.message());
                return 1;
            }
            else
                spdlog::info("initTriggerIO done");
        }
        {
            if (auto ec = get_first_error(run_commands(mvlc, crateConfig.initCommands)))
            {
                spdlog::error("Error running initCommands commands: {}", ec.message());
                return 1;
            }
            else
                spdlog::info("initCommands done");
        }
        {
            if (auto ec = setup_readout_stacks(mvlc, crateConfig.stacks))
            {
                spdlog::error("Error setting up readout stacks: {}", ec.message());
                return 1;
            }
            else
                spdlog::info("readout stacks setup");
        }
#endif
        if (auto ec = setup_readout_triggers(mvlc, crateConfig.triggers))
        {
            spdlog::error("Error from setup_readout_triggers(): {}", ec.message());
            return 1;
        }
        else
            spdlog::info("setup_readout_triggers done");

        auto usb = dynamic_cast<usb::MVLC_USB_Interface *>(mvlc.getImpl());
        assert(usb);

        spdlog::info("starting data reader before enabling daq mode");
        std::atomic<DataReaderAction> readerAction{DataReaderAction::Run};
        auto fReader = start_usb_data_reader(usb, readerAction);

        if (auto ec = enable_daq_mode(mvlc))
        {
            spdlog::error("Error enabling MVLC DAQ mode: {}", ec.message());
            return 1;
        }
        else
            spdlog::info("enable_daq_mode done");

        set_global_log_level(spdlog::level::debug);
        std::error_code ec = {};
        while (true)
        {
            spdlog::info("running mcst daq start commands");
            ec = get_first_error(run_commands(mvlc, crateConfig.mcstDaqStart));

            if (ec && ec == ErrorType::ConnectionError)
            {
                spdlog::error("ConnectionError from running mcst daq start commands: {}", ec.message());
                break;
            }
            else if (ec)
            {
                spdlog::warn("Error from running mcst daq start commands: {}, retrying", ec.message());
            }
            else
            {
                spdlog::info("Ran mcst daq start commands");
                break;
            }
        }

        if (ec)
        {
            spdlog::error("Error from daq start: {}", ec.message());
            readerAction = DataReaderAction::QuitWhenEmpty;
            size_t totalDataBytes = fReader.get();
            spdlog::info("data reader received a total of {} bytes ({} words)",
                         totalDataBytes, totalDataBytes/sizeof(u32));
            return 1;
        }

        spdlog::info("Sleeping while reading data...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        spdlog::info("sleep is done, stopping readout");

        while (true)
        {
            spdlog::info("running mcst daq stop commands");
            ec = get_first_error(run_commands(mvlc, crateConfig.mcstDaqStop));

            if (ec && ec == ErrorType::ConnectionError)
            {
                spdlog::error("ConnectionError from running mcst daq stop commands: {}", ec.message());
                break;
            }
            else if (ec)
            {
                spdlog::warn("Error from running mcst daq stop commands: {}, retrying", ec.message());
            }
            else
            {
                spdlog::info("Ran mcst daq stop commands");
                break;
            }
        }

        spdlog::info("disabling triggers and daq mode");

        for (int try_ = 0; try_ < 5; try_++)
        {
            if (auto ec = disable_daq_mode_and_triggers(mvlc))
            {
                spdlog::warn("error from disable_daq_mode_and_triggers: {}", ec.message());
            }
            else break;
        }

        spdlog::info("sleeping a bit more");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        spdlog::info("sleep is done, stopping data reader");
        readerAction = DataReaderAction::QuitWhenEmpty;
        size_t totalDataBytes = fReader.get();
        spdlog::info("data reader received a total of {} bytes ({} words)",
                     totalDataBytes, totalDataBytes/sizeof(u32));
    }

    spdlog::info("done");
}
