#ifndef __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__
#define __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__

#include <cstdint>

namespace mesytec
{
namespace mvlc
{
namespace util
{

inline constexpr std::size_t Kilobytes(std::size_t x) { return x * 1024u; }
inline constexpr std::size_t Megabytes(std::size_t x) { return Kilobytes(x) * 1024u; }
inline constexpr std::size_t Gigabytes(std::size_t x) { return Megabytes(x) * 1024u; }

} // end namespace util
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__ */
