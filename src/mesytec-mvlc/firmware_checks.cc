#include "firmware_checks.h"
#include "mesytec_vme_modules.h"

namespace mesytec::mvlc::firmware_checks
{
    bool is_supported(u16 hardwareId, u16 firmwareRevision)
    {
        const u16 minRequired = minimum_required_firmware(hardwareId);
        return firmwareRevision >= minRequired;
    }

    u16 minimum_required_firmware(u16 hardwareId)
    {
        switch (hardwareId)
        {
            case vme_modules::HardwareIds::MVLC:
                return 0x0037u;

            // Cases for MCPD and MDLL can be added here in the future.

            default:
                break;
        }

        return 0u;
    }
}
