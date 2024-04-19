#include "udp_sockets.h"

#ifndef __WIN32
    #include <netdb.h>
    #include <sys/stat.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>

    #ifdef __linux__
        #include <sys/prctl.h>
        #include <linux/netlink.h>
        #include <linux/rtnetlink.h>
        #include <linux/inet_diag.h>
        #include <linux/sock_diag.h>
    #endif

    #include <arpa/inet.h>
#else // __WIN32
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <fcntl.h>
    #include <mmsystem.h>
#endif

#include <cassert>
#include <cstring>
#include <sstream>

#include <spdlog/spdlog.h>

namespace
{

static const unsigned DefaultWriteTimeout_ms = 500;
static const unsigned DefaultReadTimeout_ms  = 500;

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const size_t MaxPayloadSize = 1500 - 20 - 8;

struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
}

static bool socketSystemInitialized = false;

void init_socket_system()
{
    if (!socketSystemInitialized)
    {
#ifdef __WIN32
        WORD wVersionRequested;
        WSADATA wsaData;
        wVersionRequested = MAKEWORD(2, 1);
        int res = WSAStartup(wVersionRequested, &wsaData);
        if (res != 0)
            spdlog::error("init_socket_system(): WSAStartup failed: {}", gai_strerror(res));
#endif
        socketSystemInitialized = true;
    }
}

}

