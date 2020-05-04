#ifndef __MESYTEC_MVLC_IO_UTIL_H__
#define __MESYTEC_MVLC_IO_UTIL_H__

// FIXME: this requires fmt-header-only to be publicly available
#include <fmt/format.h>
#include <iostream>
#include <string>

namespace mesytec
{
namespace mvlc
{
namespace util
{

template<typename Out, typename View>
Out &log_buffer(Out &out, const View &buffer, const std::string &header = {})
{
    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << std::endl;

    for (const auto &value: buffer)
        out << fmt::format("  0x{:008X}", value) << std::endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << std::endl;

    return out;
}

} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_IO_UTIL_H__ */
