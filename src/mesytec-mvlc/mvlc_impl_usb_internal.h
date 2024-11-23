#ifndef A713E15B_D034_4E5A_A532_5DDEF3CA3C27
#define A713E15B_D034_4E5A_A532_5DDEF3CA3C27

#include <system_error>
#include <ftd3xx.h>

namespace std
{
    template<> struct is_error_code_enum<_FT_STATUS>: true_type {};
} // end namespace std

namespace mesytec::mvlc::usb
{

std::error_code make_error_code(FT_STATUS st);

}


#endif /* A713E15B_D034_4E5A_A532_5DDEF3CA3C27 */
