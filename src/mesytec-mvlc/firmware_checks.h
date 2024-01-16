#ifndef B33BDDA4_93F6_4945_99F6_556AC9C8777B
#define B33BDDA4_93F6_4945_99F6_556AC9C8777B

#include "util/int_types.h"

namespace mesytec::mvlc::firmware_checks
{
    // Returns the minimum firmware revision recommended to use with the given
    // hardwareId.
    u16 minimum_recommended_firmware(u16 hardwareId);

    // Returns true if the given combination of hardwareId (0x6008) and
    // firmwareRevision (0x600e) is recommended to be used with this library.
    bool is_recommended_firmware(u16 hardwareId, u16 firmwareRevision);

}

#endif /* B33BDDA4_93F6_4945_99F6_556AC9C8777B */
