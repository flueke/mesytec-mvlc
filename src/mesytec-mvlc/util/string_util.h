#ifndef __MESYTEC_MVLC_STRING_UTIL_H__
#define __MESYTEC_MVLC_STRING_UTIL_H__

#include <algorithm>
#include <string>
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


} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_STRING_UTIL_H__ */
