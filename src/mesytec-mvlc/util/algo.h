#ifndef BBB831A8_65B0_4E06_BE5B_CB69AE21959D
#define BBB831A8_65B0_4E06_BE5B_CB69AE21959D

namespace mesytec::mvlc::util
{

template<typename It1, typename It2, typename BinaryOp>
void for_each(It1 first1, It1 last1, It2 first2, BinaryOp f)
{
    for (; first1 != last1; ++first1, ++first2)
        f(*first1, *first2);
}

}

#endif /* BBB831A8_65B0_4E06_BE5B_CB69AE21959D */
