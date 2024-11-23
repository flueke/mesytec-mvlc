#ifndef A713E15B_D034_4E5A_A532_5DDEF3CA3C27
#define A713E15B_D034_4E5A_A532_5DDEF3CA3C27

#include <system_error>
#include <ftd3xx.h>
#include "mvlc_impl_usb.h"
#include "util/logging.h"

namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std

namespace mesytec::mvlc::usb
{

std::error_code make_error_code(FT_STATUS st);
std::error_code post_connect_cleanup(mesytec::mvlc::usb::Impl &impl);
std::pair<std::error_code, size_t> read_pipe_until_empty(
    MVLC_USB_Interface &impl, Pipe pipe, std::shared_ptr<spdlog::logger> &logger);

}


#endif /* A713E15B_D034_4E5A_A532_5DDEF3CA3C27 */
