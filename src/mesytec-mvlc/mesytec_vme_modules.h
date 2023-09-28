#ifndef MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESYTEC_VME_MODULES_H
#define MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESYTEC_VME_MODULES_H

#include <string>
#include <mesytec-mvlc/util/int_types.h>

namespace mesytec::mvlc::vme_modules
{

static const u32 HardwareIdRegister = 0x6008u;
static const u32 FirmwareRegister = 0x600eu;

static const u32 MVHV4HardwareIdRegister = 0x0108;
static const u32 MVHV4FirmwareRegister = 0x010e;

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
enum class MDPP_FirmwareType
{
    RCP  = 1,
    SCP  = 2,
    QDC  = 3,
    PADC = 4,
    CSI  = 5,
};

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

inline std::string mdpp_firmware_name(int fwType)
{
    switch (static_cast<MDPP_FirmwareType>(fwType))
    {
        case MDPP_FirmwareType::RCP:  return "RCP";
        case MDPP_FirmwareType::SCP:  return "SCP";
        case MDPP_FirmwareType::QDC:  return "QDC";
        case MDPP_FirmwareType::PADC: return "PADC";
        case MDPP_FirmwareType::CSI:  return "CSI";
    }
    return {};
}

inline bool is_mdpp16(u16 hwId) { return hwId == HardwareIds::MDPP_16; }
inline bool is_mdpp32(u16 hwId) { return hwId == HardwareIds::MDPP_32; }
inline bool is_mdpp(u16 hwId) { return is_mdpp16(hwId) || is_mdpp32(hwId); }

}

#endif // MESYTEC_MVLC_SRC_MESYTEC_MVLC_MESYTEC_VME_MODULES_H
