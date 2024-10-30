// Fore some info on how to use argh effectively see
// https://a4z.gitlab.io/blog/2019/04/30/Git-like-sub-commands-with-argh.html

#include <argh.h>
#include <string>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/emittermanip.h>

using namespace mesytec::mvlc;

std::pair<MVLC, std::error_code> make_and_connect_default_mvlc(argh::parser &parser)
{
    // try the standard params first
    auto mvlc = make_mvlc_from_standard_params(parser);

    if (!mvlc)
    {
        // try via the env variable
        if (char *envAddr = std::getenv("MVLC_ADDRESS"))
            mvlc = make_mvlc(envAddr);
    }

    if (!mvlc)
    {
        std::cerr << "Error: no MVLC to connect to\n";
        return std::pair<MVLC, std::error_code>();
    }

    if (parser["--mvlc-force-connect"])
        mvlc.setDisableTriggersOnConnect(true);

    auto ec = mvlc.connect();

    if (ec)
    {
        std::cerr << fmt::format("Error connecting to MVLC {}: {}\n",
            mvlc.connectionInfo(), ec.message());
    }

    return std::make_pair(mvlc, ec);
}

struct CliContext;
struct Command;

#define DEF_EXEC_FUNC(name) int name(CliContext &ctx, const Command &self, int argc, const char **argv)

using Exec = std::function<DEF_EXEC_FUNC()>;

struct Command
{
  std::string name ;
  std::string help;
  Exec exec;
};

struct CommandNameLessThan
{
    bool operator()(const Command &first, const Command &second) const
    {
        return first.name < second.name;
    }
};

using Commands = std::set<Command, CommandNameLessThan> ;

struct CliContext
{
    std::string generalHelp;
    Commands commands;
    argh::parser parser;
};

DEF_EXEC_FUNC(help_command)
{
    spdlog::trace("entered help_command()");
    trace_log_parser_info(ctx.parser, "help_command");

    if (ctx.parser["-a"] || ctx.parser["--all"])
    {
        if (!ctx.parser[2].empty())
        {
            std::cerr << "Error: the '--all' option doesn't take any non-option arguments\n";
            return 1;
        }

        if (auto cmd = ctx.commands.find(Command{"list-commands"});
            cmd != ctx.commands.end())
        {
            return cmd->exec(ctx, *cmd, argc, argv);
        }

        std::cerr << "Error: 'list-commands' command not found\n";
        return 1;
    }

    if (auto cmd = ctx.commands.find(Command{ctx.parser[2]});
        cmd != ctx.commands.end())
    {
        std::cout << cmd->help;
        return 0;
    }

    std::cerr << fmt::format("Error: no such command '{}'\n", ctx.parser[2]);
    return 1;
}

static const Command HelpCommand =
{
    .name = "help",
    .help = R"~(This is the help text for the 'help' command
)~",
    .exec = help_command,
};

DEF_EXEC_FUNC(list_commands_command)
{
    spdlog::trace("entered list_commands_command()");
    trace_log_parser_info(ctx.parser, "list_commands_command");

    for (const auto &cmd: ctx.commands)
    {
        std::cout << cmd.name << "\n";
    }

    return 0;
}

static const Command ListCmdsCommand =
{
    .name = "list-commands",
    .help = R"~(Meta command to list all registered commands.
)~",
    .exec = list_commands_command,
};

DEF_EXEC_FUNC(mvlc_version_command)
{
    spdlog::trace("entered mvlc_version_command()");
    trace_log_parser_info(ctx.parser, "mvlc_version_command");

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    std::cout << fmt::format("{}, hardwareId=0x{:04x}, firmwareRevision=0x{:04x}\n",
        mvlc.connectionInfo(), mvlc.hardwareId(), mvlc.firmwareRevision());

    return 0;
}

static const Command MvlcVersionCommand =
{
    .name = "version",
    .help = R"~(print MVLC hardware and firmware revisions
)~",
    .exec = mvlc_version_command,
};

