#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;

static const u32 HardwareIdRegister = 0x6008u;
static const u32 FirmwareRegister = 0x600eu;

static const u32 MVHV4HardwareIdRegister = 0x0108;
static const u32 MVHV4FirmwareRegister = 0x010e;

// The low 16 bits of the vme address to read from when scanning for devices.
// Note: The probe read does not have to yield useful data, it can even raise
// BERR. As long as there's no read timeout the address is considered for the
// info gathering stage.
static const u32 ProbeRegister = 0x0000u;

// Full 16 bit values of the hardware id register (0x6008).
struct HardwareIds
{
    static const u16 MADC_32 = 0x5002;
    static const u16 MQDC_32 = 0x5003;
    static const u16 MTDC_32 = 0x5004;
    static const u16 MDPP_16 = 0x5005;
    // The VMMRs use the exact same software, so the hardware ids are equal.
    // VMMR-8 is a VMMR-16 with the 8 high busses not yielding data.
    static const u16 VMMR_8  = 0x5006;
    static const u16 VMMR_16 = 0x5006;
    static const u16 MDPP_32 = 0x5007;
    static const u16 MVHV_4  = 0x5009;
};

// Firmware type is encoded in the highest nibble of the firmware register
// (0x600e). The lower nibbles contain the firmware revision. Valid for both
// MDPP-16 and MDPP-32 but not all packages exist for the MDPP-32.
enum class MDPP16_FirmwareType
{
    RCP  = 1,
    SCP  = 2,
    QDC  = 3,
    PADC = 4,
    CSI  = 5,
};

// Using an extra type here to be able to use custom MDPP-32 firmware ids in
// case they don't match up with the MDPP-16 ones in the future. Probably
// overkill but it's done...
using MDPP32_FirmwareType = MDPP16_FirmwareType;

struct MDPP_FirmwareInfo
{
    static const u32 Mask = 0xf000u;
    static const u32 Shift = 12u;
};

// Extracts the 'firmware type' value from the given firmware info register value.
inline unsigned mdpp_fw_type_val_from_reg(u16 fwReg)
{
    return (fwReg & MDPP_FirmwareInfo::Mask) >> MDPP_FirmwareInfo::Shift;
}

inline std::string hardware_id_to_module_name(u16 hwid)
{
    switch (hwid)
    {
        case HardwareIds::MADC_32: return "MADC-32";
        case HardwareIds::MQDC_32: return "MQDC-32";
        case HardwareIds::MTDC_32: return "MTDC-32";
        case HardwareIds::MDPP_16: return "MDPP-16";
        case HardwareIds::VMMR_8:  return "VMMR-8/16";
        case HardwareIds::MDPP_32: return "MDPP-32";
        case HardwareIds::MVHV_4:  return "MVHV-4";
    }
    return {};
}

inline std::string mdpp16_firmware_name(int fwType)
{
    switch (static_cast<MDPP16_FirmwareType>(fwType))
    {
        case MDPP16_FirmwareType::RCP:  return "RCP";
        case MDPP16_FirmwareType::SCP:  return "SCP";
        case MDPP16_FirmwareType::QDC:  return "QDC";
        case MDPP16_FirmwareType::PADC: return "PADC";
        case MDPP16_FirmwareType::CSI:  return "CSI";
    }
    return {};
}

inline std::string mdpp32_firmware_name(int fwType)
{
    switch (static_cast<MDPP32_FirmwareType>(fwType))
    {
        case MDPP32_FirmwareType::RCP:  break;
        case MDPP32_FirmwareType::SCP:  return "SCP";
        case MDPP32_FirmwareType::QDC:  return "QDC";
        case MDPP32_FirmwareType::PADC: return "PADC";
        case MDPP32_FirmwareType::CSI:  break;
    }
    return {};
}

inline bool is_mdpp16(u16 hwId) { return hwId == HardwareIds::MDPP_16; }
inline bool is_mdpp32(u16 hwId) { return hwId == HardwareIds::MDPP_32; }
inline bool is_mdpp(u16 hwId) { return is_mdpp16(hwId) || is_mdpp32(hwId); }

// Scans the addresses in the range [scanBaseBegin, scanBaseEnd) for (mesytec)
// vme modules. scanBaseBegin/End specify the upper 16 bits of the full 32-bit
// vme address.
// Returns a list of candidate addresses (addresses where the read was
// successful or resulted in BERR).
std::vector<u32> scan_vme_bus_for_candidates(
    MVLC &mvlc,
    const u16 scanBaseBegin = 0u,
    const u16 scanBaseEnd   = 0xffffu,
    const u16 probeRegister = 0u,
    const u8 probeAmod = vme_amods::A32,
    const VMEDataWidth probeDataWidth = VMEDataWidth::D16)
{
    std::vector<u32> response;
    std::vector<u32> result;

    // Note: 0xffff itself is never checked as that is taken by the MVLC itself.
    const u32 baseMax = scanBaseEnd;
    u32 base = scanBaseBegin;
    size_t nStacks = 0u;
    auto tStart = std::chrono::steady_clock::now();

    do
    {
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        u32 baseStart = base;

        while (get_encoded_stack_size(sb) < MirrorTransactionMaxContentsWords / 2 - 2
                && base < baseMax)
        {
            u32 readAddress = (base << 16) | (ProbeRegister & 0xffffu);
            sb.addVMERead(readAddress, probeAmod, probeDataWidth);
            ++base;
        }

        spdlog::trace("Executing stack. size={}, baseStart=0x{:04x}, baseEnd=0x{:04x}, #addresses={}",
            get_encoded_stack_size(sb), baseStart, base, base - baseStart);

        if (auto ec = mvlc.stackTransaction(sb, response))
            throw std::system_error(ec);

        ++nStacks;

        spdlog::trace("Stack result for baseStart=0x{:04x}, baseEnd=0x{:04x} (#addrs={}), response.size()={}\n",
            baseStart, base, base-baseStart, response.size());
        spdlog::trace("  response={:#010x}\n", fmt::join(response, ", "));

        if (!response.empty())
        {
            u32 respHeader = response[0];
            spdlog::trace("  responseHeader={:#010x}, decoded: {}", respHeader, decode_frame_header(respHeader));
            auto headerInfo = extract_frame_info(respHeader);

            if (headerInfo.flags & frame_flags::SyntaxError)
            {
                spdlog::warn("MVLC stack execution returned a syntax error. Scanbus results may be incomplete!");
            }
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

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - tStart));

    spdlog::info("Scanned {} addresses in {} ms using {} stack transactions",
        scanBaseEnd - scanBaseBegin, elapsed.count(), nStacks);

    return result;
}

struct VMEModuleInfo
{
    u32 hwId;
    u32 fwId;

    std::string moduleTypeName() const
    {
        return hardware_id_to_module_name(hwId);
    }

    std::string mdppFirmwareTypeName() const
    {
        if (is_mdpp16(hwId))
            return mdpp16_firmware_name(mdpp_fw_type_val_from_reg(fwId));
        if (is_mdpp32(hwId))
            return mdpp32_firmware_name(mdpp_fw_type_val_from_reg(fwId));
        return {};
    }
};

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
        cout << "mvlc vme-scan-bus: Scans the upper 64k VME bus addresses for active modules.\n"
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
