#ifndef BBB831A8_65B0_4E06_BE5B_CB69AE21959D
#define BBB831A8_65B0_4E06_BE5B_CB69AE21959D

#include <type_traits>

namespace mesytec::mvlc::util
{

template<typename It1, typename It2, typename BinaryOp>
void for_each(It1 first1, It1 last1, It2 first2, BinaryOp f)
{
    for (; first1 != last1; ++first1, ++first2)
        f(*first1, *first2);
}

// Type trait to detect map-like containers (has key_type and mapped_type)
template<typename, typename = void>
struct is_map_like : std::false_type {};

template<typename T>
struct is_map_like<T, std::void_t<
    typename T::key_type,
    typename T::mapped_type,
    decltype(std::declval<const T&>().find(std::declval<const typename T::key_type&>())),
    decltype(std::declval<const T&>().end())
>> : std::true_type {};

template<typename T>
inline constexpr bool is_map_like_v = is_map_like<T>::value;

// map_contains: restricted to map-like containers
template<typename M, typename K,
         typename = std::enable_if_t<is_map_like_v<M>>>
bool contains(const M &map, const K &key)
{
    return map.find(key) != map.end();
}

}

#endif /* BBB831A8_65B0_4E06_BE5B_CB69AE21959D */
