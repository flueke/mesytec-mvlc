// Fore some info on how to use argh effectively see
// https://a4z.gitlab.io/blog/2019/04/30/Git-like-sub-commands-with-argh.html

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>

using Action = std::function<int (int, const char**)>;

struct Command
{
  std::string name ;
  std::string help;
  Action exec;
};

struct CommandLessThan
{
    bool operator()(const Command &first, const Command &second)
    {
        return first.name < second.name;
    }
};

using Commands = std::set<Command, CommandLessThan> ;

static Commands gCommands;

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    std::string generalHelp = R"~(
usage: mvlc-cli [-v | --version] [-h | --help] [--log-level=(error|warn|info|debug|trace)]
                [--mvlc <url> | --mvlc-usb | --mvlc-usb-index <index> |
                 --mvlc-usb-serial <serial> | --mvlc-eth <hostname>]
                <command> [<args>]

Core Commands:
    help <command>
        print help text for the given command.

    commands
        print list of available commands

    status
        gather status and version information from MVLC

    version
        print MVLC hardware and firmware revisions

MVLC connections

    mvlc-cli supports the following URL schemes with --mvlc <url> to connect to MVLCs:
        usb://                   Use the first USB device
        usb://<serial-string>    USB device matching the given serial number
        usb://@<index>           USB device with the given logical FTDI driver index
        eth://<hostname/ip>      ETH/UDP with a hostname or an ip-address
        udp://<hostname/ip>      ETH/UDP with a hostname or an ip-address
        hostname                 No scheme part -> interpreted as a hostname for ETH/UDP

    Alternatively the transport specific options --mvlc-usb, --mvlc-usb-index,
    --mvlc-usb-serial and --mvlc-eth may be used.
)~";

    if (argc < 2)
    {
        std::cout << generalHelp;
        return 1;
    }

    argh::parser args(argv, argh::parser::PREFER_FLAG_FOR_UNREG_OPTION | argh::parser::SINGLE_DASH_IS_MULTIFLAG);
    auto posArgs = args.pos_args();

    std::cout << posArgs[0] << std::endl;
    std::cout << posArgs.size() << std::endl;
    std::cout << args[1] << std::endl;

    //if (args.pos_args().size() <= 2 && (args[1] == "-h" || args[1] == "--help"))

    //if (args[{"-h", "--help"}])

    return 0;
}

#if 0
int main(int argc, char *argv[])
{
    auto mvlc = make_mvlc_usb();

    if (auto ec = mvlc.connect())
    {
        std::cerr << fmt::format("Error connection to MVLC: {}\n", ec.message());
        return 1;
    }

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
#endif