DEF_EXEC_FUNC(mvlc_stack_info_command)
{
    spdlog::trace("entered mvlc_stack_info_command()");
    trace_log_parser_info(ctx.parser, "mvlc_stack_info_command");

    static const unsigned AllStacks = std::numeric_limits<unsigned>::max();
    unsigned stackId = AllStacks;

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    const auto StackCount = mvlc.getStackCount();

    if (!ctx.parser[2].empty())
    {
        if (!(ctx.parser(2) >> stackId))
        {
            std::cerr << "Error: invalid stackId given\n";
            return 1;
        }

        if (stackId >= mvlc.getStackCount())
        {
            std::cerr << fmt::format("Error: stackId={} is out of range\n", stackId);
            return 1;
        }
    }

    const unsigned stackMin = stackId >= StackCount ? 0 : stackId;
    const unsigned stackMax = stackId >= StackCount ? StackCount : stackId + 1;

    spdlog::trace("stack_info: stackMin={}, stackMax={}", stackMin, stackMax);

    struct StackInfoEntry
    {
        unsigned stackId;
        std::error_code ec;
        StackInfo stackInfo;
    };

    std::vector<StackInfoEntry> stackInfos;

    for (unsigned stackId=stackMin; stackId<stackMax; ++stackId)
    {
        auto [stackInfo, ec] = read_stack_info(mvlc, stackId);

        StackInfoEntry e;
        e.stackId = stackId;
        e.ec = ec;
        e.stackInfo = stackInfo;

        stackInfos.emplace_back(e);

        if (ec && ec != make_error_code(MVLCErrorCode::InvalidStackHeader))
        {
            std::cerr << fmt::format("Error reading stack info for stack#{}: {}\n",
                stackId, ec.message());
            return 1;
        }
    }

    for (const auto &entry: stackInfos)
    {
        const auto &stackId = entry.stackId;
        const auto &ec = entry.ec;
        const auto &stackInfo = entry.stackInfo;
        stacks::Trigger trigger{};
        trigger.value = stackInfo.triggerValue;

        if (ec == make_error_code(MVLCErrorCode::InvalidStackHeader))
        {

            std::cerr << fmt::format("- stack#{:2} (trig@0x{:04x}, off@0x{:04x}): triggers=0x{:02x} ({}), offset={}, startAddress=0x{:04x}"
                ", empty stack (does not start with a StackStart (0xF3) header)\n",
                stackId, stackInfo.triggerAddress, stackInfo.offsetAddress,
                 (u16)trigger.value,
                 trigger_to_string(trigger),
                stackInfo.offset, stackInfo.startAddress);
            continue;
        }
        else if (!ec)
        {
            std::cout << fmt::format("- stack#{:2} (trig@0x{:04x}, off@0x{:04x}): triggers=0x{:02x} ({}), offset={}, startAddress=0x{:04x}, len={}:\n",
                stackId, stackInfo.triggerAddress, stackInfo.offsetAddress,
                (u16) trigger.value,
                trigger_to_string(trigger),
                stackInfo.offset, stackInfo.startAddress,
                stackInfo.contents.size());

            for (auto &word: stackInfo.contents)
                std::cout << fmt::format("  0x{:08x}\n", word);
            std::cout << "--\n";

            auto stackBuilder = stack_builder_from_buffer(stackInfo.contents);
            for (const auto &cmd: stackBuilder.getCommands())
            {
                std::cout << "  " << to_string(cmd) << "\n";
            }
            std::cout << "\n";
        }
        else
        {
            std::cout << fmt::format("Error reading stack info for stack#{}: {}\n", stackId, ec.message());
            return 1;
        }
    }

    return 0;
}

static const Command MvlcStackInfoCommand =
{
    .name = "stack_info",
    .help = unindent(
//R"~(usage: mvlc-cli stack_info [--raw] [--yaml] [<stackId>]
R"~(usage: mvlc-cli stack_info [<stackId>]

    Read and print command stack info and contents. If no stackId is given all event readout
    stacks (stack0..15) are read.

options:

    <stackId>       Optional numeric stack id. Range 0..15.
)~"),
    .exec = mvlc_stack_info_command,
};

