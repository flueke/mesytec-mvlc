#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/scanbus_support.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::scanbus;


int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    bool opt_logDebug = false;
    bool opt_logTrace = false;
    std::string opt_mvlcEthHost;
    bool opt_mvlcUseFirstUSBDevice = true;
    std::string opt_scanBaseBegin   = "0",
                opt_scanBaseEnd     = "0xffff",
                opt_probeRegister   = "0",
                opt_probeAmod       = "0x09",
                opt_probeDataWidth  = "d16"
                ;

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
        | lyra::opt(opt_probeRegister, "addr")["--probe-register"]("register address to probe (low 16 bits of 32 bit vme address)")
        | lyra::opt(opt_probeAmod, "amod")["--probe-amod"]("vme amod to use when probing (defaults to 0x09)")
        | lyra::opt(opt_probeDataWidth, "dataWidth")["--probe-datawidth"]("vme datawidth to use when probing (d16|d32)")
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        cerr << "Error parsing command line arguments: " << cliParseResult.errorMessage() << endl;
        return 1;
    }

    if (opt_showHelp)
    {
        cout << "mvlc-vme-scan-bus: Scans the upper 64k VME bus addresses for active modules.\n"
                "                   Reports module type, firmware type and firmware revision for mesytec modules.\n"
             << cli << "\n";
        return 0;
    }


    set_global_log_level(spdlog::level::warn);
    if (auto defaultLogger = spdlog::default_logger())
    {
        defaultLogger->set_level(spdlog::level::info);
        defaultLogger->set_pattern("%v");
    }

    // logging setup
    if (opt_logDebug)
        set_global_log_level(spdlog::level::debug);

    if (opt_logTrace)
        set_global_log_level(spdlog::level::trace);

    u32 scanBaseBegin = std::stoul(opt_scanBaseBegin, nullptr, 0);
    u32 scanBaseEnd = std::stoul(opt_scanBaseEnd, nullptr, 0);
    u32 probeRegister = std::stoull(opt_probeRegister, nullptr, 0);
    u8 probeAmod = std::stoul(opt_probeAmod, nullptr, 0);

    if (scanBaseBegin > scanBaseEnd)
        std::swap(scanBaseBegin, scanBaseEnd);

    auto probeDataWidth = VMEDataWidth::D16;

    if (opt_probeDataWidth == "d16")
        probeDataWidth = VMEDataWidth::D16;
    else if (opt_probeDataWidth == "d32")
        probeDataWidth = VMEDataWidth::D32;
    else
    {
        cerr << "Error parsing --probe-datawidth, expected d16|d32\n";
        return 1;
    }

    spdlog::info("Scan range: [{:#06x}, {:#06x}), {} addresses, probeRegister={:#06x}, probeAmod={:#04x}, probeDataWidth={}",
        scanBaseBegin, scanBaseEnd, scanBaseEnd - scanBaseBegin, probeRegister, probeAmod, opt_probeDataWidth);

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

        auto candidates = scan_vme_bus_for_candidates(
            mvlc, scanBaseBegin, scanBaseEnd,
            probeRegister, probeAmod, probeDataWidth);

        if (!candidates.empty())
        {
            spdlog::info("Found {} module candidate addresses: {:#010x}", candidates.size(), fmt::join(candidates, ", "));

            for (auto addr: candidates)
            {
                VMEModuleInfo moduleInfo{};

                if (auto ec = mvlc.vmeRead(addr + FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    spdlog::info("Error checking address {#:010x}: {}", addr, ec.message());
                    continue;
                }

                if (auto ec = mvlc.vmeRead(addr + HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    spdlog::info("Error checking address {#:010x}: {}", addr, ec.message());
                    continue;
                }

                if (moduleInfo.hwId == 0 && moduleInfo.fwId == 0)
                {
                    if (auto ec = mvlc.vmeRead(addr + MVHV4FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
                    {
                        spdlog::info("Error checking address {#:010x}: {}", addr, ec.message());
                        continue;
                    }

                    if (auto ec = mvlc.vmeRead(addr + MVHV4HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
                    {
                        spdlog::info("Error checking address {#:010x}: {}", addr, ec.message());
                        continue;
                    }
                }

                auto msg = fmt::format("Found module at {:#010x}: hwId={:#06x}, fwId={:#06x}, type={}",
                    addr, moduleInfo.hwId, moduleInfo.fwId, moduleInfo.moduleTypeName());

                if (is_mdpp(moduleInfo.hwId))
                    msg += fmt::format(", mdpp_fw_type={}", moduleInfo.mdppFirmwareTypeName());

                spdlog::info(msg);
            }
        }
        else
            spdlog::info("scanbus did not find any mesytec VME modules");
    }
    catch (const std::exception &e)
    {
        cerr << "caught an exception: " << e.what() << endl;
        return 1;
    }
}
