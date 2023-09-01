#ifndef SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H
#define SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H

#include <string>
#include <vector>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "util/int_types.h"

// Placing the VME module constants in the 'scanbus' namespace for now. At some
// point a separate repo with its own namespace should be created for mesytec
// vme module (meta) data.
namespace mesytec::mvlc::scanbus
{

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
    static const u16 MVLC    = 0x5008;
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
        case HardwareIds::MVLC:    return "MVLC";
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
    const u16 probeRegister = ProbeRegister,
    const u8 probeAmod = vme_amods::A32,
    const VMEDataWidth probeDataWidth = VMEDataWidth::D16)
{
    std::vector<u32> response;
    std::vector<u32> result;

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
                && base <= baseMax)
        {
            u32 readAddress = (base << 16) | (probeRegister & 0xffffu);
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

    } while (base <= baseMax);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - tStart));

    spdlog::info("Scanned {} addresses in {} ms using {} stack transactions",
        scanBaseEnd - scanBaseBegin + 1, elapsed.count(), nStacks);

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

}

#endif // SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H