DEF_EXEC_FUNC(scanbus_command)
{
    spdlog::trace("entered mvlc_scanbus_command()");

    auto &parser = ctx.parser;
    parser.add_params({"--scan-begin", "--scan-end", "--probe-register", "--probe-amod", "--probe-datawidth", "--stack-max-words"});
    parser.parse(argv);
    trace_log_parser_info(parser, "mvlc_scanbus_command");

    u16 scanBegin = 0x0u;
    u16 scanEnd   = 0xffffu;
    u16 probeRegister = 0;
    u8  probeAmod = 0x09;
    VMEDataWidth probeDataWidth = VMEDataWidth::D16;
    // Maximum number of words to use for scanbus command stacks.
    u16 stackMaxWords = stacks::ImmediateStackReservedWords;
    std::string str;

    if (parser("--scan-begin") >> str)
    {
        try { scanBegin = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --scan-begin\n";
            return 1;
        }
    }

    if (parser("--scan-end") >> str)
    {
        try { scanEnd = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --scan-end\n";
            return 1;
        }
    }

    if (parser("--probe-register") >> str)
    {
        try { probeRegister = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --probe-register\n";
            return 1;
        }
    }

    if (parser("--probe-amod") >> str)
    {
        try { probeAmod = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse value for --probe-amod\n";
            return 1;
        }
    }

    if (parser("--probe-datawidth") >> str)
    {
        if (auto dw = parse_vme_datawidth(str))
        {
            probeDataWidth = *dw;
        }
        else
        {
            std::cerr << fmt::format("Error: invalid --probe-datawidth given: {}\n", str);
            return 1;
        }
    }

    if (parser("--stack-max-words") >> str)
    {
        if (auto val = parse_unsigned<u16>(str))
        {
            stackMaxWords = *val;
            if (stackMaxWords < 16 || stackMaxWords >= stacks::StackMemoryWords)
            {
                std::cerr << fmt::format("Error: --stack-max-words value is out of range\n");
                return 1;
            }
        }
    }

    if (scanEnd < scanBegin)
        std::swap(scanEnd, scanBegin);

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    std::cout << fmt::format("scanbus scan range: [{:#06x}, {:#06x}), {} addresses, probeRegister={:#06x}"
        ", probeAmod={:#04x}, probeDataWidth={}, stackMaxWords={}\n",
        scanBegin, scanEnd, scanEnd - scanBegin, probeRegister,
        probeAmod, probeDataWidth == VMEDataWidth::D16 ? "d16" : "d32",
        stackMaxWords);

    using namespace scanbus;

    auto candidates = scan_vme_bus_for_candidates(mvlc, scanBegin, scanEnd,
        probeRegister, probeAmod, probeDataWidth, stackMaxWords);

    size_t moduleCount = 0;

    if (!candidates.empty())
    {
        if (candidates.size() == 1)
            std::cout << fmt::format("Found {} module candidate address: {:#010x}\n",
                candidates.size(), fmt::join(candidates, ", "));
        else
            std::cout << fmt::format("Found {} module candidate addresses: {:#010x}\n",
                candidates.size(), fmt::join(candidates, ", "));

        for (auto addr: candidates)
        {
            VMEModuleInfo moduleInfo{};

            if (auto ec = read_module_info(mvlc, addr, moduleInfo))
            {
                std::cout << fmt::format("Error checking address {:#010x}: {}\n", addr, ec.message());
                continue;
            }

            auto msg = fmt::format("Found module at {:#010x}: hwId={:#06x}, fwId={:#06x}, type={}",
                addr, moduleInfo.hwId, moduleInfo.fwId, moduleInfo.moduleTypeName());

            if (vme_modules::is_mdpp(moduleInfo.hwId))
                msg += fmt::format(", mdpp_fw_type={}", moduleInfo.mdppFirmwareTypeName());

            std::cout << fmt::format("{}\n", msg);
            ++moduleCount;
        }

        if (moduleCount)
            std::cout << fmt::format("Scan found {} modules in total.\n", moduleCount);
    }
    else
        std::cout << fmt::format("scanbus did not find any mesytec VME modules\n");

    return 0;
}

static const Command ScanbusCommand
{
    .name = "scanbus",
    .help = unindent(R"~(
usage: mvlc-cli scanbus [--scan-begin=<addr>] [--scan-end=<addr>] [--probe-register=<addr>]
                        [--probe-amod=<amod>] [--probe-datawidth=<datawidth>]
                        [--stack-max-words=<numWords>]

    Scans the upper 16 bits of the VME address space for the presence of (mesytec) VME modules.
    Displays the hardware and firmware revisions of found modules and additionally the loaded
    firmware type for MDPP-like modules.

options:
    --scan-begin=<addr> (default=0x0000)
        16-bit start address for the scan.

    --scan-end=<addr> (default=0xffff)
        16-bit one-past-end address for the scan.

    --probe-register=<addr> (default=0)
        The 16-bit register address to read from.

    --probe-amod=<amod> (default=0x09)
        The VME amod to use when reading the probe register.

    --probe-datawidth=(d16|16|d32|32) (default=d16)
        VME datawidth to use when reading the probe register.

    --stack-max-words=<numWords> (default=255)
        Limit the size of the command stacks to execute to <numWords> words.
        Max is 2047 to use all of the stack memory.
)~"),
    .exec = scanbus_command,
};

DEF_EXEC_FUNC(mvlc_set_id_command)
{
    spdlog::trace("entered mvlc_set_id_command()");
    trace_log_parser_info(ctx.parser, "mvlc_set_id_command");

    unsigned ctrlId;

    if (!(ctx.parser(2) >> ctrlId))
    {
        std::cerr << "Error: invalid ctrlId given\n";
        return 1;
    }

    spdlog::trace("mvlc_set_id_command: ctrlId={}", ctrlId);

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    if (auto ec = mvlc.writeRegister(registers::controller_id, ctrlId))
    {
        std::cerr << fmt::format("Error setting controller id: {}\n", ctrlId);
        return 1;
    }

    std::cout << fmt::format("MVLC controller id set to {}\n", ctrlId);

    return 0;
}

