#include "mvlc_impl_eth.h"
#include "mvlc_constants.h"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <system_error>

#ifndef __WIN32
    #include <netdb.h>
    #include <sys/prctl.h>
    #include <sys/stat.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>

    #ifdef __linux__
        #include <linux/netlink.h>
        #include <linux/rtnetlink.h>
        #include <linux/inet_diag.h>
        #include <linux/sock_diag.h>
    #endif

    #include <arpa/inet.h>
#else // __WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <fcntl.h>
#endif

#include "mvlc_buffer_validators.h"
#include "mvlc_dialog.h"
#include "mvlc_dialog_util.h"
#include "mvlc_error.h"
#include "mvlc_threading.h"
#include "mvlc_util.h"
#include "util/io_util.h"
#include "util/string_view.hpp"

#define LOG_LEVEL_OFF   0
#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MVLC_ETH_LOG_LEVEL
#define MVLC_ETH_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_LEVEL_SETTING MVLC_ETH_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - mvlc_eth ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - mvlc_eth ", fmt, ##__VA_ARGS__)

#define MVLC_ETH_THROTTLE_DEBUG 1

namespace
{
using namespace mesytec::mvlc;

// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mvlc;

    if (host.empty())
        return MVLCErrorCode::EmptyHostname;

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    // TODO: check getaddrinfo specific error codes. make and use getaddrinfo error category
    if (rc != 0)
    {
        //qDebug("%s: HostLookupError, host=%s, error=%s", __PRETTY_FUNCTION__, host.c_str(),
        //       gai_strerror(rc));
        return make_error_code(MVLCErrorCode::HostLookupError);
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
    {
        //qDebug("%s: HostLookupError, host=%s, no result found", __PRETTY_FUNCTION__, host.c_str());
        return make_error_code(MVLCErrorCode::HostLookupError);
    }

    return {};
}

struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
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

inline std::string format_ipv4(u32 a)
{
    std::stringstream ss;

    ss << ((a >> 24) & 0xff) << '.'
       << ((a >> 16) & 0xff) << '.'
       << ((a >>  8) & 0xff) << '.'
       << ((a >>  0) & 0xff);

    return ss.str();
}

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const size_t MaxOutgoingPayloadSize = 1500 - 20 - 8;

// Note: it is not necessary to split writes into multiple calls to send()
// because outgoing MVLC command buffers have to be smaller than the maximum,
// non-jumbo ethernet MTU.
// The send() call should return EMSGSIZE if the payload is too large to be
// atomically transmitted.
#ifdef __WIN32
inline std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketWriteTimeout);

        // Maybe TODO: use WSAGetLastError here with a WSA specific error
        // category like this: https://gist.github.com/bbolli/710010adb309d5063111889530237d6d
        return make_error_code(MVLCErrorCode::SocketError);
    }

    bytesTransferred = res;
    return {};
}
#else // !__WIN32
inline std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketWriteTimeout);

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif // !__WIN32


// Amount of receive buffer space requested from the OS for both the command
// and data sockets. It's not considered an error if less buffer space is
// granted.
static const int DesiredSocketReceiveBufferSize = 1024 * 1024 * 100;

/* Ethernet throttling implementation:
 * The MVLC now has a new 'delay pipe' on port 0x8002. It accepts delay
 * commands only and doesn't send any responses. Delay commands carry a 16-bit
 * delay value in microseconds. This delay is applied to the MVLCs data pipe
 * between each outgoing Ethernet frame thus limiting the total data rate. The
 * MVLC will block readout triggers if its internal buffers are full the same
 * way as is happening when using USB.
 *
 * The goal of the throttling code is to have packet-loss-free readouts by
 * sending appropriate delay values based on the operating systems socket
 * buffer fill level.
 *
 * The Linux version of the throttling code uses the NETLINK_SOCK_DIAG API to
 * obtain socket memory information and then applies exponential throttling
 * based on the receive buffer fill level.
 *
 * The Windows version uses WSAIoctl() with FIONREAD to obtain the current buffer
 * fill level.
 */

std::error_code send_delay_command(int delaySock, u16 delay_us)
{
    u32 cmd = static_cast<u32>(super_commands::SuperCommandType::EthDelay) << super_commands::SuperCmdShift;
    cmd |= delay_us;

    size_t bytesTransferred = 0;
    auto ec = write_to_socket(delaySock, reinterpret_cast<const u8 *>(&cmd), sizeof(cmd), bytesTransferred);

    if (ec)
        return ec;

    if (bytesTransferred != sizeof(cmd))
        return make_error_code(MVLCErrorCode::ShortWrite);

    return {};
};

// This code increases the delay value by powers of two. The max value is 64k
// so we need 16 steps to reach the maximum.
static const unsigned EthThrottleSteps = 16;

struct ReceiveBufferSnapshot
{
    u32 used = 0u;
    u32 capacity = 0u;
#ifndef __WIN32
    ino_t inode = 0u;
#endif
};

u16 throttle_exponential(eth::EthThrottleContext &ctx,  const ReceiveBufferSnapshot &bufferInfo) __attribute__((used));
u16 throttle_linear(eth::EthThrottleContext &ctx, const ReceiveBufferSnapshot &bufferInfo) __attribute__((used));

