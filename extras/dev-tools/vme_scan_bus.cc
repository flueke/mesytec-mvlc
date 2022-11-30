#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

std::vector<u32> scan_vme_bus_for_candidates(MVLC &mvlc)
{
        //u32 fwReg = base + 0x600e;
    StackCommandBuilder sb;
    sb.addWriteMarker(0x13370001u);
    std::vector<u32> response;
    std::vector<u32> result;

    // Note: 0xffff itself is never checked as that is taken by the MVLC itself.
    const u32 baseMax = 0xffffu;
    u32 base = 0;

    do
    {
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        u32 baseStart = base;

        while (get_encoded_stack_size(sb) < MirrorTransactionMaxContentsWords / 2 - 2
                && base < baseMax)
        {
            u32 hwReg = (base++ << 16) + 0x6008;
            sb.addVMERead(hwReg, vme_amods::A32, VMEDataWidth::D16);
            ++base;
        }

        if (auto ec = mvlc.stackTransaction(sb, response))
            throw ec;

        spdlog::info("base={:#06x}, response={:#010x}, response size={}", base, fmt::join(response, ", "), response.size());

        // +2 to skip over 0xF3 and the marker
        for (auto it = std::begin(response) + 2; it < std::end(response); ++it)
        {
            auto index = std::distance(std::begin(response) + 2, it);
            auto value = *it;
            if ((value & 0xffffff00) != 0xffffff00)
            {
                u32 addr = (baseStart + index) << 16;
                result.push_back(addr);
                spdlog::info("index={}, value=0x{:08x}, addr={:#010x}", index, value, addr);
            }
        }

        response.clear();

    } while (base < baseMax);

    return result;
}

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    std::string opt_mvlcEthHost;
    bool opt_mvlcUseFirstUSBDevice = true;

    auto cli
        = lyra::help(opt_showHelp)

        | lyra::opt(opt_mvlcEthHost, "hostname")
            ["--mvlc-eth"] ("mvlc ethernet hostname")

        | lyra::opt(opt_mvlcUseFirstUSBDevice)
            ["--mvlc-usb"] ("connect to the first mvlc usb device")

        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")

        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << cli << endl;
        return 0;
    }

    // logging setup
    if (opt_logDebug)
        set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        set_global_log_level(spdlog::level::trace);

    try
    {
        MVLC mvlc;

        if (!opt_mvlcEthHost.empty())
            mvlc = make_mvlc_eth(opt_mvlcEthHost);
        else
            mvlc = make_mvlc_usb();

        if (auto ec = mvlc.connect())
        {
            cerr << "Error connecting to MVLC: " << ec.message() << endl;
            return 1;
        }

        scan_vme_bus_for_candidates(mvlc);
    }
    catch (const std::exception &e)
    {
        cerr << "caught an exception: " << e.what() << endl;
        return 1;
    }
}