static const Command MvlcSetIdCommand
{
    .name = "set_id",
    .help = unindent(
R"~(usage: mvlc-cli set_id <ctrlId>

    Sets the MVLC controller id which is transmitted with every command response and in
    readout data frames.

)~"),
    .exec = mvlc_set_id_command,
};

DEF_EXEC_FUNC(register_read_command)
{
    spdlog::trace("entered register_read_command()");

    auto &parser = ctx.parser;
    trace_log_parser_info(parser, "register_read_command");

    u16 address = 0x0u;
    auto addressStr = parser[2];

    if (auto val = parse_unsigned<u16>(addressStr))
    {
        address = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <address> value given: {}\n", addressStr);
        return 1;
    }

    spdlog::trace("register_read_command: address=0x{:04x}", address);

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    u32 value = 0;

    if (auto ec = mvlc.readRegister(address, value))
    {
        std::cerr << fmt::format("Error from register read: {}\n", ec.message());
        return 1;
    }

    std::cout << fmt::format("register_read 0x{:04x} -> 0x{:08x} ({} decimal)\n",
        address, value, value);

    return 0;
}

static const Command RegisterReadCommand
{
    .name = "register_read",
    .help = R"~(
usage: mvlc-cli register_read <address>

    Read one of the internal MVLC registers.

options:
    <address>
        16-bit register address to read from.
)~",
    .exec = register_read_command,
};

DEF_EXEC_FUNC(register_write_command)
{
    spdlog::trace("entered register_write_command()");

    auto &parser = ctx.parser;
    trace_log_parser_info(parser, "register_write_command");

    u16 address = 0x0u;
    u32 value = 0x0u;

    auto addressStr = parser[2];
    auto valueStr = parser[3];

    if (auto val = parse_unsigned<u16>(addressStr))
    {
        address = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <address> value given: {}\n", addressStr);
        return 1;
    }

    if (auto val = parse_unsigned<u32>(valueStr))
    {
        value = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <value> given: {}\n", valueStr);
        return 1;
    }

    spdlog::trace("register_write_command: address=0x{:04x}, value=0x{:08x}", address, value);

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    if (auto ec = mvlc.writeRegister(address, value))
    {
        std::cerr << fmt::format("Error from register write: {}\n", ec.message());
        return 1;
    }

    std::cout << fmt::format("register_write 0x{:04x} -> 0x{:08x} ({} decimal) ok\n",
        address, value, value);

    return 0;
}

static const Command RegisterWriteCommand
{
    .name = "register_write",
    .help = R"~(
usage: mvlc-cli register_write <address> <value>

    Write one of the internal MVLC registers.

options:
    <address>
        16-bit register address to write to.

    <value>
        16/32-bit register value to write.
)~",
    .exec = register_write_command,
};

DEF_EXEC_FUNC(vme_read_command)
{
    spdlog::trace("entered vme_read_command()");

    auto &parser = ctx.parser;
    parser.add_params({"--amod", "--datawidth"});
    parser.parse(argv);
    trace_log_parser_info(parser, "vme_read_command");

    u8 amod = 0x09u;
    VMEDataWidth dataWidth = VMEDataWidth::D16;
    u32 address = 0x0u;
    std::string str;

    if (parser("--amod") >> str)
    {
        if (auto val = parse_unsigned<u8>(str))
        {
            amod = *val;

            if (vme_amods::is_block_mode(amod))
            {
                std::cerr << fmt::format("Error: expected non-block vme amod value.\n");
                return 1;
            }
        }
        else
        {
            std::cerr << fmt::format("Error: invalid --amod value given: {}\n", str);
            return 1;
        }
    }

    if (parser("--datawidth") >> str)
    {
        if (auto dw = parse_vme_datawidth(str))
        {
            dataWidth = *dw;
        }
        else
        {
            std::cerr << fmt::format("Error: invalid --datawidth given: {}\n", str);
            return 1;
        }
    }

    auto addressStr = parser[2];

    if (auto val = parse_unsigned<u32>(addressStr))
    {
        address = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <address> value given: {}\n", addressStr);
        return 1;
    }

    spdlog::trace("vme_read_command: amod=0x{:02x}, dataWidth={}, address=0x{:08x}",
        amod, dataWidth == VMEDataWidth::D16 ? "d16" : "d32", address);

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    u32 value = 0;

    if (auto ec = mvlc.vmeRead(address, value, amod, dataWidth))
    {
        if (ec == MVLCErrorCode::StackSyntaxError)
        {
            std::cerr << fmt::format("Error from VME read: {}. Check --amod value.\n",
                ec.message());
        }
        else
        {
            std::cerr << fmt::format("Error from VME read: {}\n", ec.message());
        }
        return 1;
    }

    std::cout << fmt::format("vme_read 0x{:02x} {} 0x{:08x} -> 0x{:08x} ({} decimal)\n",
        amod, dataWidth == VMEDataWidth::D16 ? "d16" : "d32", address, value, value);

    return 0;
}

