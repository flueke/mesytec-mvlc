#ifndef SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H
#define SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H

#include <string>
#include <vector>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mesytec_vme_modules.h>
#include <mesytec-mvlc/mesytec-mvlc_export.h>

namespace mesytec::mvlc::scanbus
{

// The low 16 bits of the vme address to read from when scanning for devices.
// Note: The probe read does not have to yield useful data, it can even raise
// BERR. As long as there's no read timeout the address is considered for the
// info gathering stage.
static const u32 ProbeRegister = 0x0000u;

// Scans the addresses in the range [scanBaseBegin, scanBaseEnd) for (mesytec)
// vme modules. scanBaseBegin/End specify the upper 16 bits of the full 32-bit
// vme address.
// Returns a list of candidate addresses (addresses where the read was
// successful or resulted in BERR).
// Note: by default the stack sized used when scanning is limited to the size
// reserved for the immediate exec stack. This can be set to
// stacks::StackMemoryWords to make use of all stack memory which should speed
// up the scan considerably.
MESYTEC_MVLC_EXPORT std::vector<u32> scan_vme_bus_for_candidates(
    MVLC &mvlc,
    const u16 scanBaseBegin = 0u,
    const u16 scanBaseEnd   = 0xffffu,
    const u16 probeRegister = ProbeRegister,
    const u8 probeAmod = vme_amods::A32,
    const VMEDataWidth probeDataWidth = VMEDataWidth::D16,
    const unsigned maxStackSize = stacks::ImmediateStackReservedWords);

// Same as above using default parameters for everything but the max stack size.
inline std::vector<u32> scan_vme_bus_for_candidates_stacksize(
    MVLC &mvlc, const unsigned maxStackSize)
{
    return scan_vme_bus_for_candidates(mvlc, 0u, 0xffffu, ProbeRegister,
        vme_amods::A32, VMEDataWidth::D16, maxStackSize);
}

struct VMEModuleInfo
{
    u32 hwId;
    u32 fwId;

    std::string moduleTypeName() const
    {
        return vme_modules::hardware_id_to_module_name(hwId);
    }

    std::string mdppFirmwareTypeName() const
    {
        if (vme_modules::is_mdpp(hwId))
            return vme_modules::mdpp_firmware_name(vme_modules::mdpp_fw_type_val_from_reg(fwId));
        return {};
    }
};

MESYTEC_MVLC_EXPORT std::error_code read_module_info(MVLC &mvlc, u32 vmeAddress, VMEModuleInfo &dest);

}

#endif // SRC_MESYTEC_MVLC_SCANBUS_SUPPORT_H