u16 throttle_exponential(eth::EthThrottleContext &ctx,  const ReceiveBufferSnapshot &bufferInfo)
{
    /* At 50% buffer level start throttling. Delay value
     * scales within the range of ctx.range of buffer usage
     * from 1 to 2^16.
     * So at buffer fill level of (ctx.threshold +
     * ctx.range) the maximum delay value should be set,
     * effectively blocking the MVLC from sending. Directly
     * at threshold level the minimum delay of 1 should be
     * set. In between scaling in powers of two is applied
     * to the delay value. This means the scaling range is
     * divided into 16 scaling steps so that at the maximum
     * value a delay of 2^16 is calculated.
     */

    double bufferUse = bufferInfo.used * 1.0 / bufferInfo.capacity;
    u16 delay = 0u;

    if (bufferUse >= ctx.threshold)
    {
        const double throttleIncrement = ctx.range / EthThrottleSteps;
        double aboveThreshold = bufferUse - ctx.threshold;
        u32 increments = std::floor(aboveThreshold / throttleIncrement);

        if (increments > EthThrottleSteps)
            increments = EthThrottleSteps;

        delay = std::min(1u << increments, static_cast<u32>(std::numeric_limits<u16>::max()));
    }

    return delay;
};

// Similar to throttle_exponential but apply linear throttling from 1 to 300 µs
// in the range [ctx.threshold, ctx.treshold + ctx.range].
u16 throttle_linear(eth::EthThrottleContext &ctx, const ReceiveBufferSnapshot &bufferInfo)
{
    double bufferUse = bufferInfo.used * 1.0 / bufferInfo.capacity;
    u16 delay = 0u;

    if (bufferUse >= ctx.threshold)
    {
        double aboveThreshold = bufferUse - ctx.threshold;
        delay = 747.5 * aboveThreshold + 1;
    }

    return delay;
}