static const Command VmeReadCommand
{
    .name = "vme_read",
    .help = R"~(
usage: mvlc-cli vme_read [--amod=0x09] [--datawidth=16] <address>

    Perform a single value vme read.

options:
    --amod=<amod> (default=0x09)
        VME address modifier to send with the read command. Only non-block amods are allowed.
        A list of address modifiers is available here: https://www.vita.com/page-1855176.

    --datawidth=(d16|16|d32|32) (default=d16)
        VME datawidth to use for the read.

    <address>
        32-bit VME address to read from.
)~",
    .exec = vme_read_command,
};

DEF_EXEC_FUNC(vme_write_command)
{
    spdlog::trace("entered vme_write_command()");

    auto &parser = ctx.parser;
    parser.add_params({"--amod", "--datawidth"});
    parser.parse(argv);
    trace_log_parser_info(parser, "vme_write_command");

    u8 amod = 0x09u;
    VMEDataWidth dataWidth = VMEDataWidth::D16;
    u32 address = 0x0u;
    u32 value = 0x0u;
    std::string str;

    if (parser("--amod") >> str)
    {
        if (auto val = parse_unsigned<u8>(str))
        {
            amod = *val;

            if (vme_amods::is_block_mode(amod))
            {
                std::cerr << fmt::format("Error: expected non-block vme amod value.\n");
                return 1;
            }
        }
        else
        {
            std::cerr << fmt::format("Error: invalid --amod value given: {}\n", str);
            return 1;
        }
    }

    if (parser("--datawidth") >> str)
    {
        if (auto dw = parse_vme_datawidth(str))
        {
            dataWidth = *dw;
        }
        else
        {
            std::cerr << fmt::format("Error: invalid --datawidth given: {}\n", str);
            return 1;
        }
    }

    auto addressStr = parser[2];
    auto valueStr = parser[3];

    if (auto val = parse_unsigned<u32>(addressStr))
    {
        address = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <address> value given: {}\n", addressStr);
        return 1;
    }

    if (auto val = parse_unsigned<u32>(valueStr))
    {
        value = *val;
    }
    else
    {
        std::cerr << fmt::format("Error: invalid <value> given: {}\n", addressStr);
        return 1;
    }

    spdlog::trace("vme_write_command: amod=0x{:02x}, dataWidth={}, address=0x{:08x}, value=0x{:08x}",
        amod, dataWidth == VMEDataWidth::D16 ? "d16" : "d32", address, value);

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    if (auto ec = mvlc.vmeWrite(address, value, amod, dataWidth))
    {
        if (ec == MVLCErrorCode::StackSyntaxError)
        {
            std::cerr << fmt::format("Error from VME write: {}. Check --amod value.\n",
                ec.message());
        }
        else
        {
            std::cerr << fmt::format("Error from VME write: {}\n", ec.message());
        }
        return 1;
    }

    std::cout << fmt::format("vme_write 0x{:02x} {} 0x{:08x} 0x{:08x} ({} decimal) ok\n",
        amod, dataWidth == VMEDataWidth::D16 ? "d16" : "d32", address, value, value);

    return 0;
}

static const Command VmeWriteCommand
{
    .name = "vme_write",
    .help = R"~(
usage: mvlc-cli vme_write [--amod=0x09] [--datawidth=16] <address> <value>

    Perform a single value vme write.

options:
    --amod=<amod> (default=0x09)
        VME address modifier to send with the write command. Only non-block amods are allowed.
        A list of address modifiers is available here: https://www.vita.com/page-1855176.

    --datawidth=(d16|16|d32|32) (default=d16)
        VME datawidth to use for the write.

    <address>
        32 bit VME address to write to.

    <value>
        16/32 bit value to write.
)~",
    .exec = vme_write_command,
};

namespace
{
    struct RegisterMeta
    {
        std::string name;
        std::string group;
        u16 address;
    };

    struct RegisterReadResult
    {
        RegisterMeta meta;
        u32 value;
        std::string info;
    };

    using InfoDecoder = std::function<std::string (const std::vector<RegisterReadResult> &data)>;

