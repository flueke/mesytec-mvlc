#ifndef DC22CAE9_2F42_4791_8B90_EC5EEB17424A
#define DC22CAE9_2F42_4791_8B90_EC5EEB17424A

#include <chrono>

namespace mesytec::mvlc::util
{

class Stopwatch
{
public:
    using duration_type = std::chrono::microseconds;
    using clock_type = std::chrono::high_resolution_clock;

    explicit Stopwatch()
    {
        start();
    }

    void start()
    {
        tStart_ = tInterval_ = std::chrono::high_resolution_clock::now();
    }

    // Returns the elapsed time in the current interval and restarts the interval.
    duration_type interval()
    {
        auto now = clock_type::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tInterval_);
        tInterval_ = now;
        return result;
    }

    // Returns the elapsed time from start() to now.
    duration_type end()
    {
        auto now = clock_type::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tStart_);
        return result;
    }

    // Const version of interval(): returns the elapsed time in the current
    // interval without resetting it.
    duration_type get_interval() const
    {
        auto now = clock_type::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tInterval_);
        return result;
    }

    // Returns the elapsed time from start() to now without resetting the
    // current interval.
    duration_type get_elapsed() const
    {
        auto now = clock_type::now();
        return std::chrono::duration_cast<duration_type>(now - tStart_);
    }

private:
    clock_type::time_point tStart_;
    clock_type::time_point tInterval_;
};

}

#endif /* DC22CAE9_2F42_4791_8B90_EC5EEB17424A */
