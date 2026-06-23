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

// non-string sequence contains()
template <class, class = void>
struct is_indexed_range : std::false_type {};

template <class T>
struct is_indexed_range<T, std::void_t<
    decltype(std::declval<T&>().begin()),
    decltype(std::declval<T&>().end()),
    decltype(std::declval<T&>()[std::size_t{}])
>> : std::true_type {};

template <class T>
struct is_string_like : std::false_type {};

template <>
struct is_string_like<std::string> : std::true_type {};

template<class C, class V,
std::enable_if_t<is_indexed_range<C>::value && !is_string_like<C>::value, bool> = true>
bool contains(const C &container, const V &value)
{
    return std::find(container.begin(), container.end(), value) != container.end();
}

}

#endif /* BBB831A8_65B0_4E06_BE5B_CB69AE21959D */
