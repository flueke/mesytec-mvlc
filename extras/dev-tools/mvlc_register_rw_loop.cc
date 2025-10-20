#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>
#include <signal.h>
#include <system_error>

using namespace mesytec::mvlc;

std::error_code do_single_register(MVLC &mvlc, u16 registerAddress, u32 registerValue)
{
    if (auto ec = mvlc.writeRegister(registerAddress, registerValue))
    {
        std::cerr << fmt::format("Error writing register 0x{:04x}: {}\n", registerAddress, ec.message());
        return ec;
    }

    u32 registerReadValue = 0u;

    if (auto ec = mvlc.readRegister(registerAddress, registerReadValue))
    {
        std::cerr << fmt::format("Error reading register 0x{:04x}: {}\n", registerAddress, ec.message());
        return ec;
    }

    return {};
}

#if 0

/*
# All available signal alias registers since MVLC firmware 0x0023.

writeabs A32 D16 0xFFFF7000 0      # triva trigger number
writeabs A32 D16 0xFFFF7002 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF7004 0      # triva trigger number
writeabs A32 D16 0xFFFF7006 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF7008 0      # triva trigger number
writeabs A32 D16 0xFFFF700A 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF700C 0      # triva trigger number
writeabs A32 D16 0xFFFF700E 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF7010 0      # triva trigger number
writeabs A32 D16 0xFFFF7012 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF7014 0      # triva trigger number
writeabs A32 D16 0xFFFF7016 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF7018 0      # triva trigger number
writeabs A32 D16 0xFFFF701A 0      # mapped to mvlc irq

writeabs A32 D16 0xFFFF701C 0      # triva trigger number
writeabs A32 D16 0xFFFF701E 0      # mapped to mvlc irq
*/
#endif

// Write patterns to a sequence of registers, then read back and verify the values.
// Note: seems for these signaling registers only 16 valid bits are implemented
// in the MVLC.
std::error_code do_memory_block(MVLC &mvlc)
{
    // IRQ signal alias setup registers. The trigger number registers can be
    // written to and read back. The irq mapping registers can be written to,
    // but they always return 0 when read. This test only writes and reads
    // to/from the trigger number registers.
    // writeabs A32 D16 0xFFFF7000 0      # triva trigger number
    // writeabs A32 D16 0xFFFF7002 0      # mapped to mvlc irq
    const u16 memStart = 0x7000;
    const u16 memAddresses = 8;
    const u16 addrIncr = 4;

    for (size_t i=0; i < memAddresses; ++i)
    {
        u16 addr = memStart + addrIncr * i;
        u32 value = i % 2 == 0 ? 0xaa00 : 0x5500;
        value |= i;
        if (auto ec = mvlc.writeRegister(addr, value))
        {
            std::cerr << fmt::format("Error writing register 0x{:04x}: {}\n", addr, ec.message());
            return ec;
        }
    }

    for (size_t i=0; i < memAddresses; ++i)
    {
        u16 addr = memStart + addrIncr * i;
        u32 value = i % 2 == 0 ? 0xaa00 : 0x5500;
        value |= i;

        u32 readBack = 0;
        if (auto ec = mvlc.readRegister(addr, readBack))
        {
            std::cerr << fmt::format("Error reading register 0x{:04x}: {}\n", addr, ec.message());
            return ec;
        }

        if (readBack != value)
        {
            std::cerr << fmt::format("Error: Read back value 0x{:08x} does not match written value 0x{:08x} at address 0x{:04x}\n",
                                     readBack, value, addr);
            return std::make_error_code(std::errc::protocol_error);
        }
    }

    return {};
}

int main(int argc, char *argv[])
{
    util::setup_signal_handlers();

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
        else if (parser["--warn"])
            logLevelName = "warn";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    enum TestType
    {
        SingleRegister,
        MemoryBlock,
    };

    TestType testType = SingleRegister;

    std::string str;

    if (parser("--test-type") >> str)
    {
        if (str == "single-register")
            testType = SingleRegister;
        else if (str == "memory-block")
            testType = MemoryBlock;
        else
        {
            std::cerr << "Error: Invalid test type '" << str << "'.\n";
            return 1;
        }
    }

    u16 registerAddress = 0x600E; // fw revision register. no effect when written to
    if (parser("--register-address") >> str)
    {
        if (auto val = util::parse_unsigned<u16>(str))
            registerAddress = *val;
    }

    u32 registerValue = 1;
    if (parser("--register-value") >> str)
    {
        if (auto val = util::parse_unsigned<u32>(str))
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
    size_t totalReads = 0;
    size_t totalWrites = 0;

    while (!util::signal_received())
    {
        switch (testType)
        {
            case SingleRegister:
                if (auto ec = do_single_register(mvlc, registerAddress, registerValue))
                    return 1;
                totalReads += 1;
                totalWrites += 1;
                break;

            case MemoryBlock:
                if (auto ec = do_memory_block(mvlc))
                    return 1;
                totalReads += 8;
                totalWrites += 8;
                break;
        }

        if (auto elapsed = swReport.get_interval(); elapsed >= std::chrono::seconds(1))
        {
            auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(swReport.get_elapsed());
            auto cycles = cycleNumber - lastCycleNumber;
            lastCycleNumber = cycleNumber;
            auto cyclesPerSecond = cycles / std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            auto writesPerSecond = totalWrites / std::chrono::duration_cast<std::chrono::duration<double>>(totalElapsed).count();
            auto readsPerSecond = totalReads / std::chrono::duration_cast<std::chrono::duration<double>>(totalElapsed).count();
            fmt::print("Elapsed: {} s, Cycle Number: {}; {:.2} cycles/s, reads/s={:.2}, writes/s={:.2}\n",
                 totalElapsed.count(), cycleNumber, cyclesPerSecond , readsPerSecond, writesPerSecond);
            swReport.interval();
        }

        ++cycleNumber;
    }

    return 0;
}
