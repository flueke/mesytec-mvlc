#ifndef __MESYTEC_MVLC_PROTECTED_H__
#define __MESYTEC_MVLC_PROTECTED_H__

#include "ticketmutex.h"

namespace mesytec
{
namespace mvlc
{

template<typename T> class Protected;

template<typename T>
class Access
{
    public:
        T &ref() { return m_obj; }
        const T & ref() const { return m_obj; }

        T *operator->() { return &m_obj; }
        T copy() const { return m_obj; }

        Access(Access &&) = default;
        Access &operator=(Access &&) = default;

        ~Access()
        {
        }

    private:
        friend class Protected<T>;

        Access(std::unique_lock<TicketMutex> &&lock, T &obj)
            : m_lock(std::move(lock))
            , m_obj(obj)
        {
        }

        std::unique_lock<TicketMutex> m_lock;
        T &m_obj;
};

template<typename T>
class Protected
{
    public:
        Protected(T &&obj)
            : m_obj(obj)
        {
        }

        Access<T> access()
        {
            return Access<T>(std::unique_lock<TicketMutex>(m_mutex), m_obj);
        }

    private:
        TicketMutex m_mutex;
        T m_obj;
};

}
}

#endif /* __PROTECTED_H__ */
