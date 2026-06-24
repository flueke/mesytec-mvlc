#ifndef D19D7A1C_7125_49E1_9102_C0A39E4F5FC5
#define D19D7A1C_7125_49E1_9102_C0A39E4F5FC5

namespace mesytec::mvlc::util
{

// From: https://en.cppreference.com/w/cpp/utility/variant/visit

// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

}

#endif /* D19D7A1C_7125_49E1_9102_C0A39E4F5FC5 */
