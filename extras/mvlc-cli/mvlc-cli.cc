// Fore some info on how to use argh effectively see
// https://a4z.gitlab.io/blog/2019/04/30/Git-like-sub-commands-with-argh.html

#include <argh.h>
#include <string>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/scanbus_support.h>

using namespace mesytec::mvlc;

// https://stackoverflow.com/a/24900770
inline std::string unindent(const char* p)
{
    std::string result;
    if (*p == '\n') ++p;
    const char* p_leading = p;
    while (std::isspace(*p) && *p != '\n')
        ++p;
    size_t leading_len = p - p_leading;
    while (*p)
    {
        result += *p;
        if (*p++ == '\n')
        {
            for (size_t i = 0; i < leading_len; ++i)
                if (p[i] != p_leading[i])
                    goto dont_skip_leading;
            p += leading_len;
        }
      dont_skip_leading: ;
    }
    return result;
}

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                // static_cast<int(*)(int)>(std::tolower)         // wrong
                // [](int c){ return std::tolower(c); }           // wrong
                // [](char c){ return std::tolower(c); }          // wrong
                   [](unsigned char c){ return std::tolower(c); } // correct
                  );
    return s;
}

void trace_log_parser_info(const argh::parser &parser, const std::string context)
{
    if (auto params = parser.params(); !params.empty())
    {
        for (const auto &param: params)
            spdlog::trace("argh-parse {} parameter: {}={}", context, param.first, param.second);
    }

    if (auto flags = parser.flags(); !flags.empty())
        spdlog::trace("argh-parse {} flags: {}", context, fmt::join(flags, ", "));

    if (auto pos_args = parser.pos_args(); !pos_args.empty())
    {
        spdlog::trace("argh-parse {} pos args: {}", context, fmt::join(pos_args, ", "));
    }
}

static const std::vector<std::string> MvlcStandardParams =
    {"--mvlc", "--mvlc-usb-index", "--mvlc-usb-serial", "--mvlc-eth"};

MVLC make_mvlc_from_standard_params(const argh::parser &parser)
{
    trace_log_parser_info(parser, "make_mvlc_from_standard_params");
    std::string arg;

    if (parser("--mvlc") >> arg)
        return make_mvlc(arg); // mvlc URI

    if (parser["--mvlc-usb"])
        return make_mvlc_usb();

    unsigned usbIndex = 0;
    if (parser("--mvlc-usb-index") >> usbIndex)
        return make_mvlc_usb(usbIndex);

    if (parser("--mvlc-usb-serial") >> arg)
        return make_mvlc_usb(arg);

    if (parser("--mvlc-eth") >> arg)
        return make_mvlc_eth(arg);

    return MVLC{};
}

MVLC make_mvlc_from_standard_params(const char **argv)
{
    argh::parser parser;
    for (const auto &p: MvlcStandardParams)
        parser.add_param(p);
    parser.parse(argv);
    return make_mvlc_from_standard_params(parser);
}

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
    .help = R"~(Raw help for the 'help' command
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
    .help = R"~(Raw help for the 'list-commands' command
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

    if (!ctx.parser[2].empty())
    {
        if (!(ctx.parser(2) >> stackId))
        {
            std::cerr << "Error: invalid stackId given\n";
            return 1;
        }

        if (stackId >= stacks::StackCount)
        {
            std::cerr << fmt::format("Error: stackId={} is out of range\n", stackId);
            return 1;
        }
    }

    const unsigned stackMin = stackId >= stacks::StackCount ? 0 : stackId;
    const unsigned stackMax = stackId >= stacks::StackCount ? stacks::StackCount : stackId + 1;

    const bool doRaw     = ctx.parser["--raw"];
    const bool doYaml    = ctx.parser["--yaml"];
    const bool doDecoded = !doYaml && !doRaw;

    if (doRaw && doYaml)
    {
        std::cerr << "Error: --raw and --yaml are exlusive flags\n";
        return 1;
    }

    spdlog::trace("stack_info: stackMin={}, stackMax={}, doRaw={}, doYaml={}",
        stackMin, stackMax, doRaw, doYaml);

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

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

    // TODO: leftoff here on 230707

    for (const auto &entry: stackInfos)
    {
        const auto &stackId = entry.stackId;
        const auto &ec = entry.ec;
        const auto &stackInfo = entry.stackInfo;

        if (ec == make_error_code(MVLCErrorCode::InvalidStackHeader))
        {
            std::cerr << fmt::format("- stack#{}: triggers=0x{:02x}, offset={}, startAddress=0x{:04x}"
                ", empty stack (does not start with a StackStart (0xF3) header)\n",
                stackId, stackInfo.triggers, stackInfo.offset, stackInfo.startAddress);
            continue;
        }
        else
        {
            std::cout << fmt::format("- stack#{}: triggers=0x{:02x}, offset={}, startAddress=0x{:04x}, len={}:\n",
                stackId, stackInfo.triggers, stackInfo.offset, stackInfo.startAddress,
                stackInfo.contents.size());

            for (auto &word: stackInfo.contents)
                std::cout << fmt::format("  0x{:08x}\n", word);
            std::cout << "--\n";

            auto stackBuilder = stack_builder_from_buffer(stackInfo.contents);
            for (const auto &cmd: stackBuilder.getCommands())
            {
                std::cout << "  " << to_string(cmd) << "\n";
            }
        }
    }

    return 0;
}

