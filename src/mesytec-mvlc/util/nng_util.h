#ifndef A4EB644B_226B_4B4C_8576_B4BCE738EED5
#define A4EB644B_226B_4B4C_8576_B4BCE738EED5

#ifndef MESYTEC_MVLC_PLATFORM_WINDOWS
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

#include <nng/nng.h>
#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/util/fmt.h>

namespace mesytec::mvlc
{

struct NngException: public std::runtime_error
{
    explicit NngException(int nng_rv_)
        : std::runtime_error(fmt::format("libnng error: {}", nng_strerror(nng_rv_)))
        , nng_rv(nng_rv_)
    {
    }

    int nng_rv;
};

inline std::string nng_sockaddr_to_string(const nng_sockaddr &addr)
{
    char buf[INET_ADDRSTRLEN];
    switch (addr.s_family)
    {
    case NNG_AF_INET:
        inet_ntop(AF_INET, &addr.s_in.sa_addr, buf, sizeof(buf));
        return fmt::format("tcp://{}:{}", buf, ntohs(addr.s_in.sa_port));
    case NNG_AF_INET6:
        inet_ntop(AF_INET6, &addr.s_in6.sa_addr, buf, sizeof(buf));
        return fmt::format("tcp://{}:{}", buf, ntohs(addr.s_in.sa_port));
    case NNG_AF_IPC:
        return fmt::format("ipc://{}", addr.s_ipc.sa_path);
    case NNG_AF_INPROC:
        return fmt::format("inproc://{}", addr.s_inproc.sa_name);
    }

    return {};
}

}

#endif /* A4EB644B_226B_4B4C_8576_B4BCE738EED5 */
