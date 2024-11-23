#include "mvlc_impl_usb_internal.h"

#include <cassert>
#include <string_view>

#include "mvlc_error.h"

namespace
{
using namespace mesytec::mvlc;

class FTErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "ftd3xx";
    }

    std::string message(int ev) const override
    {
        switch (static_cast<_FT_STATUS>(ev))
        {
            case FT_OK:                                 return "FT_OK";
            case FT_INVALID_HANDLE:                     return "FT_INVALID_HANDLE";
            case FT_DEVICE_NOT_FOUND:                   return "FT_DEVICE_NOT_FOUND";
            case FT_DEVICE_NOT_OPENED:                  return "FT_DEVICE_NOT_OPENED (is it open in another application?)";
            case FT_IO_ERROR:                           return "FT_IO_ERROR";
            case FT_INSUFFICIENT_RESOURCES:             return "FT_INSUFFICIENT_RESOURCES";
            case FT_INVALID_PARAMETER: /* 6 */          return "FT_INVALID_PARAMETER";
            case FT_INVALID_BAUD_RATE:                  return "FT_INVALID_BAUD_RATE";
            case FT_DEVICE_NOT_OPENED_FOR_ERASE:        return "FT_DEVICE_NOT_OPENED_FOR_ERASE";
            case FT_DEVICE_NOT_OPENED_FOR_WRITE:        return "FT_DEVICE_NOT_OPENED_FOR_WRITE";
            case FT_FAILED_TO_WRITE_DEVICE: /* 10 */    return "FT_FAILED_TO_WRITE_DEVICE";
            case FT_EEPROM_READ_FAILED:                 return "FT_EEPROM_READ_FAILED";
            case FT_EEPROM_WRITE_FAILED:                return "FT_EEPROM_WRITE_FAILED";
            case FT_EEPROM_ERASE_FAILED:                return "FT_EEPROM_ERASE_FAILED";
            case FT_EEPROM_NOT_PRESENT:                 return "FT_EEPROM_NOT_PRESENT";
            case FT_EEPROM_NOT_PROGRAMMED:              return "FT_EEPROM_NOT_PROGRAMMED";
            case FT_INVALID_ARGS:                       return "FT_INVALID_ARGS";
            case FT_NOT_SUPPORTED:                      return "FT_NOT_SUPPORTED";
            case FT_NO_MORE_ITEMS:                      return "FT_NO_MORE_ITEMS";
            case FT_TIMEOUT: /* 19 */                   return "FT_TIMEOUT";
            case FT_OPERATION_ABORTED:                  return "FT_OPERATION_ABORTED";
            case FT_RESERVED_PIPE:                      return "FT_RESERVED_PIPE";
            case FT_INVALID_CONTROL_REQUEST_DIRECTION:  return "FT_INVALID_CONTROL_REQUEST_DIRECTION";
            case FT_INVALID_CONTROL_REQUEST_TYPE:       return "FT_INVALID_CONTROL_REQUEST_TYPE";
            case FT_IO_PENDING:                         return "FT_IO_PENDING";
            case FT_IO_INCOMPLETE:                      return "FT_IO_INCOMPLETE";
            case FT_HANDLE_EOF:                         return "FT_HANDLE_EOF";
            case FT_BUSY:                               return "FT_BUSY";
            case FT_NO_SYSTEM_RESOURCES:                return "FT_NO_SYSTEM_RESOURCES";
            case FT_DEVICE_LIST_NOT_READY:              return "FT_DEVICE_LIST_NOT_READY";
            case FT_DEVICE_NOT_CONNECTED:               return "FT_DEVICE_NOT_CONNECTED";
            case FT_INCORRECT_DEVICE_PATH:              return "FT_INCORRECT_DEVICE_PATH";

            case FT_OTHER_ERROR:                        return "FT_OTHER_ERROR";
        }

        return "unknown FT error";
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<_FT_STATUS>(ev))
        {
            case FT_OK:
                return ErrorType::Success;

            case FT_INVALID_HANDLE:
            case FT_DEVICE_NOT_FOUND:
            case FT_DEVICE_NOT_OPENED:
            case FT_DEVICE_NOT_CONNECTED:
                return ErrorType::ConnectionError;

            case FT_IO_ERROR:
            case FT_INSUFFICIENT_RESOURCES:
            case FT_INVALID_PARAMETER:
            case FT_INVALID_BAUD_RATE:
            case FT_DEVICE_NOT_OPENED_FOR_ERASE:
            case FT_DEVICE_NOT_OPENED_FOR_WRITE:
            case FT_FAILED_TO_WRITE_DEVICE:
            case FT_EEPROM_READ_FAILED:
            case FT_EEPROM_WRITE_FAILED:
            case FT_EEPROM_ERASE_FAILED:
            case FT_EEPROM_NOT_PRESENT:
            case FT_EEPROM_NOT_PROGRAMMED:
            case FT_INVALID_ARGS:
            case FT_NOT_SUPPORTED:
            case FT_NO_MORE_ITEMS:
                return ErrorType::ConnectionError;

            case FT_TIMEOUT:
                return ErrorType::Timeout;

            case FT_OPERATION_ABORTED:
            case FT_RESERVED_PIPE:
            case FT_INVALID_CONTROL_REQUEST_DIRECTION:
            case FT_INVALID_CONTROL_REQUEST_TYPE:
            case FT_IO_PENDING:
            case FT_IO_INCOMPLETE:
            case FT_HANDLE_EOF:
            case FT_BUSY:
            case FT_NO_SYSTEM_RESOURCES:
            case FT_DEVICE_LIST_NOT_READY:
            case FT_INCORRECT_DEVICE_PATH:
            case FT_OTHER_ERROR:
                return ErrorType::ConnectionError;
        }

        assert(false);
        return {};
    }
};

const FTErrorCategory theFTErrorCategory {};

}

namespace mesytec::mvlc::usb
{

std::error_code make_error_code(FT_STATUS st)
{
    return { static_cast<int>(st), theFTErrorCategory };
}

static const size_t DataBufferSize = usb::USBStreamPipeReadSize;

std::pair<std::error_code, size_t> read_pipe_until_empty(
    MVLC_USB_Interface &impl, Pipe pipe, std::shared_ptr<spdlog::logger> &logger)
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
                       std::basic_string_view<u32>(reinterpret_cast<u32*>(buffer.data()), bytesTransferred/sizeof(u32)),
                       fmt::format("read_pipe_until_empty: pipe={}, ec={}, bytes={}, data:",
                                   static_cast<int>(pipe), ec.message(), bytesTransferred)
                       );
        }

        if (ec == ErrorType::ConnectionError)
            break;
    } while (bytesTransferred > 0);

    return std::make_pair(ec, totalBytesTransferred);
}

}
