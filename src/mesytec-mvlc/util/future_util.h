#ifndef __MESYTEC_MVLC_FUTURE_UTIL_H__
#define __MESYTEC_MVLC_FUTURE_UTIL_H__

#include <chrono>
#include <future>

template<typename R>
bool is_ready(std::future<R> const& f)
{
    //return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;

    // Faster alternative:
    return (f.wait_until(std::chrono::steady_clock::time_point::min())
            == std::future_status::ready);
}

#endif /* __MESYTEC_MVLC_FUTURE_UTIL_H__ */
