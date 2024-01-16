#include "firmware_checks.h"
#include "mesytec_vme_modules.h"

namespace mesytec::mvlc::firmware_checks
{
    u16 minimum_recommended_firmware(u16 hardwareId)
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

    bool is_recommended_firmware(u16 hardwareId, u16 firmwareRevision)
    {
        const u16 minRecommended = minimum_recommended_firmware(hardwareId);
        return firmwareRevision >= minRecommended;
    }

}