static const Command MvlcStackInfoCommand =
{
    .name = "stack_info",
    .help = unindent(
R"~(usage: mvlc-cli stack_info [--raw] [--yaml] [<stackId>]

    Read and print command stack info and contents. If no stackId is given all event readout
    stacks (stack1..7) are read.

options:

    --raw           Print the raw stack buffer instead of decoded commands.
    --yaml          Output the stack(s) in yaml format, suitable for loading with 'upload_stack'.
    <stackId>       Optional numeric stack id. Range 0..7.
)~"),
    .exec = mvlc_stack_info_command,
};

DEF_EXEC_FUNC(scanbus_command)
{
    spdlog::trace("entered mvlc_scanbus_command()");

    auto parser = ctx.parser;
    parser.add_params({"--scan-begin", "--scan-end", "--probe-register", "--probe-amod", "--probe-datawidth"});
    parser.parse(argv);
    trace_log_parser_info(parser, "mvlc_scanbus_command");

    u16 scanBegin = 0x0u;
    u16 scanEnd   = 0xffffu;
    u16 probeRegister = 0;
    u8  probeAmod = 0x09;
    VMEDataWidth probeDataWidth = VMEDataWidth::D16;
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
        auto dwStr = str_tolower(str);

        if (dwStr == "d16" || dwStr == "16")
            probeDataWidth = VMEDataWidth::D16;
        else if (dwStr == "d32" || dwStr == "32")
            probeDataWidth = VMEDataWidth::D32;
        else
        {
            std::cerr << fmt::format("Error: invalid --probe-datawidth given: {}\n", str);
            return 1;
        }

        str.clear();
    }

    if (scanEnd < scanBegin)
        std::swap(scanEnd, scanBegin);

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    std::cout << fmt::format("scanbus scan range: [{:#06x}, {:#06x}), {} addresses, probeRegister={:#06x}"
        ", probeAmod={:#04x}, probeDataWidth={}\n",
        scanBegin, scanEnd, scanEnd - scanBegin, probeRegister,
        probeAmod, probeDataWidth == VMEDataWidth::D16 ? "d16" : "d32");

    using namespace scanbus;

    auto candidates = scan_vme_bus_for_candidates(mvlc, scanBegin, scanEnd,
        probeRegister, probeAmod, probeDataWidth);

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

            if (auto ec = mvlc.vmeRead(addr + FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
            {
                std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                continue;
            }

            if (auto ec = mvlc.vmeRead(addr + HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
            {
                std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                continue;
            }

            if (moduleInfo.hwId == 0 && moduleInfo.fwId == 0)
            {
                if (auto ec = mvlc.vmeRead(addr + MVHV4FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                    continue;
                }

                if (auto ec = mvlc.vmeRead(addr + MVHV4HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                    continue;
                }
            }

            auto msg = fmt::format("Found module at {:#010x}: hwId={:#06x}, fwId={:#06x}, type={}",
                addr, moduleInfo.hwId, moduleInfo.fwId, moduleInfo.moduleTypeName());

            if (is_mdpp(moduleInfo.hwId))
                msg += fmt::format(", mdpp_fw_type={}", moduleInfo.mdppFirmwareTypeName());

            std::cout << fmt::format("{}\n", msg);
        }
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
)~"),
    .exec = scanbus_command,
};

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
usage: mvlc-cli [-v | --version] [-h | --help [-a]] [--log-level=(off|error|warn|info|debug|trace)]
                [--mvlc <url> | --mvlc-usb | --mvlc-usb-index <index> |
                 --mvlc-usb-serial <serial> | --mvlc-eth <hostname>]
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

MVLC low level commands:

    version
        Print MVLC hardware and firmware revisions.

    status
        Gather status and version information from MVLC.

    read_register
        Read an internal MVLC register.

    write_register
        Write an internal MVLC register.

VME access and utility commands:

    vme_read
    vme_read_swapped
    vme_write
    vme_scan_bus

Command stacks and lists:

    stack_info

Crate/readout configuration



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
)~";


    spdlog::set_level(spdlog::level::warn);
    mesytec::mvlc::set_global_log_level(spdlog::level::warn);

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser parser({"-h", "--help", "--log-level"});
    for (const auto &p: MvlcStandardParams)
        parser.add_param(p);
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";

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
        std::cout << "mvlc-cli - version 0.1\n";
        std::cout << fmt::format("mesytec-mvlc - version {}\n", mesytec::mvlc::library_version());
        return 0;
    }

    std::cout << generalHelp;
    return 1;
}
