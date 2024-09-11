#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>
#include <signal.h>
#include <system_error>

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    setup_signal_handlers();

    spdlog::set_level(spdlog::level::info);
    mesytec::mvlc::set_global_log_level(spdlog::level::info);

    if (argc < 2)
    {
        std::cerr << "Error: No MVLC address provided.\n";
        return 1;
    }

    argh::parser parser({ "--mvlc", "--log-level"});
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = util::str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";
        else if (parser["--warn"])
            logLevelName = "warn";

        if (!logLevelName.empty())
        {
            spdlog::set_level(spdlog::level::from_str(logLevelName));
            mesytec::mvlc::set_global_log_level(spdlog::level::from_str(logLevelName));
        }
    }

    if (parser.pos_args().size() < 2)
    {
        std::cerr << "Error: No CrateConfig YAML file provided.\n";
        return 1;
    }

    auto crateConfigFile = parser.pos_args()[1];

    try
    {
        auto crateConfig = crate_config_from_yaml_file(crateConfigFile);
        MVLC mvlc;

        std::string mvlcUrl;
        if (parser("--mvlc") >> mvlcUrl)
            mvlc = make_mvlc(mvlcUrl);
        else
            mvlc = make_mvlc(crateConfig);

        if (parser["--mvlc-force-connect"])
            mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
            throw std::system_error(ec, "mvlc.connect()");

        util::Stopwatch swReport;
        size_t cycleNumber = 0;
        size_t lastCycleNumber = 0;

        while (!signal_received())
        {
            auto initResults = init_readout(mvlc, crateConfig, {});

            if (initResults.ec)
            {
                std::cerr << fmt::format("Init Cycle #{}: Error from DAQ init sequence: {}\n",
                    cycleNumber, initResults.ec.message());
                break;
            }

            auto mcstStartResults = run_commands(mvlc, crateConfig.mcstDaqStart);

            if (auto ec = get_first_error(mcstStartResults))
            {
                std::cerr << fmt::format("Init Cycle #{}: Error from MCST DAQ start: {}\n",
                    cycleNumber, ec.message());
                break;
            }

            for (const auto &result: mcstStartResults)
                spdlog::debug("  {}: {}", to_string(result.cmd), result.ec.message());


            auto mcstStopResults = run_commands(mvlc, crateConfig.mcstDaqStop);

            if (auto ec = get_first_error(mcstStopResults))
            {
                std::cerr << fmt::format("Init Cycle #{}: Error from MCST DAQ stop: {}\n",
                    cycleNumber, ec.message());
                break;
            }

            for (const auto &result: mcstStopResults)
                spdlog::debug("  {}: {}", to_string(result.cmd), result.ec.message());

            if (auto elapsed = swReport.get_interval(); elapsed >= std::chrono::seconds(1))
            {
                auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(swReport.get_elapsed());
                auto cycles = cycleNumber - lastCycleNumber;
                lastCycleNumber = cycleNumber;
                auto cyclesPerSecond = cycles / std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
                fmt::print("Elapsed: {} s, Init Cycle #{}, {:.2} cycles/s\n", totalElapsed.count(), cycleNumber, cyclesPerSecond);
                swReport.interval();
            }

            ++cycleNumber;
        }


        return 0;
    }
    catch (const std::system_error &e)
    {
        std::cerr << "System error: " << e.what() << '\n';
        return 1;
    }
    catch(const std::exception& e)
    {
        std::cerr << "Exception:" << e.what() << '\n';
        return 1;
    }

    return 0;
}