    std::string decode_ipv4(const std::vector<RegisterReadResult> &data)
    {
        if (data.size() != 2)
            return "invalid data";

        u32 addr = (data[1].value << 16) | data[0].value;

        return fmt::format("{}.{}.{}.{}", (addr >> 24) & 0xff, (addr >> 16) & 0xff,
            (addr >> 8) & 0xff, addr & 0xff);
    }

    std::string decode_mac(const std::vector<RegisterReadResult> &data)
    {
        if (data.size() != 3)
            return "invalid data";

        return fmt::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            (data[2].value >> 8) & 0xff, data[2].value & 0xff,
            (data[1].value >> 8) & 0xff, data[1].value & 0xff,
            (data[0].value >> 8) & 0xff, data[0].value & 0xff);
    }

    std::string decode_stack_exec_status(const std::vector<RegisterReadResult> &data)
    {
        if (data.size() != 2)
            return "invalid data";

        auto frameFlags = extract_frame_flags(data[0].value);

        return fmt::format("FrameFlags={}, StackReference={:#010x}",
            format_frame_flags(frameFlags), data[1].value);
    }

    static const std::vector<RegisterMeta> RegistersData =
    {
        {"daq_mode", {}, 0x1300},
        {"controller_id", {}, 0x1304},
        {"stack_exec_status0", "stack_exec_status", 0x1400},
        {"stack_exec_status1", "stack_exec_status", 0x1404},
        {"own_ip_lo", "own_ip", 0x4400},
        {"own_ip_hi", "own_ip", 0x4402},
        {"dhcp_ip_lo", "dhcp_ip", 0x4408},
        {"dhcp_ip_hi", "dhcp_ip", 0x440a},
        {"data_ip_lo", "data_dest_ip", 0x4410},
        {"data_ip_hi", "data_dest_ip", 0x4412},
        {"cmd_mac_0", "cmd_dest_mac", 0x4414},
        {"cmd_mac_1", "cmd_dest_mac", 0x4416},
        {"cmd_mac_2", "cmd_dest_mac", 0x4418},
        {"cmd_dest_port", {}, 0x441a},
        {"data_dest_port", {}, 0x441c},
        {"data_mac_0", "data_dest_mac", 0x441e},
        {"data_mac_1", "data_dest_mac", 0x4420},
        {"data_mac_2", "data_dest_mac", 0x4422},
        {"jumbo_frame_enable", {}, 0x4430},
        {"eth_delay_read", {}, 0x4432},
        {"hardware_id", {}, 0x6008},
        {"firmware_revision", {}, 0x600e},
        {"mcst_enable", {}, 0x6020},
        {"mcst_address", {}, 0x6024},
    };

    static const std::map<std::string, InfoDecoder> Decoders =
    {
        { "own_ip", decode_ipv4 },
        { "dhcp_ip", decode_ipv4 },
        { "data_dest_ip", decode_ipv4 },
        { "cmd_dest_mac", decode_mac },
        { "data_dest_mac", decode_mac },
        { "stack_exec_status", decode_stack_exec_status },
    };
}

DEF_EXEC_FUNC(dump_registers_command)
{
    spdlog::trace("entered dump_registers_command()");

    auto &parser = ctx.parser;
    parser.add_params({"--yaml"});
    parser.parse(argv);
    trace_log_parser_info(parser, "dump_registers_command");

    auto [mvlc, ec] = make_and_connect_default_mvlc(parser);

    if (!mvlc || ec)
        return 1;

    std::vector<RegisterReadResult> registerValues;

    for (const auto &regMeta: RegistersData)
    {
        RegisterReadResult result{};
        result.meta = regMeta;

        if (result.meta.group.empty())
            result.meta.group = result.meta.name;

        if (auto ec = mvlc.readRegister(regMeta.address, result.value))
        {
            fmt::print("Error reading register '{}' ({:#06x}): {}\n",
                regMeta.name, regMeta.address, ec.message());
            return 1;
        }

        registerValues.emplace_back(result);
    }

    std::map<std::string, std::vector<RegisterReadResult>> groupedValues;

    for (const auto &rv: registerValues)
    {
        groupedValues[rv.meta.group].emplace_back(rv);
    }

    // Populate RegisterReadResult.info with the return value from the decoder
    // function for each group.
    for (auto &[group, values]: groupedValues)
    {
        if (auto it = Decoders.find(group); it != Decoders.end())
        {
            auto decodedInfo = it->second(values);

            for (auto &rrr: values)
            {
                rrr.info = decodedInfo;
            }
        }
    }

    // Recreate the original flat table from the augmented grouped values.
    registerValues.clear();

    for (const auto &[_, values]: groupedValues)
    {
        for (const auto &rv: values)
        {
            registerValues.emplace_back(rv);
        }
    }

    if (!parser["--yaml"])
    {
        fmt::print("{:20s} {:20s} {:7s} {:10s}\n", "name", "group", "address", "value", "info");

        for (const auto &rv: registerValues)
        {
            fmt::print("{:20s} {:20s} {:#06x}  {:#010x} {}\n",
                    rv.meta.name, rv.meta.group, rv.meta.address, rv.value, rv.info);
        }
    }
    else
    {
        YAML::Emitter out;
        out << YAML::Hex;

        out << YAML::BeginSeq;

        for (const auto &rv: registerValues)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << rv.meta.name;
            out << YAML::Key << "address" << YAML::Value << rv.meta.address;
            out << YAML::Key << "group" << YAML::Value << rv.meta.group;
            out << YAML::Key << "value" << YAML::Value << rv.value;
            out << YAML::Key << "info" << YAML::Value << rv.info;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;

        std::cout << out.c_str() << "\n";
    }

    return 0;
}