#ifndef __WIN32
void mvlc_eth_throttler(
    Protected<eth::EthThrottleContext> &ctx,
    Protected<eth::EthThrottleCounters> &counters)
{
    auto send_query = [] (int netlinkSock)
    {
        struct sockaddr_nl nladdr = {
            .nl_family = AF_NETLINK
        };

        struct NetlinkDiagMessage
        {
            struct nlmsghdr nlh;
            struct inet_diag_req_v2 diagReq;
        };

        NetlinkDiagMessage req = {};

        req.nlh.nlmsg_len = sizeof(req);
        req.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
        req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_MATCH;

        req.diagReq.sdiag_family = AF_INET;
        req.diagReq.sdiag_protocol = IPPROTO_UDP;
        req.diagReq.idiag_ext = (1u << (INET_DIAG_SKMEMINFO - 1));
        req.diagReq.pad = 0;
        req.diagReq.idiag_states = 0xffffffffu; // All states (0 filters out all sockets).
        // Filter by dest port to reduce the number of results.
        req.diagReq.id.idiag_dport = htons(eth::DataPort);

        struct iovec iov = {
            .iov_base = &req,
            .iov_len = sizeof(req)
        };

        struct msghdr msg = {
            .msg_name = (void *) &nladdr,
            .msg_namelen = sizeof(nladdr),
            .msg_iov = &iov,
            .msg_iovlen = 1
        };

        for (;;) {
            if (sendmsg(netlinkSock, &msg, 0) < 0) {
                if (errno == EINTR)
                    continue;

                LOG_WARN("mvlc_eth_throttler: sendmsg failed: %s", strerror(errno));
                return -1;
            }

            return 0;
        }
    };

    auto get_buffer_snapshot = [] (const inet_diag_msg *diag, unsigned len)
        -> std::pair<ReceiveBufferSnapshot, bool>
    {
        std::pair<ReceiveBufferSnapshot, bool> ret = {};

        if (len < NLMSG_LENGTH(sizeof(*diag)))
        {
            LOG_WARN("mvlc_eth_throttler: NLMSG_LENGTH");
            return ret;
        }

        if (diag->idiag_family != AF_INET)
        {
            LOG_WARN("mvlc_eth_throttler: idiag_family != AF_INET");
            return ret;
        }

        unsigned int rta_len = len - NLMSG_LENGTH(sizeof(*diag));

        for (auto attr = (struct rtattr *) (diag + 1);
             RTA_OK(attr, rta_len);
             attr = RTA_NEXT(attr, rta_len))
        {
            switch (attr->rta_type)
            {
                case INET_DIAG_SKMEMINFO:
                    if (RTA_PAYLOAD(attr) >= sizeof(u32) * SK_MEMINFO_VARS)
                    {
                        auto memInfo = reinterpret_cast<const u32 *>(RTA_DATA(attr));

                        ret.first.used = memInfo[SK_MEMINFO_RMEM_ALLOC];
                        ret.first.capacity = memInfo[SK_MEMINFO_RCVBUF];
                        ret.first.inode = diag->idiag_inode;
                        ret.second = true;
                        return ret;
                    }
                    break;
            }
        }

        LOG_WARN("mvlc_eth_throttler: defaulted return in get_buffer_snapshot()");
        return ret;
    };

    auto receive_response = [&get_buffer_snapshot] (int netlinkSock, u32 dataSocketInode)
        -> std::pair<ReceiveBufferSnapshot, bool>
    {
        long buf[8192 / sizeof(long)];
        struct sockaddr_nl nladdr = {
            .nl_family = AF_NETLINK
        };
        struct iovec iov = {
            .iov_base = buf,
            .iov_len = sizeof(buf)
        };
        int flags = 0;

        std::pair<ReceiveBufferSnapshot, bool> result{};

        for (;;) {
            struct msghdr msg = {
                .msg_name = (void *) &nladdr,
                .msg_namelen = sizeof(nladdr),
                .msg_iov = &iov,
                .msg_iovlen = 1
            };

            ssize_t ret = recvmsg(netlinkSock, &msg, flags);

            if (ret < 0) {
                if (errno == EINTR)
                    continue;

                LOG_WARN("mvlc_eth_throttler: recvmsg failed: %s",
                         strerror(errno));

                return {};
            }

            if (ret == 0)
            {
                LOG_WARN("mvlc_eth_throttler: empty netlink response");
                return {};
            }

            const struct nlmsghdr *h = (struct nlmsghdr *) buf;

            if (!NLMSG_OK(h, ret))
            {
                LOG_WARN("mvlc_eth_throttler: netlink header not ok");
                return {};
            }

            for (; NLMSG_OK(h, ret); h = NLMSG_NEXT(h, ret)) {
                if (h->nlmsg_type == NLMSG_DONE)
                {
                    LOG_TRACE("mvlc_eth_throttler: NLMSG_DONE");
                    return result;
                }

                if (h->nlmsg_type == NLMSG_ERROR)
                {
                    auto err = reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(h));
                    LOG_WARN("mvlc_eth_throttler: NLMSG_ERROR error=%d (%s)",
                             err->error, strerror(-err->error));
                    return {};
                }

                if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY)
                {
                    LOG_WARN("mvlc_eth_throttler: not SOCK_DIAG_BY_FAMILY");
                    return {};
                }

                auto diag = reinterpret_cast<const inet_diag_msg *>(NLMSG_DATA(h));

                // Test the inode so that we do not monitor foreign sockets.
                if (diag->idiag_inode == dataSocketInode)
                    result = get_buffer_snapshot(diag, h->nlmsg_len);
                else
                {
                    LOG_WARN("mvlc_eth_throttler: wrong inode");
                }
            }
        }

        return {};
    };

    prctl(PR_SET_NAME,"eth_throttler",0,0,0);

    int diagSocket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);

    if (diagSocket < 0)
    {
        LOG_WARN("mvlc_eth_throttler: could not create netlink diag socket: %s",
                 strerror(errno));
        return;
    }

    u32 dataSocketInode = ctx.access()->dataSocketInode;

    LOG_DEBUG("mvlc_eth_throttler entering loop");

    while (!ctx.access()->quit)
    {
        if (send_query(diagSocket) == 0)
        {
            auto res = receive_response(diagSocket, dataSocketInode);

            if (res.second)
            {
                u16 delay = throttle_linear(ctx.access().ref(), res.first);
                send_delay_command(ctx.access()->delaySocket, delay);

                auto ca = counters.access();
                ca->currentDelay = delay;
                ca->maxDelay = std::max(ca->maxDelay, delay);

                if (ctx.access()->debugOut.good())
                {
                    ctx.access()->debugOut
                        << " inode=" << res.first.inode
                        << " rmem_alloc=" << res.first.used
                        << " rcvbuf=" << res.first.capacity
                        << " delay=" << delay
                        << std::endl;
                }
            }
        }

        std::this_thread::sleep_for(ctx.access()->queryDelay);
    }

    close_socket(diagSocket);

    LOG_DEBUG("mvlc_eth_throttler leaving loop");
}
#else // __WIN32
void mvlc_eth_throttler(
    Protected<eth::EthThrottleContext> &ctx,
    Protected<eth::EthThrottleCounters> &counters)
{
    int dataSocket = ctx.access()->dataSocket;
    ReceiveBufferSnapshot rbs = { 0u, static_cast<u32>(ctx.access()->dataSocketReceiveBufferSize) };

    LOG_DEBUG("mvlc_eth_throttler entering loop");

    while (!ctx.access()->quit)
    {
        auto tStart = std::chrono::steady_clock::now();

        DWORD bytesReturned = 0;
        int res = WSAIoctl(
            dataSocket,         // socket
            FIONREAD,           // opcode
            nullptr,            // ptr to input buffer
            0,                  // input buffer size
            &rbs.used,          // ptr to output buffer
            sizeof(rbs.used),   // output buffer size
            &bytesReturned,     // actual number of bytes output
            nullptr,            // overlapped
            nullptr);           // completion

        if (res == 0)
        {
            u16 delay = throttle_exponential(ctx.access().ref(), rbs);
            send_delay_command(ctx.access()->delaySocket, delay);

            auto ca = counters.access();
            ca->currentDelay = delay;
            ca->maxDelay = std::max(ca->maxDelay, delay);

            if (ctx.access()->debugOut.good())
            {
                ctx.access()->debugOut
                    << " rmem_alloc=" << rbs.used
                    << " rcvbuf=" << rbs.capacity
                    << " delay=" << delay
                    << std::endl;
            }
        }
        else
        {
            LOG_WARN("WSAIoctl failed: %d", WSAGetLastError());
        }

        auto tDelayDone = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(ctx.access()->queryDelay);
        auto tSleepDone = std::chrono::steady_clock::now();

        auto dtDelay = std::chrono::duration_cast<std::chrono::microseconds>(tDelayDone - tStart);
        auto dtSleep = std::chrono::duration_cast<std::chrono::microseconds>(tSleepDone - tDelayDone);
        auto dtTotal = std::chrono::duration_cast<std::chrono::microseconds>(tSleepDone - tStart);

        LOG_DEBUG("mvlc_eth_throttler: dtDelay=%d us, dtSleep=%d us, dtTotal=%d us",
                  dtDelay.count(), dtSleep.count(), dtTotal.count());
    }

    LOG_DEBUG("mvlc_eth_throttler leaving loop");
}
#endif

} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace eth
{

Impl::Impl(const std::string &host)
    : m_host(host)
    , m_throttleCounters({})
    , m_throttleContext({})
{
#ifdef __WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 1);
    int res = WSAStartup( wVersionRequested, &wsaData );
    if (res != 0)
        throw std::runtime_error("Error initializing Windows Socket API (WSAStartup failed)");
#endif
}

Impl::~Impl()
{
    disconnect();

#ifdef __WIN32
    WSACleanup();
#endif
}

// A note about using ::bind() and then ::connect():
//
// Under linux this has the effect of changing the local bound address from
// INADDR_ANY to the address of the interface that's used to reach the remote
// address. E.g. when connecting to localhost the following will happen: after
// the bind() call the local "listen" address will be 0.0.0.0, after the
// connect() call this will change to 127.0.0.1. The local port specified in
// the bind() call will be kept. This is nice.

// Things happening in Impl::connect:
// * Remote host lookup to get the IPv4 address of the MVLC.
// * Create three UDP sockets for the command, data and delay pipes.
// * Use ::connect() on the sockets with the MVLC address and the default
//   command, data and delay ports. This way the sockets will only receive datagrams
//   originating from the MVLC.
// * Send an initial request and read the response. Preferably this
//   should tell us if another client is currently using the MVLC. It could be
//   some sort of "DAQ mode register" or a way to check where the MVLC is
//   currently sending its data output.
std::error_code Impl::connect()
{
    auto close_sockets = [this] ()
    {
        if (m_cmdSock >= 0)
            close_socket(m_cmdSock);
        if (m_dataSock >= 0)
            close_socket(m_dataSock);
        if (m_delaySock >= 0)
            close_socket(m_delaySock);

        m_cmdSock = -1;
        m_dataSock = -1;
        m_delaySock = -1;
    };

    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    m_cmdSock = -1;
    m_dataSock = -1;
    m_delaySock = -1;

    resetPipeAndChannelStats();

    LOG_TRACE("looking up host %s...", m_host.c_str());

    if (auto ec = lookup(m_host, CommandPort, m_cmdAddr))
    {
        LOG_TRACE("host lookup failed for host %s: %s",
                  m_host.c_str(), ec.message().c_str());
        return ec;
    }

    assert(m_cmdAddr.sin_port == htons(CommandPort));

    // Copy address and replace the port with DataPort
    m_dataAddr = m_cmdAddr;
    m_dataAddr.sin_port = htons(DataPort);

    // Same for the delay port.
    m_delayAddr = m_cmdAddr;
    m_delayAddr.sin_port = htons(DelayPort);

    // Lookup succeeded and we now have three remote addresses, one for the
    // command, one for the data pipe and one for the delay port.
    //
    // Now create the IPv4 UDP sockets and bind them.

    LOG_TRACE("creating sockets...");

    m_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_cmdSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("socket() failed for command pipe: %s", ec.message().c_str());
        return ec;
    }

    m_dataSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_dataSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("socket() failed for data pipe: %s", ec.message().c_str());
        close_sockets();
        return ec;
    }

    m_delaySock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_delaySock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("socket() failed for delay port: %s", ec.message().c_str());
        close_sockets();
        return ec;
    }

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    LOG_TRACE("binding sockets...");

    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        for (auto sock: { m_cmdSock, m_dataSock, m_delaySock })
        {
            if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr),
                       sizeof(localAddr)))
            {
                close_sockets();
                return std::error_code(errno, std::system_category());
            }
        }
    }

    LOG_TRACE("connecting sockets...");

    // Call connect on the sockets so that we receive only datagrams
    // originating from the MVLC.
    if (::connect(m_cmdSock, reinterpret_cast<struct sockaddr *>(&m_cmdAddr),
                            sizeof(m_cmdAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("connect() failed for command socket: %s", ec.message().c_str());
        close_sockets();
        return ec;
    }

    if (::connect(m_dataSock, reinterpret_cast<struct sockaddr *>(&m_dataAddr),
                            sizeof(m_dataAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("connect() failed for data socket: %s", ec.message().c_str());
        close_sockets();
        return ec;
    }

    if (::connect(m_delaySock, reinterpret_cast<struct sockaddr *>(&m_delayAddr),
                            sizeof(m_delayAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        LOG_TRACE("connect() failed for delay socket: %s", ec.message().c_str());
        close_sockets();
        return ec;
    }

    // Set read and write timeouts for the command and data ports.
    LOG_TRACE("setting socket timeouts...");

    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        if (auto ec = set_socket_write_timeout(getSocket(pipe), writeTimeout(pipe)))
        {
            LOG_TRACE("set_socket_write_timeout failed: %s, socket=%d",
                      ec.message().c_str(),
                      getSocket(pipe));
            return ec;
        }

        if (auto ec = set_socket_read_timeout(getSocket(pipe), readTimeout(pipe)))
        {
            LOG_TRACE("set_socket_read_timeout failed: %s", ec.message().c_str());
            return ec;
        }
    }

    // Set the write timeout for the delay socket
    if (auto ec = set_socket_write_timeout(m_delaySock, DefaultWriteTimeout_ms))
    {
        LOG_TRACE("set_socket_write_timeout failed: %s", ec.message().c_str());
        return ec;
    }

    // Set socket receive buffer size
    LOG_TRACE("setting socket receive buffer sizes...");

    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
#ifndef __WIN32
        int res = setsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             &DesiredSocketReceiveBufferSize,
                             sizeof(DesiredSocketReceiveBufferSize));
#else
        int res = setsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             reinterpret_cast<const char *>(&DesiredSocketReceiveBufferSize),
                             sizeof(DesiredSocketReceiveBufferSize));
#endif
        //assert(res == 0);

        if (res != 0)
        {
            auto ec = std::error_code(errno, std::system_category());
            LOG_WARN("setting socket buffer size failed: %s", ec.message().c_str());
            //return ec;
        }

        {
            int actualBufferSize = 0;
            socklen_t szLen = sizeof(actualBufferSize);

#ifndef __WIN32
            res = getsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             &actualBufferSize,
                             &szLen);
#else
            res = getsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             reinterpret_cast<char *>(&actualBufferSize),
                             &szLen);
#endif

            assert(res == 0);

            if (res != 0)
                return std::error_code(errno, std::system_category());

            LOG_INFO("pipe=%u, SO_RCVBUF=%d", static_cast<unsigned>(pipe), actualBufferSize);

            if (actualBufferSize < DesiredSocketReceiveBufferSize)
            {
                LOG_INFO("pipe=%u, requested SO_RCVBUF of %d bytes, got %d bytes",
                         static_cast<unsigned>(pipe), DesiredSocketReceiveBufferSize, actualBufferSize);
            }

            if (pipe == Pipe::Data)
                m_dataSocketReceiveBufferSize = actualBufferSize;
        }
    }

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    // Attempt to read the trigger registers. If one has a non-zero value
    // assume the MVLC is in use by another client. If the
    // disableTriggersOnConnect flag is set try to disable the triggers,
    // otherwise return MVLCErrorCode::InUse.
    {
        LOG_TRACE("reading MVLC trigger registers...");

        MVLCDialog dlg(this);
        bool inUse = false;

        for (u8 stackId = 0; stackId < stacks::StackCount; stackId++)
        {
            u16 addr = stacks::get_trigger_register(stackId);
            u32 regVal = 0u;

            if (auto ec = dlg.readRegister(addr, regVal))
            {
                close_sockets();
                return ec;
            }

            if (regVal != stacks::NoTrigger)
            {
                inUse = true;
                break;
            }
        }

        if (inUse && !disableTriggersOnConnect())
        {
            LOG_WARN("MVLC is in use");
            close_sockets();
            return make_error_code(MVLCErrorCode::InUse);
        }
        else if (inUse)
        {
            assert(disableTriggersOnConnect());

            if (auto ec = disable_all_triggers(dlg))
            {
                LOG_WARN("MVLC is in use and mvme failed to disable triggers: %s", ec.message().c_str());
                close_sockets();
                return ec;
            }
        }
    }

    // Send an initial empty frame to the UDP data pipe port so that
    // the MVLC knows where to send the readout data.
    {
        LOG_TRACE("Sending initial empty request to the UDP data port");

        static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
        size_t bytesTransferred = 0;

        if (auto ec = this->write(
                Pipe::Data,
                reinterpret_cast<const u8 *>(EmptyRequest.data()),
                EmptyRequest.size() * sizeof(u32),
                bytesTransferred))
        {
            LOG_WARN("Error sending initial empty request to the UDP data port: %s", ec.message().c_str());
            close_sockets();
            return ec;
        }
    }

    LOG_TRACE("ETH connect sequence finished");

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    {
        auto tc = m_throttleContext.access();
#ifndef __WIN32

        struct stat sb = {};

        if (fstat(m_dataSock, &sb) == 0)
            tc->dataSocketInode = sb.st_ino;

        tc->delaySocket = m_delaySock;
#else // __WIN32
        tc->dataSocket = m_dataSock;
        tc->dataSocketReceiveBufferSize = m_dataSocketReceiveBufferSize;
#endif
        tc->quit = false;
#if MVLC_ETH_THROTTLE_DEBUG
        tc->debugOut = std::ofstream("mvlc-eth-throttle-debug.txt");
#endif
    }

    m_throttleThread = std::thread(
        mvlc_eth_throttler,
        std::ref(m_throttleContext),
        std::ref(m_throttleCounters));

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    m_throttleContext.access()->quit = true;

    if (m_throttleThread.joinable())
        m_throttleThread.join();

    close_socket(m_cmdSock);
    close_socket(m_dataSock);
    close_socket(m_delaySock);
    m_cmdSock = -1;
    m_dataSock = -1;
    m_delaySock = -1;
    return {};
}

