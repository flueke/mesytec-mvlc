#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>

// CLI tool showing how to use MVLC stacks to perform many VME writes in a
// single stack transaction. The interesting parts starts below where the
// StackCommandBuilder is created.
//
// A real life implementation can be found in libmvp:
// https://github.com/flueke/libmvp/blob/cc59d93c869e27c6c88685ab11ce91d4cc437fbd/src/mvlc_mvp_lib.cc#L676
// This is used to flash mesytec devices through the VME bus via the MVP protocol.
//
// Note: add -DMVLC_BUILD_DEV_TOOLS=ON to the CMake command line to build this.

using namespace mesytec::mvlc;

void help()
{
    auto helpText = unindent(
R"~(usage: mvlc-vme-write-many --mvlc=<url> <address> <payload> [--log-level=<level>]
                               [--writes-per-transaction=1] [--increment=0] [--total-writes=0]
                               [--log-level=<level>] [--trace] [--debug] [--info] [--warn] [--error]

    --mvlc=<url>                    MVLC URL to connect to, e.g. usb://, mvlc-0124, eth://mvlc-0124
    address                         VME address to write to, e.g. 0xffff2400 to target somewhere in the
                                     middle of the MVLC stack memory area
    payload                         Payload to write, 32-bit unsigned value, e.g. 0x1337cafe or 0x1234
    --writes-per-transaction=<N>    How many writes to perform per stack transaction.
    --increment=<M>                 Increment the target address by this amount after each write.
                                    Defaults to 0 so the same address is written N times.
    --transactions=<N>              Total number of write transactions to perform. 0 means infinite.

    --log-level=<level>             Set the log level, e.g. info, debug, trace, warn, error, off.

  Example: ./mvlc-vme-write-many --mvlc mvlc-0124 0xffff600e 0x1337cafe --writes-per-transaction=600
)~");

    std::cerr << helpText << "\n";
}