static const Command DumpRegistersCommand
{
    .name = "dump_registers",
    .help = R"~(
usage: mvlc-cli dump_registers [--yaml]

    Read and print interal MVLC registers. Use --yaml to get YAML formatted data
    instead of a human-readable table.
)~",
    .exec = dump_registers_command,
};

DEF_EXEC_FUNC(crateconfig_from_mvlc)
{
    spdlog::trace("entered crateconfig_from_mvlc()");
    trace_log_parser_info(ctx.parser, "crateconfig_from_mvlc");

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    const auto StackCount = mvlc.getStackCount();
    // First is stack1, stack0 is reserved for direct command execution.
    std::vector<StackInfo> stackInfos;

    for (unsigned stackId=1; stackId < StackCount; ++stackId)
    {
        auto [stackInfo, ec] = read_stack_info(mvlc, stackId);
        stackInfos.emplace_back(std::move(stackInfo));

        if (ec && ec != make_error_code(MVLCErrorCode::InvalidStackHeader))
        {
            std::cout << fmt::format("Error reading stack info for stack#{}: {}\n", stackId, ec.message());
            return 1;
        }
    }

    u32 crateId = 0;
    if (auto ec = mvlc.readRegister(registers::controller_id, crateId))
    {
        std::cerr << fmt::format("Error reading controller id: {}\n", ec.message());
        return 1;
    }

    CrateConfig crateConfig;
    crateConfig.connectionType = mvlc.connectionType();
    crateConfig.crateId = crateId;

    // FIXME: add getHost() and other info to the interfaces. It's weird to
    // downcast to Impl here. Or put a getConnectionInfo() into MVLC and make it
    // compatible with CrateConfig.
    if (auto eth = dynamic_cast<eth::Impl *>(mvlc.getImpl()))
    {
        crateConfig.ethHost = eth->getHost();
        u32 jumbosEnabled = 0;
        if (auto ec = mvlc.readRegister(registers::jumbo_frame_enable, jumbosEnabled))
        {
            std::cerr << fmt::format("Error reading jumbo frame enable register: {}\n", ec.message());
            return 1;
        }
        crateConfig.ethJumboEnable = jumbosEnabled;
    }
    else if (auto usb = dynamic_cast<usb::Impl *>(mvlc.getImpl()))
    {
        auto devInfo = usb->getDeviceInfo();
        crateConfig.usbIndex = devInfo.index;
        crateConfig.usbSerial = devInfo.serial;
    }

    for (const auto &stackInfo: stackInfos)
    {
        crateConfig.triggers.push_back(stackInfo.triggerValue);
        crateConfig.stacks.emplace_back(stack_builder_from_buffer(stackInfo.contents));
    }

    std::cout << to_yaml(crateConfig);

    return 0;
}

static const Command CrateConfigFromMvlcCommand
{
    .name = "crateconfig_from_mvlc",
    .help = R"~(
usage: mvlc-cli crateconfig_from_mvlc

    Read stack contents and trigger values from the MVLC to create and print a CrateConfig.
    Note: the resulting CrateConfig is missing the Trigger / IO setup as that can't be read
    back and the VME module init commands used to start the DAQ.

    The remaining information in the resulting CrateConfig is still useful for
    debugging.
)~",
    .exec = crateconfig_from_mvlc,
};

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
usage: mvlc-cli [-v | --version] [-h | --help [-a]]
                [--log-level=(off|error|warn|info|debug|trace)] [--trace] [--debug] [--info]
                [--mvlc <url> | --mvlc-usb | --mvlc-usb-index <index> |
                 --mvlc-usb-serial <serial> | --mvlc-eth <hostname>
                 --mvlc-force-connect]
                <command> [<args>]

