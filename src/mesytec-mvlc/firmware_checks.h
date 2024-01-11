#ifndef B33BDDA4_93F6_4945_99F6_556AC9C8777B
#define B33BDDA4_93F6_4945_99F6_556AC9C8777B

#include "util/int_types.h"

namespace mesytec::mvlc::firmware_checks
{
    // Returns true if the given combination of hardwareId (0x6008) and
    // firmwareRevision (0x600e) is supported by this library.
    bool is_supported(u16 hardwareId, u16 firmwareRevision);

    // Returns the minimum firmware revision required by this library for the
    // given hardwareId.
    u16 minimum_required_firmware(u16 hardwareId);
}

#endif /* B33BDDA4_93F6_4945_99F6_556AC9C8777B */
