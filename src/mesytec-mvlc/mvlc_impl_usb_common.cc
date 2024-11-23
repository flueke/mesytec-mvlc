#include "mvlc_impl_usb_common.h"
#include "mvlc_impl_usb_internal.h"
#include "util/logging.h"
#include "mvlc_dialog.h"
#include "mvlc_dialog_util.h"

#include <future>
#include <memory>
#include <regex>

namespace
{

// Returns an unfiltered list of all connected FT60X devices. */
mesytec::mvlc::usb::DeviceInfoList make_device_info_list()
{
    using mesytec::mvlc::usb::DeviceInfoList;
    using mesytec::mvlc::usb::DeviceInfo;

    DeviceInfoList result;

    DWORD numDevs = 0;
    FT_STATUS st = FT_CreateDeviceInfoList(&numDevs);

    if (st == FT_OK && numDevs > 0)
    {
        auto ftInfoNodes = std::make_unique<FT_DEVICE_LIST_INFO_NODE[]>(numDevs);
        st = FT_GetDeviceInfoList(ftInfoNodes.get(), &numDevs);

        if (st == FT_OK)
        {
            result.reserve(numDevs);

            for (DWORD ftIndex = 0; ftIndex < numDevs; ftIndex++)
            {
                const auto &infoNode = ftInfoNodes[ftIndex];
                DeviceInfo di = {};
                di.index = ftIndex;
                di.serial = infoNode.SerialNumber;
                di.description = infoNode.Description;

                if (infoNode.Flags & FT_FLAGS_OPENED)
                    di.flags |= DeviceInfo::Flags::Opened;

                if (infoNode.Flags & FT_FLAGS_HISPEED)
                    di.flags |= DeviceInfo::Flags::USB2;

                if (infoNode.Flags & FT_FLAGS_SUPERSPEED)
                    di.flags |= DeviceInfo::Flags::USB3;

                di.handle = infoNode.ftHandle;

                result.emplace_back(di);
            }
        }
    }

    return result;
}

#if 0
template<typename Impl>
std::pair<std::error_code, size_t> read_pipe_until_empty(
    Impl &impl, Pipe pipe, std::shared_ptr<spdlog::logger> &logger)
{
    size_t totalBytesTransferred = 0;
    size_t bytesTransferred = 0;
    std::array<u8, DataBufferSize> buffer;
    std::error_code ec = {};
    do
    {
        bytesTransferred = 0;
        ec = impl.read_unbuffered(pipe, buffer.data(), buffer.size(), bytesTransferred);
        totalBytesTransferred += bytesTransferred;

        if (logger)
        {
            logger->debug("read_pipe_until_empty: pipe={}, ec={}, bytes={}",
                          static_cast<int>(pipe), ec.message(), bytesTransferred);

            log_buffer(logger, spdlog::level::trace,
                       basic_string_view<u32>(reinterpret_cast<u32*>(buffer.data()), bytesTransferred/sizeof(u32)),
                       fmt::format("read_pipe_until_empty: pipe={}, ec={}, bytes={}, data:",
                                   static_cast<int>(pipe), ec.message(), bytesTransferred)
                       );
        }

        if (ec == ErrorType::ConnectionError)
            break;
    } while (bytesTransferred > 0);

    return std::make_pair(ec, totalBytesTransferred);
};
#endif

#ifdef __WIN32
std::error_code abort_pipe(void *ftdiHandle, Pipe pipe, mesytec::mvlc::usb::EndpointDirection dir)
{
    auto logger = get_logger("mvlc_usb");

    logger->trace(
        "FT_AbortPipe on pipe={}, dir={}",
        static_cast<unsigned>(pipe),
        static_cast<unsigned>(dir));

    auto st = FT_AbortPipe(ftdiHandle, get_endpoint(pipe, dir));

    if (auto ec = mesytec::mvlc::usb::make_error_code(st))
    {
        logger->warn(
            "FT_AbortPipe on pipe={}, dir={} returned an error: {}",
            static_cast<unsigned>(pipe),
            static_cast<unsigned>(dir),
            ec.message().c_str());
        return ec;
    }
    return {};
}
#endif // __WIN32

} // end anon namespace

