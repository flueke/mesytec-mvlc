// Fore some info on how to use argh effectively see
// https://a4z.gitlab.io/blog/2019/04/30/Git-like-sub-commands-with-argh.html

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>

using namespace mesytec::mvlc;

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

MVLC make_mvlc_from_standard_args(const char **argv)
{
    argh::parser parser({"--mvlc", "--mvlc-usb-index", "--mvlc-usb-serial", "--mvlc-eth"});
    parser.parse(argv);

    std::string arg;

    if (parser("--mvlc") >> arg)
        return make_mvlc(arg); // mvlc URI

    if (parser["--mvlc-usb"])
        return make_mvlc_usb();

    return MVLC{};
}

MVLC make_default_mvlc(const char **argv)
{
    // arguments before env variables
    if (auto mvlc = make_mvlc_from_standard_args(argv))
        return mvlc;

    // try via env variable
    if (char *envAddr = std::getenv("MVLC_ADDRESS"))
        return make_mvlc(envAddr);

    // no luck
    return MVLC{};
}

MVLC make_default_mvlc_err(const char **argv)
{
    if (auto mvlc = make_default_mvlc(argv))
        return mvlc;

    std::cerr << "Error: no MVLC to connect to\n";
    return MVLC{};
}

std::pair<MVLC, std::error_code> make_default_mvlc_err_connect(const char **argv)
{
    if (auto mvlc = make_default_mvlc_err(argv))
    {
        auto ec = mvlc.connect();
        if (ec)
        {
            std::cerr << fmt::format("Error connecting to MVLC {}: {}\n",
                mvlc.connectionInfo(), ec.message());
        }

        return std::make_pair(mvlc, ec);
    }

    return std::pair<MVLC, std::error_code>();
};

void trace_log_parser_info(const argh::parser &parser, const std::string context="cli")
{
    if (auto params = parser.params(); !params.empty())
    {
        for (const auto &param: params)
            spdlog::trace("{} argh-parse parameter: {}={}", context, param.first, param.second);
    }

    if (auto flags = parser.flags(); !flags.empty())
        spdlog::trace("{} argh-parse flags: {}", context, fmt::join(flags, ", "));

    if (auto pos_args = parser.pos_args(); !pos_args.empty())
    {
        spdlog::trace("{} argh-parse pos args: {}", context, fmt::join(pos_args, ", "));
    }
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
    .help = R"~(Raw help for the 'help' command)~",
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
    .help = R"~(Raw help for the 'list-commands' command)~",
    .exec = list_commands_command,
};

DEF_EXEC_FUNC(mvlc_version_command)
{
    spdlog::trace("entered mvlc_version_command()");
    trace_log_parser_info(ctx.parser, "mvlc_version_command");

    auto [mvlc, ec] = make_default_mvlc_err_connect(argv);

    if (!mvlc || ec)
        return 1;

    std::cout << fmt::format("{}, hardwareId=0x{:04x}, firmwareRevision=0x{:04x}\n",
        mvlc.connectionInfo(), mvlc.hardwareId(), mvlc.firmwareRevision());

    return 0;
}

static const Command MvlcVersionCommand =
{
    .name = "version",
    .help = R"~(print MVLC hardware and firmware revisions)~",
    .exec = mvlc_version_command,
};

DEF_EXEC_FUNC(mvlc_stack_info_command)
{
    spdlog::trace("entered mvlc_version_command()");
    trace_log_parser_info(ctx.parser, "mvlc_version_command");

    auto [mvlc, ec] = make_default_mvlc_err_connect(argv);

    if (!mvlc || ec)
        return 1;

    for (unsigned stackId=1; stackId<stacks::StackCount; ++stackId)
    {
        auto [stackInfo, ec] = read_stack_info(mvlc, stackId);

        if (ec && ec != make_error_code(MVLCErrorCode::InvalidStackHeader))
        {
            std::cerr << fmt::format("Error reading stack info for stack#{}: {}\n",
                stackId, ec.message());
            return 1;
        }
        else if (ec == make_error_code(MVLCErrorCode::InvalidStackHeader))
        {
            std::cerr << fmt::format("- stack#{}: triggers=0x{:02x}, offset={}, startAddress=0x{:04x}"
                ", could not read stack contents: {}\n",
                stackId, stackInfo.triggers, stackInfo.offset, stackInfo.startAddress, ec.message());
        }
        else
        {
            std::cout << fmt::format("- stack#{}: triggers=0x{:02x}, offset={}, startAddress=0x{:04x}, len={}:\n",
                stackId, stackInfo.triggers, stackInfo.offset, stackInfo.startAddress,
                stackInfo.contents.size());

            //for (auto &word: stackInfo.contents)
            //    std::cout << fmt::format("  0x{:08x}\n", word);
            //std::cout << "  --\n";

            auto stackBuilder = stack_builder_from_buffer(stackInfo.contents);
            for (const auto &cmd: stackBuilder.getCommands())
                std::cout << "  " << to_string(cmd) << "\n";
        }
    }

    return 0;
}

static const Command MvlcStackInfoCommand =
{
    .name = "stack_info",
    .help = R"~(retrieve and print the readout stacks from an MVLC)~",
    .exec = mvlc_stack_info_command,
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
        print help text for the given command.

    list-commands | help -a
        print list of available commands

MVLC low level commands:

    version
        print MVLC hardware and firmware revisions

    status
        gather status and version information from MVLC

    read_register
        read an internal register

    write_register
        write an internal register

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


    spdlog::set_level(spdlog::level::critical);
    mesytec::mvlc::set_global_log_level(spdlog::level::critical);

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser parser({"-h", "--help", "--log-level"});
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
        {
            logLevelName = str_tolower(logLevelName);
            spdlog::set_level(spdlog::level::from_str(logLevelName));
            mesytec::mvlc::set_global_log_level(spdlog::level::from_str(logLevelName));
        }
    }

    trace_log_parser_info(parser);

    CliContext ctx;
    ctx.generalHelp = generalHelp;

    ctx.commands.insert(HelpCommand);
    ctx.commands.insert(ListCmdsCommand);
    ctx.commands.insert(MvlcVersionCommand);
    ctx.commands.insert(MvlcStackInfoCommand);
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