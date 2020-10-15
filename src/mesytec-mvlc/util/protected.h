#ifndef __MESYTEC_MVLC_PROTECTED_H__
#define __MESYTEC_MVLC_PROTECTED_H__

#include <condition_variable>
#include "ticketmutex.h"

namespace mesytec
{
namespace mvlc
{

// Protected and Access combine an object with a mutex and use RAII to manage
// lock lifetimes.

template<typename T> class Protected;

template<typename T>
class [[nodiscard]] Access
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
        explicit Protected(T &&obj)
            : m_obj(std::move(obj))
        {
        }

        Access<T> access()
        {
            return Access<T>(std::unique_lock<TicketMutex>(m_mutex), m_obj);
        }

        T copy() { return access().copy(); }

    private:
        TicketMutex m_mutex;
        T m_obj;
};

// WaitableProtected and WaitableAccess are like the above but in addition to
// the object and the mutex a condition_variable is used which allows waiting
// for the object to be modified.
// When an WaitableAccess object is destroyed it first unlocks the mutex and
// then uses notify_all() to wake up any thread waiting for the protected
// object to be modified.

template<typename T> class WaitableProtected;

template<typename T>
class WaitableAccess
{
    public:
        T &ref() { return m_obj; }
        const T & ref() const { return m_obj; }

        T *operator->() { return &m_obj; }
        T copy() const { return m_obj; }

        WaitableAccess(WaitableAccess &&) = default;
        WaitableAccess &operator=(WaitableAccess &&) = default;

        ~WaitableAccess()
        {
            m_lock.unlock();
            m_cond.notify_all();
        }

    private:
        friend class WaitableProtected<T>;

        WaitableAccess(std::unique_lock<TicketMutex> &&lock,
                       std::condition_variable_any &cond,
                       T &obj)
            : m_lock(std::move(lock))
            , m_cond(cond)
            , m_obj(obj)
        {
        }

        std::unique_lock<TicketMutex> m_lock;
        std::condition_variable_any &m_cond;
        T &m_obj;

};

template<typename T>
class WaitableProtected
{
    public:
        WaitableProtected(T && obj)
            : m_obj(std::move(obj))
        {
        }

        WaitableAccess<T> access()
        {
            return WaitableAccess<T>(
                std::unique_lock<TicketMutex>(m_mutex),
                m_cond,
                m_obj);
        }

        template<typename Predicate>
        WaitableAccess<T> wait(Predicate pred_)
        {
            std::unique_lock<TicketMutex> lock(m_mutex);

            auto pred = [this, &pred_] ()
            {
                return pred_(m_obj);
            };

            m_cond.wait(lock, pred);

            return WaitableAccess<T>(
                std::move(lock),
                m_cond,
                m_obj);
        }

        template<typename Predicate, typename Rep, typename Period>
        WaitableAccess<T> wait_for(
            const std::chrono::duration<Rep, Period> &duration,
            Predicate pred_)
        {
            std::unique_lock<TicketMutex> lock(m_mutex);

            auto pred = [this, &pred_] ()
            {
                return pred_(m_obj);
            };

            m_cond.wait_for(lock, duration, pred);

            return WaitableAccess<T>(
                std::move(lock),
                m_cond,
                m_obj);
        }

        template<typename Predicate, typename Rep, typename Period>
        WaitableAccess<T> wait_until(
            const std::chrono::duration<Rep, Period> &duration,
            Predicate pred_)
        {
            std::unique_lock<TicketMutex> lock(m_mutex);

            auto pred = [this, &pred_] ()
            {
                return pred_(m_obj);
            };

            m_cond.wait_until(lock, duration, pred);

            return WaitableAccess<T>(
                std::move(lock),
                m_cond,
                m_obj);
        }

    private:
        TicketMutex m_mutex;
        std::condition_variable_any m_cond;
        T m_obj;
};

}
}

#endif /* __PROTECTED_H__ */
