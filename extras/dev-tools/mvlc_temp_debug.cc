#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

std::vector<u32> scan_vme_bus_for_candidates(MVLC &mvlc, u32 scanBaseBegin = 0, u32 scanBaseEnd = 0xffffu)
{
    std::vector<u32> response;
    std::vector<u32> result;

    // Note: 0xffff itself is never checked as that is taken by the MVLC itself.
    const u32 baseMax = scanBaseEnd;
    u32 base = scanBaseBegin;

    do
    {
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        u32 baseStart = base;

        while (get_encoded_stack_size(sb) < MirrorTransactionMaxContentsWords / 2 - 2
                && base < baseMax)
        {
            sb.addVMERead(base << 16, vme_amods::A32, VMEDataWidth::D16);
            ++base;
        }

        spdlog::trace("Executing stack. size={}, baseStart=0x{:04x}, baseEnd=0x{:04x}, #addresses={}",
            get_encoded_stack_size(sb), baseStart, base, base - baseStart);

        if (auto ec = mvlc.stackTransaction(sb, response))
            throw std::system_error(ec);

        spdlog::trace("Stack result for baseStart=0x{:04x}, baseEnd=0x{:04x} (#addrs={}), response.size()={}\n",
            baseStart, base, base-baseStart, response.size());
        spdlog::trace("  response={:#010x}\n", fmt::join(response, ", "));

        if (!response.empty())
        {
            u32 respHeader = response[0];
            spdlog::trace("  responseHeader={:#010x}, decoded: {}", respHeader, decode_frame_header(respHeader));
        }


        // +2 to skip over 0xF3 and the marker
        for (auto it = std::begin(response) + 2; it < std::end(response); ++it)
        {
            auto index = std::distance(std::begin(response) + 2, it);
            auto value = *it;
            const u32 addr = (baseStart + index) << 16;
            //spdlog::debug("index={}, addr=0x{:08x} value=0x{:08x}", index, addr, value);

            // In the error case the lowest byte contains the stack error line number, so it
            // needs to be masked out for this test.
            if ((value & 0xffffff00) != 0xffffff00)
            {
                result.push_back(addr);
                spdlog::trace("Found candidate address: index={}, value=0x{:08x}, addr={:#010x}", index, value, addr);
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

    std::string opt_scanBaseBegin = "0";
    std::string opt_scanBaseEnd   = "0xffff";

    auto cli
        = lyra::help(opt_showHelp)

        | lyra::opt(opt_mvlcEthHost, "hostname")
            ["--mvlc-eth"] ("mvlc ethernet hostname")

        | lyra::opt(opt_mvlcUseFirstUSBDevice)
            ["--mvlc-usb"] ("connect to the first mvlc usb device")

        | lyra::opt(opt_logDebug)["--debug"]("enable debug logging")

        | lyra::opt(opt_logTrace)["--trace"]("enable trace logging")

        | lyra::opt(opt_scanBaseBegin, "addr")["--scan-begin"]("first scan base address")
        | lyra::opt(opt_scanBaseEnd, "addr")["--scan-end"]("one past last scan base address")

        // This shit never works :(
        //| lyra::arg(opt_scanBaseBegin, "scanBaseBegin")("first scan base address").required()
        //| lyra::arg(opt_scanBaseEnd, "scanBaseEnd")("one past last scan base address").required()
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

    u32 scanBaseBegin = std::stoul(opt_scanBaseBegin, nullptr, 0);
    u32 scanBaseEnd = std::stoul(opt_scanBaseEnd, nullptr, 0);


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

        if (auto candidates = scan_vme_bus_for_candidates(mvlc, scanBaseBegin, scanBaseEnd);
            !candidates.empty())
        {
            spdlog::debug("Found module candidate addresses: {:#010x}", fmt::join(candidates, ", "));
        }
        else
            spdlog::info("scanbus did not find any mesytec VME modules");

        spdlog::info("Addresses scanned: [{:#06x}, {:#06x}), count={}",
                     scanBaseBegin, scanBaseEnd, scanBaseEnd - scanBaseBegin);
    }
    catch (const std::exception &e)
    {
        cerr << "caught an exception: " << e.what() << endl;
        return 1;
    }
}
