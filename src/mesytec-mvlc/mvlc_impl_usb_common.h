#ifndef F3415BFF_8ECD_4E52_8C61_6BD268296B84
#define F3415BFF_8ECD_4E52_8C61_6BD268296B84

#include <string>
#include <system_error>
#include <vector>
#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_basic_interface.h"
#include "mesytec-mvlc/mvlc_usb_interface.h"
#include "mesytec-mvlc/util/int_types.h"

namespace mesytec::mvlc::usb
{

class MESYTEC_MVLC_EXPORT Impl;

struct DeviceInfo
{
    struct Flags
    {
        // Opened is set if the device is opened by some process at the time
        // the info is queried.
        static const u8 Opened = 1;
        static const u8 USB2   = 2;
        static const u8 USB3   = 4;
    };

    int index = -1;             // index value used by the FTDI lib for this device.
    std::string serial;         // usb serial number string
    std::string description;    // usb device description string
    u8 flags = 0;               // Flags bits
    void *handle = nullptr;     // FTDI handle if opened

    inline explicit operator bool() const { return index >= 0; }
};

using DeviceInfoList = std::vector<DeviceInfo>;

enum class ListOptions
{
    MVLCDevices,
    AllDevices,
};

MESYTEC_MVLC_EXPORT DeviceInfoList get_device_info_list(
    const ListOptions opts = ListOptions::MVLCDevices);

MESYTEC_MVLC_EXPORT DeviceInfo get_device_info_by_serial(
    const DeviceInfoList &infoList, const std::string &serial);

enum class EndpointDirection: u8
{
    In,
    Out
};

std::error_code check_chip_configuration(void *handle);
std::error_code post_connect_cleanup(Impl &impl);

}

#endif /* F3415BFF_8ECD_4E52_8C61_6BD268296B84 */