bool Impl::isConnected() const
{
    return m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0;
}

std::error_code Impl::setWriteTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_writeTimeouts[p] = ms;

    return {};
}

std::error_code Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
    auto p = static_cast<unsigned>(pipe);

    if (p >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    m_readTimeouts[p] = ms;

    return {};
}

unsigned Impl::writeTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_writeTimeouts[static_cast<unsigned>(pipe)];
}

unsigned Impl::readTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_readTimeouts[static_cast<unsigned>(pipe)];
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    bytesTransferred = 0;

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    return write_to_socket(getSocket(pipe), buffer, size, bytesTransferred);
}

#ifdef __WIN32
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
        return make_error_code(MVLCErrorCode::SocketReadTimeout);

    if (sres == SOCKET_ERROR)
        return make_error_code(MVLCErrorCode::SocketError);

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    LOG_TRACE("::recv res=%lld", res);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketReadTimeout);

        return make_error_code(MVLCErrorCode::SocketError);
    }

#if LOG_LEVEL_SETTING >= LOG_LEVEL_TRACE
    if (res >= static_cast<ssize_t>(sizeof(u32)))
    {
        util::log_buffer(
            std::cerr,
            basic_string_view<const u32>(reinterpret_cast<const u32 *>(dest), res / sizeof(u32)),
            "32-bit words in buffer from ::recv()");
    }
