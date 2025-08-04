#ifndef A8121AE2_6B5D_4523_A4A1_CE0854D6FA19
#define A8121AE2_6B5D_4523_A4A1_CE0854D6FA19

#include <type_traits>

namespace mesytec::mvlc::util
{

// Helper to convert a strongly typed enum to its underlying type.
// Example: std::cout << foo(to_underlying(b::B2)) << std::endl;
// Source: https://stackoverflow.com/a/8357462
template <typename E> constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}

} // namespace mesytec::mvlc::util

#endif /* A8121AE2_6B5D_4523_A4A1_CE0854D6FA19 */
