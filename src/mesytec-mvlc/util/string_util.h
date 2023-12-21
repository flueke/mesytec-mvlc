#ifndef __MESYTEC_MVLC_STRING_UTIL_H__
#define __MESYTEC_MVLC_STRING_UTIL_H__

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace mesytec
{
namespace mvlc
{
namespace util
{

inline std::string join(const std::vector<std::string> &parts, const std::string &sep = ", ")
{
    std::string result;

    auto it = parts.begin();

    while (it != parts.end())
    {
        result += *it++;

        if (it < parts.end())
            result += sep;
    }

    return result;
}

inline std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// inplace trim spaces
// Source: Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
inline std::string &trim(std::string &str)
{
    const char *spaces = " \n\r\t";
    str.erase(str.find_last_not_of(spaces) + 1);
    str.erase(0, str.find_first_not_of(spaces));
    return str;
}

inline std::string trimmed(std::string str)
{
    return trim(str);
}

// Helper for unindenting raw string literals.
// https://stackoverflow.com/a/24900770
inline std::string unindent(const char* p)
{
    std::string result;
    if (*p == '\n') ++p;
    const char* p_leading = p;
    while (std::isspace(*p) && *p != '\n')
        ++p;
    size_t leading_len = p - p_leading;
    while (*p)
    {
        result += *p;
        if (*p++ == '\n')
        {
            for (size_t i = 0; i < leading_len; ++i)
                if (p[i] != p_leading[i])
                    goto dont_skip_leading;
            p += leading_len;
        }
      dont_skip_leading: ;
    }
    return result;
}

// Parse string into unsigned type. Supports hex, octal, decimal.
template<typename T,
    std::enable_if_t<std::is_unsigned_v<T>, bool> = true>
std::optional<T> parse_unsigned(const std::string &str_)
{
    auto str = trimmed(str_);

    if (str.empty())
        return {};

    try
    {
        std::size_t pos = 0;
        auto parsed = std::stoull(str, &pos, 0);

        if (pos != str.size())
            return {}; // not all characters where used when parsing.

        if (parsed > std::numeric_limits<T>::max())
            return {}; // result does not fit into target type

        return static_cast<T>(parsed);
    }
    catch(const std::exception& e) { }

    return {};
}

} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_STRING_UTIL_H__ */
