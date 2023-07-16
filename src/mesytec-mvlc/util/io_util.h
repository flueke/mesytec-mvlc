#ifndef __MESYTEC_MVLC_IO_UTIL_H__
#define __MESYTEC_MVLC_IO_UTIL_H__

#include <iostream>
#include <string>
#include "mesytec-mvlc/util/fmt.h"
#include "mesytec-mvlc/util/int_types.h"
#include "mesytec-mvlc/util/string_view.hpp"

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
        out << fmt::format("  0x{:008x}", value) << std::endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << std::endl;

    return out;
}

template<typename Out, typename View>
Out &log_buffer(Out &out, const View &buffer, const std::string &header,
                size_t numStartWords, size_t numEndWords)
{
    numStartWords = std::min(numStartWords, buffer.size());
    numEndWords = std::min(numEndWords, buffer.size());


    out << "begin buffer '" << header << "' (size=" << buffer.size() << ")" << std::endl;

    out << numStartWords << " first words:" << std::endl;

    for (size_t i=0; i<numStartWords; i++)
        out << fmt::format("  0x{:008x}", buffer[i]) << std::endl;

    out << numEndWords << " last words:" << std::endl;

    for (size_t i=buffer.size() - (numEndWords + 1); i < buffer.size(); i++)
        out << fmt::format("  0x{:008x}", buffer[i]) << std::endl;

    out << "end buffer " << header << "' (size=" << buffer.size() << ")" << std::endl;

    return out;
}

template<typename Out>
Out &log_buffer(Out &out, const u32 *buffer, size_t size, const std::string &header = {})
{
    return log_buffer(out, nonstd::basic_string_view<const u32>(buffer, size), header);
}

} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_IO_UTIL_H__ */