namespace mesytec::mvlc::eth
{

int connect_udp_socket(const std::string &host, u16 port, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    struct sockaddr_in addr = {};

    if ((ec = lookup(host, port, addr)))
        return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
            ec = std::error_code(errno, std::system_category());
            close_socket(sock);
            return -1;
        }
    }

    // connect
    if (::connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)))
    {
        ec = std::error_code(errno, std::system_category());
        close_socket(sock);
        return -1;
    }

    // set the socket timeouts
    if ((ec = set_socket_read_timeout(sock, DefaultReadTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    if ((ec = set_socket_write_timeout(sock, DefaultWriteTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    return sock;
}

int bind_udp_socket(u16 localPort, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;
        localAddr.sin_port = htons(localPort);

        if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
            ec = std::error_code(errno, std::system_category());
            close_socket(sock);
            return -1;
        }
    }

    // set the socket timeouts
    if ((ec = set_socket_read_timeout(sock, DefaultReadTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    if ((ec = set_socket_write_timeout(sock, DefaultWriteTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    return sock;
}

u16 get_local_socket_port(int sock, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    struct sockaddr_in localAddr = {};
    socklen_t localAddrLen = sizeof(localAddr);

    if (::getsockname(
            sock, reinterpret_cast<struct sockaddr *>(&localAddr),
            &localAddrLen) != 0)
    {
        ec = std::error_code(errno, std::system_category());
        return 0u;
    }

    u16 localPort = ntohs(localAddr.sin_port);

    return localPort;
}

std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    // FIXME (maybe): find a better error code to return here
    if (host.empty())
        return std::make_error_code(std::errc::not_connected);

    init_socket_system();

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    if (rc != 0)
    {
        #ifdef __WIN32
        spdlog::error("getaddrinfo(): host={}, error={}", host, gai_strerror(rc));
        #endif
        // FIXME (maybe): find a better error code to return here
        return std::make_error_code(std::errc::not_connected);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(dest))
        {
            std::memcpy(&dest, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp)
        // FIXME (maybe): find a better error code to return here
        return std::make_error_code(std::errc::not_connected);

    return {};
}

#ifndef __WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    struct timeval tv = ms_to_timeval(ms);

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#else // WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    init_socket_system();

    DWORD optval = ms;
    int res = setsockopt(sock, SOL_SOCKET, optname,
                         reinterpret_cast<const char *>(&optval),
                         sizeof(optval));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#endif

std::error_code set_socket_write_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

std::error_code set_socket_read_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

#ifndef __WIN32
std::error_code close_socket(int sock)
{
    int res = ::close(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#else // WIN32
std::error_code close_socket(int sock)
{
    int res = ::closesocket(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#endif

// Note: it is not necessary to split writes into multiple calls to send()
// because outgoing MVLC command buffers have to be smaller than the maximum,
// non-jumbo ethernet MTU.
// The send() call should return EMSGSIZE if the payload is too large to be
// atomically transmitted.
#ifdef __WIN32
std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred, int timeout_ms)
{
    assert(size <= MaxPayloadSize);

    init_socket_system();

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return SocketErrorCode::SocketWriteTimeout;

        // Maybe TODO: use WSAGetLastError here with a WSA specific error
        // category like this: https://gist.github.com/bbolli/710010adb309d5063111889530237d6d
        return SocketErrorCode::GenericSocketError;
    }

    bytesTransferred = res;
    return {};
}
#else // !__WIN32
std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::error_code(EAGAIN, std::system_category());

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif // !__WIN32

#ifdef __WIN32
std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
    size_t &bytesTransferred, int timeout_ms, sockaddr_in *src_addr)
{
    init_socket_system();

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
        return SocketErrorCode::SocketReadTimeout;

    if (sres == SOCKET_ERROR)
        return SocketErrorCode::GenericSocketError;

    // Not sure why but windows returns EFAULT (10014) when passing the the
    // src_addr pointer directly to recvfrom(). Using a local sockaddr_in
    // structure works.
    struct sockaddr_in srcAddr;
    int srcAddrSize = sizeof(srcAddr);

    ssize_t res = ::recvfrom(sockfd, reinterpret_cast<char *>(dest), size, 0,
        reinterpret_cast<struct sockaddr *>(&srcAddr), &srcAddrSize);

    if (src_addr)
        std::memcpy(src_addr, &srcAddr, std::min(srcAddrSize, static_cast<int>(sizeof(*src_addr))));

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return SocketErrorCode::SocketReadTimeout;

        return SocketErrorCode::GenericSocketError;
    }

    bytesTransferred = res;

    return {};
}
#else
std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
    size_t &bytesTransferred, int /*timeout_ms*/, sockaddr_in *src_addr)
{
    bytesTransferred = 0u;

    socklen_t addrlen = sizeof(sockaddr_in);
    ssize_t res = ::recvfrom(sockfd, reinterpret_cast<char *>(dest), size, 0,
        reinterpret_cast<sockaddr *>(src_addr), &addrlen);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::error_code(EAGAIN, std::system_category());

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif

std::string format_ipv4(u32 a)
{
    std::stringstream ss;

    ss <<  ((a >> 24) & 0xff) << '.'
        << ((a >> 16) & 0xff) << '.'
        << ((a >>  8) & 0xff) << '.'
        << ((a >>  0) & 0xff);

    return ss.str();
}

std::error_code MESYTEC_MVLC_EXPORT set_socket_receive_buffer_size(
    int sockfd, int desiredBufferSize, int *actualBufferSize)
{
    #ifndef __WIN32
            int res = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
                                &desiredBufferSize,
                                sizeof(desiredBufferSize));
    #else
            int res = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
                                reinterpret_cast<const char *>(&desiredBufferSize),
                                sizeof(desiredBufferSize));
    #endif

    if (res != 0)
        return std::error_code(errno, std::system_category());

    if (actualBufferSize)
    {
        *actualBufferSize = 0;
        socklen_t szLen = sizeof(actualBufferSize);

#ifndef __WIN32
        res = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
                            &actualBufferSize,
                            &szLen);
#else
        res = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
                            reinterpret_cast<char *>(&actualBufferSize),
                            &szLen);
#endif
        if (res != 0)
            return std::error_code(errno, std::system_category());

        // TODO: move this out of here
        #if 0
        logger->debug("pipe={}, SO_RCVBUF={}", static_cast<unsigned>(pipe), actualBufferSize);

        if (actualBufferSize < DesiredSocketReceiveBufferSize)
        {
            auto desiredMB = static_cast<double>(DesiredSocketReceiveBufferSize) / util::Megabytes(1);
            auto actualMB = static_cast<double>(actualBufferSize) / util::Megabytes(1);
            logger->info("pipe={}, requested SO_RCVBUF of {} bytes ({} MB), got {} bytes ({} MB)",
                            static_cast<unsigned>(pipe),
                            DesiredSocketReceiveBufferSize, desiredMB,
                            actualBufferSize, actualMB);
        }
        #endif
    }

    return {};
}

}