int main(int argc, char *argv[])
{
    argh::parser parser;
    parser.add_params({"--mvlc", "--log-level", "--writes-per-transaction", "--increment"});
    parser.parse(argc, argv);
    std::string arg;
    size_t writesPerTransaction = 1;
    size_t increment = 0;
    u32 startAddress = 0u;
    u32 payload = 0u;
    size_t transactions = 0;

    std::string logLevelName("warn");
    if (parser("--log-level") >> logLevelName)
        logLevelName = str_tolower(logLevelName);
    else if (parser["--trace"])
        logLevelName = "trace";
    else if (parser["--debug"])
        logLevelName = "debug";
    else if (parser["--info"])
        logLevelName = "info";
    else if (parser["--warn"])
        logLevelName = "warn";
    else if (parser["--error"])
        logLevelName = "error";

    if (auto logLevel = log_level_from_string(logLevelName))
    {
        spdlog::set_level(*logLevel);
    }
    else
    {
        fmt::print(stderr, "Error: invalid log level '{}'\n", logLevelName);
        return 1;
    }

    if (parser["--help"])
    {
        help();
        return 0;
    }

    if (parser.size() < 3)
    {
        fmt::print(stderr, "Error: not enough arguments provided.\n");
        help();
        return 1;
    }

    auto addrStr = parser[1];
    auto payloadStr = parser[2];

    if (auto val = parse_unsigned<u32>(addrStr))
    {
        startAddress = *val;
    }
    else
    {
        fmt::print(stderr, "Error: invalid address value '{}'\n", addrStr);
        return 1;
    }

    if (auto val = parse_unsigned<u32>(payloadStr))
    {
        payload = *val;
    }
    else
    {
        fmt::print(stderr, "Error: invalid payload value '{}'\n", payloadStr);
        return 1;
    }

    if (parser("--writes-per-transaction") >> arg)
    {
        if (auto val = util::parse_unsigned<size_t>(arg))
            writesPerTransaction = *val;
        else
        {
            fmt::print(stderr, "Error: invalid writes-per-transaction value '{}'\n", arg);
            return 1;
        }
    }

    if (parser("--increment") >> arg)
    {
        if (auto val = util::parse_unsigned<size_t>(arg))
            increment = *val;
        else
        {
            fmt::print(stderr, "Error: invalid increment value '{}'\n", arg);
            return 1;
        }
    }

    if (parser("--transactions") >> arg)
    {
        if (auto val = util::parse_unsigned<size_t>(arg))
            transactions = *val;
        else
        {
            fmt::print(stderr, "Error: invalid --transactions value '{}'\n", arg);
            return 1;
        }
    }

    parser("--mvlc") >> arg;
    auto mvlc = make_mvlc(arg);

    if (!mvlc)
    {
        fmt::print(stderr, "Error: could not create MVLC from URL '{}'\n", arg);
        return 1;
    }

    if (auto ec = mvlc.connect())
    {
        fmt::print(stderr, "Error: could not connect to MVLC: {}\n", ec.message());
        return 1;
    }

    fmt::print("Performing {} writes per stack transaction. targetAddress={:#010x}, payload={:#010x}.\n",
               writesPerTransaction, startAddress, payload);
    fmt::print("Performing {} total transactions, incrementing address by {} after each write.\n",
               transactions > 0 ? std::to_string(transactions) : "∞", increment);

    static const auto ReportInterval = std::chrono::milliseconds(500);
    u32 nextStackReference = 1;
    std::vector<u32> stackResponse;
    util::Stopwatch swReport;

    for (size_t tx=0; transactions == 0 || tx<transactions; ++tx)
    {
        StackCommandBuilder sb;

        // Required: a marker has to be the first word in the stack command
        // list. The libray uses this to match requests and responses.
        sb.addWriteMarker(nextStackReference++);

        // Add the writes. These won't produce any output in the response buffer.
        for (size_t i=0; i<writesPerTransaction; ++i)
        {
            sb.addVMEWrite(startAddress + i * increment, payload, vme_amods::A32, VMEDataWidth::D32);
        }

        // optional: make the MVLC wait for the specified number of cycles (24
        // bit values, 125 MHz clock), e.g. to wait for some write operation to
        // settle.
        //sb.addWait(1000);

        // optional: use the stack accumulator feature to repeatedly poll a
        //   register until it contains 0.
        //   The stack will return a frame with the timeout flag set if the
        //   stack times out while waiting for the condition to become true.
        //   TODO: verify how long the timeout is.
        //sb.addReadToAccu(moduleBase + StatusRegister, vme_amods::A32, VMEDataWidth::D16);
        //sb.addCompareLoopAccu(AccuComparator::EQ, 0);

        // optional: read response data from a fifo. Uses the stack accu to turn
        //   the single cycle vmeRead into a fake block read. The result is
        //   returned in a 0xF5 BlockRead frame.
        //sb.addSetAccu(ExpectedFlashResponseSize+1);
        //sb.addVMERead(moduleBase + OutputFifoRegister, vme_amods::A32, VMEDataWidth::D16);

        auto stackSize = get_encoded_stack_size(sb);

        // Clear the response buffer, then run the transaction. This uploads the
        // stack in potentially multiple outgoing packets/buffers, then executes
        // it using the immediate flag. All output is directed to the command
        // pipe.
        stackResponse.clear();
        if (auto ec = mvlc.stackTransaction(sb, stackResponse))
        {
            fmt::print(stderr, "Error: stack transaction failed: {}\n", ec.message());
            return 1;
        }

        // stackResponse is contained in a 0xF3 StackFrame. 0xF7 StackError
        // notification frames may also be emitted.

        // optional TODO: walk the stackResponse frames to extract results and
        // frame flags. Report timeouts, bus errors, etc.

        if (auto elapsed = swReport.get_interval(); elapsed >= ReportInterval || tx == transactions-1)
        {
            auto totalElapsedSeconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(swReport.get_elapsed())
                    .count();
            auto totaltxPerSecond = tx / totalElapsedSeconds;
            auto totalWrites = tx * writesPerTransaction;
            auto totalWritesPerSecond = totalWrites / totalElapsedSeconds;
            fmt::println("Elapsed: {:.3f} s, {} writes/tx, {} tx, {:.2f} tx/s, {} writes, {:.2f} writes/s, stack size: {} words",
                 totalElapsedSeconds, writesPerTransaction,  tx, totaltxPerSecond, totalWrites, totalWritesPerSecond,
                 stackSize);
            swReport.interval();
        }
    }

    return 0;
}