#endif

    bytesTransferred = res;

    return {};
}
#else
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int)
{
    bytesTransferred = 0u;

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketReadTimeout);

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif

PacketReadResult Impl::read_packet(Pipe pipe_, u8 *buffer, size_t size)
{
    PacketReadResult res = {};

    unsigned pipe = static_cast<unsigned>(pipe_);
    auto &pipeStats = m_pipeStats[pipe];

    {
        UniqueLock guard(m_statsMutex);
        ++pipeStats.receiveAttempts;
    }

    if (pipe >= PipeCount)
    {
        res.ec = make_error_code(MVLCErrorCode::InvalidPipe);
        return res;
    }

    if (!isConnected())
    {
        res.ec = make_error_code(MVLCErrorCode::IsDisconnected);
        return res;
    }

    res.ec = receive_one_packet(getSocket(pipe_), buffer, size,
                                res.bytesTransferred,
                                readTimeout(pipe_));
    res.buffer = buffer;

    if (res.ec && res.bytesTransferred == 0)
        return res;

    {
        UniqueLock guard(m_statsMutex);
        ++pipeStats.receivedPackets;
        pipeStats.receivedBytes += res.bytesTransferred;
        ++pipeStats.packetSizes[res.bytesTransferred];
    }

    LOG_TRACE("  pipe=%u, res.bytesTransferred=%u", pipe, res.bytesTransferred);

    if (!res.hasHeaders())
    {
        UniqueLock guard(m_statsMutex);
        ++pipeStats.shortPackets;
        LOG_WARN("  pipe=%u, received data is smaller than the MVLC UDP header size", pipe);
        res.ec = make_error_code(MVLCErrorCode::ShortRead);
        return res;
    }

    LOG_TRACE("  pipe=%u, header0=0x%08x -> packetChannel=%u, packetNumber=%u, wordCount=%u",
              pipe, res.header0(), res.packetChannel(), res.packetNumber(), res.dataWordCount());

    LOG_TRACE("  pipe=%u, header1=0x%08x -> udpTimestamp=%u, nextHeaderPointer=%u",
              pipe, res.header1(), res.udpTimestamp(), res.nextHeaderPointer());

    LOG_TRACE("  pipe=%u, calculated available data words = %u, leftover bytes = %u",
              pipe, res.availablePayloadWords(), res.leftoverBytes());

    if (res.dataWordCount() > res.availablePayloadWords())
    {
        res.ec = make_error_code(MVLCErrorCode::UDPDataWordCountExceedsPacketSize);
        return res;
    }

    // This is a workaround for an issue in Windows 10 Build 2004 where
    // ethernet padding bytes coming after the UDP data are for some reason
    // included in the return value from recv(). The workaround truncates the
    // received data to the number of data words transmitted by the MVLC in
    // packet header0.
    if (res.availablePayloadWords() > res.dataWordCount())
    {
        //LOG_WARN("Win10 Build 2004 UDP length hack code path reached!");
        res.bytesTransferred = res.dataWordCount() * sizeof(u32) + eth::HeaderBytes;
    }

    if (res.leftoverBytes() > 0)
    {
        LOG_WARN("  pipe=%u, %u leftover bytes in received packet",
                 pipe, res.leftoverBytes());
        UniqueLock guard(m_statsMutex);
        ++pipeStats.packetsWithResidue;
    }

    if (res.packetChannel() >= NumPacketChannels)
    {
        LOG_WARN("  pipe=%u, packet channel number out of range: %u", pipe, res.packetChannel());
        UniqueLock guard(m_statsMutex);
        ++pipeStats.packetChannelOutOfRange;
        res.ec = make_error_code(MVLCErrorCode::UDPPacketChannelOutOfRange);
        return res;
    }

    auto &channelStats = m_packetChannelStats[res.packetChannel()];
    {
        UniqueLock guard(m_statsMutex);
        ++channelStats.receivedPackets;
        channelStats.receivedBytes += res.bytesTransferred;
    }

    {
        auto &lastPacketNumber = m_lastPacketNumbers[res.packetChannel()];

        LOG_TRACE("  pipe=%u, packetChannel=%u, packetNumber=%u, lastPacketNumber=%d",
                  pipe, res.packetChannel(), res.packetNumber(), lastPacketNumber);

        // Packet loss calculation. The initial lastPacketNumber value is -1.
        if (lastPacketNumber >= 0)
        {
            auto loss = calc_packet_loss(lastPacketNumber, res.packetNumber());

            if (loss > 0)
            {
                LOG_DEBUG("  pipe=%u, packetChannel=%u, lastPacketNumber=%u,"
                          " packetNumber=%u, loss=%d",
                          pipe, res.packetChannel(), lastPacketNumber, res.packetNumber(), loss);
            }

            res.lostPackets = loss;
            UniqueLock guard(m_statsMutex);
            pipeStats.lostPackets += loss;
            channelStats.lostPackets += loss;
        }

        lastPacketNumber = res.packetNumber();

        {
            UniqueLock guard(m_statsMutex);
            ++channelStats.packetSizes[res.bytesTransferred];
        }
    }

    // Check where nextHeaderPointer is pointing to
    if (res.nextHeaderPointer() != header1::NoHeaderPointerPresent)
    {
        u32 *start = res.payloadBegin();
        u32 *end   = res.payloadEnd();
        u32 *headerp = start + res.nextHeaderPointer();

        if (headerp >= end)
        {
            UniqueLock guard(m_statsMutex);
            ++pipeStats.headerOutOfRange;
            ++channelStats.headerOutOfRange;

            LOG_INFO("  pipe=%u, nextHeaderPointer out of range: nHPtr=%u, "
                     "availDataWords=%u, pktChan=%u, pktNum=%d, pktSize=%u bytes",
                     pipe, res.nextHeaderPointer(), res.availablePayloadWords(),
                     res.packetChannel(), res.packetNumber(), res.bytesTransferred);
        }
        else
        {
            u32 header = *headerp;
            LOG_TRACE("  pipe=%u, nextHeaderPointer=%u -> header=0x%08x",
                      pipe, res.nextHeaderPointer(), header);
            u32 type = get_frame_type(header);
            UniqueLock guard(m_statsMutex);
            ++pipeStats.headerTypes[type];
            ++channelStats.headerTypes[type];
        }
    }
    else
    {
        LOG_TRACE("  pipe=%u, NoHeaderPointerPresent, eth header1=0x%08x",
                  pipe, res.header1());
        UniqueLock guard(m_statsMutex);
        ++pipeStats.noHeader;
        ++channelStats.noHeader;
    }

    return res;
}