Core Commands:
    help <command>
        Show help for the given command and exit.

    list-commands | help -a
        Print list of available commands.

Core Switches:
    -v | --version
        Show mvlc-cli and mesytec-mvlc versions and exit.

    -h <command> | --help <command>
        Show help for the given command and exit.

    -h -a | --help -a
        Same as list-commands: print a list of available commands.

MVLC connection URIs:

    mvlc-cli supports the following URI schemes with --mvlc <uri> to connect to MVLCs:
        usb://                   Use the first USB device
        usb://<serial-string>    USB device matching the given serial number
        usb://@<index>           USB device with the given logical FTDI driver index
        eth://<hostname|ip>      ETH/UDP with a hostname or an ip-address
        udp://<hostname|ip>      ETH/UDP with a hostname or an ip-address
        hostname                 No scheme part -> interpreted as a hostname for ETH/UDP

    Alternatively the transport specific options --mvlc-usb, --mvlc-usb-index,
    --mvlc-usb-serial and --mvlc-eth may be used.

    If none of the above is given MVLC_ADDRESS from the environment is used as
    the MVLC URI.

    Use --mvlc-force-connect to forcibly disable DAQ mode when connecting. Use
    this when you get the "MVLC is in use" error on connect.
)~";


    spdlog::set_level(spdlog::level::warn);
    mesytec::mvlc::set_global_log_level(spdlog::level::warn);

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser parser({"-h", "--help", "--log-level"});
    add_mvlc_standard_params(parser);
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";
        else if (parser["--info"])
            logLevelName = "info";

        if (!logLevelName.empty())
            spdlog::set_level(spdlog::level::from_str(logLevelName));
    }

    trace_log_parser_info(parser, "mvlc-cli");

    CliContext ctx;
    ctx.generalHelp = generalHelp;

    ctx.commands.insert(HelpCommand);
    ctx.commands.insert(ListCmdsCommand);
    ctx.commands.insert(MvlcVersionCommand);
    ctx.commands.insert(MvlcStackInfoCommand);
    ctx.commands.insert(ScanbusCommand);
    ctx.commands.insert(MvlcSetIdCommand);
    ctx.commands.insert(RegisterReadCommand);
    ctx.commands.insert(RegisterWriteCommand);
    ctx.commands.insert(VmeReadCommand);
    ctx.commands.insert(VmeWriteCommand);
    ctx.commands.insert(DumpRegistersCommand);
    ctx.commands.insert(CrateConfigFromMvlcCommand);
    ctx.parser = parser;

    // mvlc-cli                 // show generalHelp
    // mvlc-cli -h              // show generalHelp
    // mvlc-cli -h -a           // call list-commands
    // mvlc-cli -h vme_read     // find cmd by name and output its help
    // mvlc-cli vme_read -h     // same as above
    // mvlc-cli help vme_read   // call 'help', let it parse 'vme_read'

    {
        std::string cmdName;
        if ((parser({"-h", "--help"}) >> cmdName))
        {
            if (auto cmd = ctx.commands.find(Command{cmdName});
                cmd != ctx.commands.end())
            {
                std::cout << cmd->help;
                return 0;
            }

            std::cerr << fmt::format("Error: no such command '{}'\n"
                                     "Use 'mvlc-cli list-commands' to get a list of commands\n",
                                     cmdName);
            return 1;
        }
    }

    if (auto cmdName = parser[1]; !cmdName.empty())
    {
        if (auto cmd = ctx.commands.find(Command{parser[1]});
            cmd != ctx.commands.end())
        {
            spdlog::trace("parsed cli: found cmd='{}'", cmd->name);
            if (parser[{"-h", "--help"}])
            {
                spdlog::trace("parsed cli: found -h flag for command {}, directly displaying help text",
                    cmd->name);
                std::cout << cmd->help;
                return 0;
            }

            spdlog::trace("parsed cli: executing cmd='{}'", cmd->name);
            return cmd->exec(ctx, *cmd, argc, const_cast<const char **>(argv));
        }
        else
        {
            std::cerr << fmt::format("Error: no such command '{}'\n", parser[1]);
            return 1;
        }
    }

    assert(parser[1].empty());

    if (parser[{"-h", "--help"}])
    {
        if (parser["-a"])
            return ListCmdsCommand.exec(ctx, ListCmdsCommand, argc, const_cast<const char **>(argv));
        std::cout << generalHelp;
        return 0;
    }

    if (parser[{"-v", "--version"}])
    {
        std::cout << fmt::format("mvlc-cli - version {}\n", mesytec::mvlc::library_version());
        return 0;
    }

    std::cout << generalHelp;
    return 1;
}
