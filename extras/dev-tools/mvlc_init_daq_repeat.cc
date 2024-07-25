#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>
#include <signal.h>
#include <system_error>

using namespace mesytec::mvlc;

static std::atomic<bool> signal_received_ = false;

void signal_handler(int signum)
{
    signal_received_ = true;
}

void setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
}

bool signal_received()
{
    return signal_received_;
}

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

    argh::parser parser({ "--test-type", "--register-address", "--register-value", "--log-level"});
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

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
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
                std::cerr << fmt::format("Cycle #{}: Error from DAQ init sequence: {}\n",
                    cycleNumber, initResults.ec.message());
                break;
            }

            if (auto elapsed = swReport.get_interval(); elapsed >= std::chrono::seconds(1))
            {
                auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(swReport.get_elapsed());
                auto cycles = cycleNumber - lastCycleNumber;
                lastCycleNumber = cycleNumber;
                auto cyclesPerSecond = cycles / std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
                spdlog::info("Elapsed: {} s, Cycle Number: {}; {:.2} cycles/s", totalElapsed.count(), cycleNumber, cyclesPerSecond);
                swReport.interval();
            }

            ++cycleNumber;
        }


        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "Exception:" << e.what() << '\n';
        return 1;
    }
    catch (const std::system_error &e)
    {
        std::cerr << "System error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