/* initial:
 *   next_header_pointer = 0
 *   packet_number = 0
 *
 *   - receive one packet
 *   - make sure there are two header words
 *   - extract packet_number and number_of_data_words
 *   - record possible packet loss or ordering problems based on packet number
 *   - check to make sure timestamp is incrementing (packet ordering) (not
 *     implemented yet in the MVLC firmware)
 */

std::error_code Impl::read(Pipe pipe_, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    unsigned pipe = static_cast<unsigned>(pipe_);

    assert(buffer);
    assert(pipe < PipeCount);

    const size_t requestedSize = size;
    bytesTransferred = 0u;

    if (pipe >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto &receiveBuffer = m_receiveBuffers[pipe];

    // Copy from receiveBuffer into the dest buffer while updating local
    // variables.
    auto copy_and_update = [&buffer, &size, &bytesTransferred, &receiveBuffer] ()
    {
        if (size_t toCopy = std::min(receiveBuffer.available(), size))
        {
            memcpy(buffer, receiveBuffer.start, toCopy);
            buffer += toCopy;
            size -= toCopy;
            receiveBuffer.start += toCopy;
            bytesTransferred += toCopy;
        }
    };

    LOG_TRACE("+ pipe=%u, size=%zu, bufferAvail=%zu", pipe, requestedSize, receiveBuffer.available());

    copy_and_update();

    if (size == 0)
    {
        LOG_TRACE("  pipe=%u, size=%zu, read request satisfied from buffer, new buffer size=%zu",
                  pipe, requestedSize, receiveBuffer.available());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue actual read requests.
    assert(receiveBuffer.available() == 0);

    size_t readCount = 0u;
    const auto tStart = std::chrono::high_resolution_clock::now();

    while (size > 0)
    {
        assert(receiveBuffer.available() == 0);
        receiveBuffer.reset();

        LOG_TRACE("  pipe=%u, requestedSize=%zu, remainingSize=%zu, reading from MVLC...",
                  pipe, requestedSize, size);

        auto rr = read_packet(pipe_, receiveBuffer.buffer.data(), receiveBuffer.buffer.size());

        ++readCount;

        LOG_TRACE("  pipe=%u, received %u bytes, ec=%s",
                  pipe, rr.bytesTransferred, rr.ec.message().c_str());

        if (rr.ec && rr.bytesTransferred == 0)
            return rr.ec;

        receiveBuffer.start = reinterpret_cast<u8 *>(rr.payloadBegin());
        receiveBuffer.end   = reinterpret_cast<u8 *>(rr.payloadEnd());

        // Copy to destination buffer
        copy_and_update();

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

        //qDebug() << elapsed.count() << readTimeout(pipe_);

        if (elapsed.count() >= readTimeout(pipe_))
        {
            LOG_TRACE("  pipe=%u, read of size=%zu completes with %zu bytes and timeout"
                      " after %zu reads, remaining bytes in buffer=%zu",
                      pipe, requestedSize, bytesTransferred, readCount,
                      receiveBuffer.available());

            return make_error_code(MVLCErrorCode::SocketReadTimeout);
        }
    }

    LOG_TRACE("  pipe=%u, read of size=%zu completed using %zu reads, remaining bytes in buffer=%zu",
              pipe, requestedSize, readCount, receiveBuffer.available());

    return {};
}

std::error_code Impl::enableJumboFrames(bool b)
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    return MVLCDialog(this).writeRegister(
        registers::jumbo_frame_enable, static_cast<u32>(b));
}

std::pair<bool, std::error_code> Impl::jumboFramesEnabled()
{
    if (!isConnected())
        return std::make_pair(false, make_error_code(MVLCErrorCode::IsDisconnected));

    u32 value = 0u;
    auto ec = MVLCDialog(this).readRegister(registers::jumbo_frame_enable, value);

    return std::make_pair(static_cast<bool>(value), ec);
}

std::error_code Impl::sendDelayCommand(u16 delay_us)
{
    u32 cmd = static_cast<u32>(super_commands::SuperCommandType::EthDelay) << super_commands::SuperCmdShift;
    cmd |= delay_us;

    size_t bytesTransferred = 0;
    auto ec = write_to_socket(m_delaySock, reinterpret_cast<const u8 *>(&cmd), sizeof(cmd), bytesTransferred);

    if (ec)
        return ec;

    if (bytesTransferred != sizeof(cmd))
        return make_error_code(MVLCErrorCode::ShortWrite);

    return {};
}


EthThrottleCounters Impl::getThrottleCounters() const
{
    return m_throttleCounters.copy();
}

#if 0
std::error_code Impl::getReadQueueSize(Pipe pipe_, u32 &dest)
{
    auto pipe = static_cast<unsigned>(pipe_);
    assert(pipe < PipeCount);

    if (pipe < PipeCount)
        dest = m_receiveBuffers[static_cast<unsigned>(pipe)].available();

    return make_error_code(MVLCErrorCode::InvalidPipe);
}
#endif

std::array<PipeStats, PipeCount> Impl::getPipeStats() const
{
    UniqueLock guard(m_statsMutex);
    return m_pipeStats;
}

std::array<PacketChannelStats, NumPacketChannels> Impl::getPacketChannelStats() const
{
    UniqueLock guard(m_statsMutex);
    return m_packetChannelStats;
}

void Impl::resetPipeAndChannelStats()
{
    UniqueLock guard(m_statsMutex);
    m_pipeStats = {};
    m_packetChannelStats = {};
    std::fill(m_lastPacketNumbers.begin(), m_lastPacketNumbers.end(), -1);
}

u32 Impl::getCmdAddress() const
{
    return ntohl(m_cmdAddr.sin_addr.s_addr);
}

u32 Impl::getDataAddress() const
{
    return ntohl(m_dataAddr.sin_addr.s_addr);
}

std::string Impl::connectionInfo() const
{
    std::string remoteIP = format_ipv4(getCmdAddress());

    if (getHost() != remoteIP)
    {
        std::string result = "host=" + getHost();
        result += ", address=" + remoteIP;
        return result;
    }

    return "address=" + remoteIP;
}

s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber)
{
    static const s32 PacketNumberMax = eth::header0::PacketNumberMask;

    s32 diff = packetNumber - lastPacketNumber;

    if (diff < 1)
    {
        diff = PacketNumberMax + diff;
        return diff;
    }

    return diff - 1;
}

} // end namespace eth
} // end namespace mvlc
} // end namespace mesytec