namespace mesytec::mvlc::usb
{

DeviceInfoList get_device_info_list(const ListOptions opts)
{
    auto result = make_device_info_list();

    if (opts == ListOptions::MVLCDevices)
    {
        // Remove if the description does not contain "MVLC"
        auto it = std::remove_if(result.begin(), result.end(), [] (const DeviceInfo &di) {
            static const std::regex reDescr("MVLC");
            return !(std::regex_search(di.description, reDescr));
        });

        result.erase(it, result.end());
    }

    return result;
}

DeviceInfo get_device_info_by_serial(const DeviceInfoList &infoList, const std::string &serial)
{
    auto it = std::find_if(infoList.begin(), infoList.end(),
                           [&serial] (const DeviceInfo &di) {
        return di.serial == serial;
    });

    return it != infoList.end() ? *it : DeviceInfo{};
}

std::error_code check_chip_configuration(void *handle)
{
    FT_60XCONFIGURATION conf = {};

    FT_STATUS st = FT_GetChipConfiguration(handle, &conf);

    if (st != FT_OK)
        return make_error_code(st);

    if (conf.FIFOClock != CONFIGURATION_FIFO_CLK_100
        || conf.FIFOMode != CONFIGURATION_FIFO_MODE_600
        || conf.ChannelConfig != CONFIGURATION_CHANNEL_CONFIG_2
        || !(conf.PowerAttributes & 0x40) // self powered
        || !(conf.PowerAttributes & 0x20) // remote wakup
        || conf.OptionalFeatureSupport != CONFIGURATION_OPTIONAL_FEATURE_DISABLEALL)
    {
        return std::error_code(MVLCErrorCode::USBChipConfigError);
    }

    return {};
}

// USB specific post connect routine which tries to disable a potentially
// running DAQ. This is done to make sure the command communication is working
// properly and no readout data is clogging the USB.
// Steps:
// - Disable daq mode by writing 0 to the corresponding register.
//   Errors are ignored except ErrorType::ConnectionError which indicate that
//   we could not open the USB device or lost the connection.
// - Read from the command pipe until no more data arrives. Again only
//   ConnectionError type errors are considered fatal.
// - Read from the data pipe until no more data arrives. These can be delayed
//   responses from writing to the daq mode register or queued up stack error
//   notifications.
// - Do a register read to check that communication is ok now.
std::error_code post_connect_cleanup(Impl &impl)
{
    auto logger = get_logger("mvlc_usb");
    logger->debug("begin post_connect_cleanup");

    static const int DisableTriggerRetryCount = 5;

    mesytec::mvlc::MVLCDialog_internal dlg(&impl);

    // Disable daq mode. There may be timeouts due to the data pipe being
    // full and no command responses arriving on the command pipe. Also
    // notification data can be stuck in the command pipe so that the responses
    // are not parsed correctly.

    // Try writing the register in a separate thread. This uses the command pipe
    // for communication.
    auto fCmd = std::async(std::launch::async, [&] ()
    {
        std::pair<std::error_code, size_t> ret = {};

        for (int try_ = 0; try_ < DisableTriggerRetryCount; try_++)
        {
            if (auto ec = disable_daq_mode(dlg))
            {
                if (ec == ErrorType::ConnectionError)
                {
                    ret.first = ec;
                    break;
                }
            }
            else
                break;

            // Read any available data from the command pipe.
            auto rr = read_pipe_until_empty(impl, Pipe::Command, logger);
            ret.first = rr.first;
            ret.second += rr.second;

            if (ret.first == ErrorType::ConnectionError)
                break;
        }

        return ret;
    });

    auto fData = std::async(std::launch::async, [&] ()
    {
        // Read any available data from the data pipe.
        return read_pipe_until_empty(impl, Pipe::Data, logger);
    });

    auto rCmd = fCmd.get();
    auto rData = fData.get();

    logger->debug("pipe reading finished, data: ec={}, bytes={}", rData.first.message(), rData.second);
    logger->debug("pipe reading finished, cmd:  ec={}, bytes={}", rCmd.first.message(), rCmd.second);

    std::error_code ec = {};

    if (rCmd.first)
        ec = rCmd.first;
    else if (rData.first)
        ec = rData.first;

    if (ec == ErrorType::Timeout)
        ec = {};

    logger->debug("end post_connect_cleanup, final ec={}", ec.message());

    return ec;
}

}
