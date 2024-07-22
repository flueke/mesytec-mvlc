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

    argh::parser parser({"--register-address", "--register-value", "--log-level"});
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

    std::string str;
    u16 registerAddress = 0x600E; // fw revision register. no effect when written to
    if (parser("--register-address") >> str)
    {
        if (auto val = parse_unsigned<u16>(str))
            registerAddress = *val;
    }

    u32 registerValue = 1;
    if (parser("--register-value") >> str)
    {
        if (auto val = parse_unsigned<u32>(str))
            registerValue = *val;
    }

    auto mvlcUrl = parser.pos_args()[1];
    auto mvlc = make_mvlc(mvlcUrl);

    if (auto ec = mvlc.connect())
    {
        std::cerr << fmt::format("Error connecting to mvlc '{}': {}\n", mvlcUrl, ec.message());
        return 1;
    }

    util::Stopwatch swReport;
    size_t cycleNumber = 0;
    size_t lastCycleNumber = 0;

    while (!signal_received())
    {
        if (auto ec = mvlc.writeRegister(registerAddress, registerValue))
        {
            std::cerr << fmt::format("Error writing register 0x{:04x}: {}\n", registerAddress, ec.message());
            return 1;
        }

        u32 registerReadValue = 0u;

        if (auto ec = mvlc.readRegister(registerAddress, registerReadValue))
        {
            std::cerr << fmt::format("Error reading register 0x{:04x}: {}\n", registerAddress, ec.message());
            return 1;
        }

        if (auto elapsed = swReport.get_interval(); elapsed >= std::chrono::seconds(1))
        {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(swReport.get_elapsed());
            auto cycles = cycleNumber - lastCycleNumber;
            lastCycleNumber = cycleNumber;
            auto cyclesPerSecond = cycles / std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            spdlog::info("Elapsed: {} s, Cycle: {}; {:.2} rw cycles/s", totalElapsed.count(), cycleNumber, cyclesPerSecond);
            swReport.interval();
        }

        ++cycleNumber;
    }

    return 0;
}
