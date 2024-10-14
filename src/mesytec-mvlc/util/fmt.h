#ifndef __MESYTEC_MVLC_FMT_H__
#define __MESYTEC_MVLC_FMT_H__

#include <spdlog/fmt/fmt.h>

template <typename First, typename Second> struct fmt::formatter<std::pair<First, Second>>: formatter<string_view>
{
    auto format(const std::pair<First, Second> &p, format_context &ctx) const
    {
        return format_to(ctx.out(), "({}, {})", p.first, p.second);
    }
};

#endif /* __MESYTEC_MVLC_FMT_H__ */
