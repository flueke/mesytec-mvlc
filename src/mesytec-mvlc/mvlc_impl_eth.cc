#include "mvlc_impl_eth.h"
#include "mvlc_constants.h"
#include "util/udp_sockets.h"

#include "mvlc_buffer_validators.h"
#include "mvlc_dialog.h"
#include "mvlc_dialog_util.h"
#include "mvlc_error.h"
#include "mvlc_threading.h"
#include "mvlc_util.h"
#include "util/io_util.h"
#include "util/logging.h"
#include "util/storage_sizes.h"
#include "util/string_view.hpp"

#if defined __linux__ or defined __WIN32
#define MVLC_ENABLE_ETH_THROTTLE 1
#define MVLC_ETH_THROTTLE_WRITE_DEBUG_FILE 0
#endif

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
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <fcntl.h>
    #include <mmsystem.h>
#endif

using namespace mesytec::mvlc;

namespace
{

static const unsigned DefaultWriteTimeout_ms = 500;
static const unsigned DefaultReadTimeout_ms  = 500;

// Amount of receive buffer space requested from the OS for both the command
// and data sockets. It's not considered an error if less buffer space is
// granted.
static const int DesiredSocketReceiveBufferSize = 1024 * 1024 * 10;

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
    auto ec = eth::write_to_socket(delaySock, reinterpret_cast<const u8 *>(&cmd), sizeof(cmd), bytesTransferred);

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

// Similar to throttle_exponential but apply linear throttling from 1 to 300
// µs (at a=747.5).
// in the range [ctx.threshold, ctx.treshold + ctx.range].
u16 throttle_linear(eth::EthThrottleContext &ctx, const ReceiveBufferSnapshot &bufferInfo)
{
    double bufferUse = bufferInfo.used * 1.0 / bufferInfo.capacity;
    u16 delay = 0u;

    if (bufferUse >= ctx.threshold)
    {
        double aboveThreshold = bufferUse - ctx.threshold;
        const double a = 747.5;
        delay = a * aboveThreshold + 1;
    }

    return delay;
}

inline float calc_avg_delay(u16 curDelay, float lastAvg)
{
    static const float Smoothing = 0.75;

    return Smoothing * curDelay + (1.0 - Smoothing) * lastAvg;
}

using ThrottleFunc = u16 (*)(eth::EthThrottleContext &ctx,  const ReceiveBufferSnapshot &bufferInfo);

static ThrottleFunc theThrottleFunc = throttle_exponential;

#ifdef __linux__
void mvlc_eth_throttler(
    Protected<eth::EthThrottleContext> &ctx,
    Protected<eth::EthThrottleCounters> &counters)
{
    auto logger = get_logger("mvlc_eth_throttler");

    auto send_query = [&logger] (int netlinkSock)
    {
        struct sockaddr_nl nladdr = {};
        nladdr.nl_family = AF_NETLINK;

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

        struct msghdr msg = {};
        msg.msg_name = (void *) &nladdr;
        msg.msg_namelen = sizeof(nladdr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        for (;;) {
            if (sendmsg(netlinkSock, &msg, 0) < 0) {
                if (errno == EINTR)
                    continue;

                logger->warn("send_query: netlink sendmsg failed: {}", strerror(errno));
                return -1;
            }

            return 0;
        }
    };

    auto get_buffer_snapshot = [&logger] (const inet_diag_msg *diag, unsigned len)
        -> std::pair<ReceiveBufferSnapshot, bool>
    {
        std::pair<ReceiveBufferSnapshot, bool> ret = {};

        if (len < NLMSG_LENGTH(sizeof(*diag)))
        {
            logger->warn("netlink: len < NLMSG_LENGTH(diag)");
            return ret;
        }

        if (diag->idiag_family != AF_INET)
        {
            logger->warn("netlink: idiag_family != AF_INET");
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

        logger->warn("defaulted return in get_buffer_snapshot()");
        return ret;
    };

    auto receive_response = [&get_buffer_snapshot, &logger] (int netlinkSock, u32 dataSocketInode)
        -> std::pair<ReceiveBufferSnapshot, bool>
    {
        long buf[8192 / sizeof(long)];
        struct sockaddr_nl nladdr = {};
        nladdr.nl_family = AF_NETLINK;

        struct iovec iov = {
            .iov_base = buf,
            .iov_len = sizeof(buf)
        };
        int flags = 0;

        std::pair<ReceiveBufferSnapshot, bool> result{};

        for (;;) {
            struct msghdr msg = {};
            msg.msg_name = (void *) &nladdr;
            msg.msg_namelen = sizeof(nladdr);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;

            ssize_t ret = recvmsg(netlinkSock, &msg, flags);

            if (ret < 0) {
                if (errno == EINTR)
                    continue;

                logger->warn("mvlc_eth_throttler: recvmsg failed: {}",
                         strerror(errno));

                return {};
            }

            if (ret == 0)
            {
                logger->warn("mvlc_eth_throttler: empty netlink response");
                return {};
            }

            const struct nlmsghdr *h = (struct nlmsghdr *) buf;

            if (!NLMSG_OK(h, ret))
            {
                logger->warn("mvlc_eth_throttler: netlink header not ok");
                return {};
            }

            for (; NLMSG_OK(h, ret); h = NLMSG_NEXT(h, ret)) {
                if (h->nlmsg_type == NLMSG_DONE)
                {
                    //logger->trace("mvlc_eth_throttler: NLMSG_DONE");
                    return result;
                }

                if (h->nlmsg_type == NLMSG_ERROR)
                {
                    auto err = reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(h));
                    logger->warn("mvlc_eth_throttler: NLMSG_ERROR error={} ({})",
                             err->error, strerror(-err->error));
                    return {};
                }

                if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY)
                {
                    logger->warn("mvlc_eth_throttler: not SOCK_DIAG_BY_FAMILY");
                    return {};
                }

                auto diag = reinterpret_cast<const inet_diag_msg *>(NLMSG_DATA(h));

                // Test the inode so that we do not monitor foreign sockets.
                if (diag->idiag_inode == dataSocketInode)
                    result = get_buffer_snapshot(diag, h->nlmsg_len);
            }
        }

        return {};
    };

#ifdef __linux__
    prctl(PR_SET_NAME,"eth_throttler",0,0,0);
#endif

    int diagSocket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);

    if (diagSocket < 0)
    {
        logger->warn("could not create netlink diag socket: {}", strerror(errno));
        return;
    }

    u32 dataSocketInode = ctx.access()->dataSocketInode;
    s32 lastSentDelay = -1;

    logger->debug("mvlc_eth_throttler entering loop");

    while (!ctx.access()->quit)
    {
        if (send_query(diagSocket) == 0)
        {
            auto res = receive_response(diagSocket, dataSocketInode);

            if (res.second)
            {
                u16 delay = theThrottleFunc(ctx.access().ref(), res.first);

                if (lastSentDelay != static_cast<s32>(delay))
                {
                    logger->debug("sending delay command, lastSentDelay={}, newDelay={}",
                                  lastSentDelay, delay);
                    send_delay_command(ctx.access()->delaySocket, delay);
                    lastSentDelay = delay;
                }

                auto ca = counters.access();
                ca->currentDelay = delay;
                ca->maxDelay = std::max(ca->maxDelay, delay);
                ca->avgDelay = calc_avg_delay(delay, ca->avgDelay);
                ca->rcvBufferSize = res.first.capacity;
                ca->rcvBufferUsed = res.first.used;

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

    eth::close_socket(diagSocket);

    logger->debug("mvlc_eth_throttler leaving loop");
}
#elif defined(__WIN32)
void mvlc_eth_throttler(
    Protected<eth::EthThrottleContext> &ctx,
    Protected<eth::EthThrottleCounters> &counters)
{
    auto logger = get_logger("mvlc_eth_throttler");

    int dataSocket = ctx.access()->dataSocket;
    ReceiveBufferSnapshot rbs = { 0u, static_cast<u32>(ctx.access()->dataSocketReceiveBufferSize) };

    // Use timeBeginPeriod and timeEndPeriod to get better sleep granularity.
    static const unsigned Win32TimePeriod = 1;
    timeBeginPeriod(Win32TimePeriod);
    s32 lastSentDelay = -1;

    logger->debug("mvlc_eth_throttler entering loop");

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
            u16 delay = theThrottleFunc(ctx.access().ref(), rbs);

            if (lastSentDelay != static_cast<s32>(delay))
            {
                logger->debug("sending delay command, lastSentDelay={}, newDelay={}",
                              lastSentDelay, delay);
                send_delay_command(ctx.access()->delaySocket, delay);
                lastSentDelay = delay;
            }

            auto ca = counters.access();
            ca->currentDelay = delay;
            ca->maxDelay = std::max(ca->maxDelay, delay);
            ca->avgDelay = calc_avg_delay(delay, ca->avgDelay);
            ca->rcvBufferSize = rbs.capacity;
            ca->rcvBufferUsed = rbs.used;

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
            logger->warn("WSAIoctl failed: {}", WSAGetLastError());
        }

        auto tDelayDone = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(ctx.access()->queryDelay);
        auto tSleepDone = std::chrono::steady_clock::now();

        auto dtDelay = std::chrono::duration_cast<std::chrono::microseconds>(tDelayDone - tStart);
        auto dtSleep = std::chrono::duration_cast<std::chrono::microseconds>(tSleepDone - tDelayDone);
        auto dtTotal = std::chrono::duration_cast<std::chrono::microseconds>(tSleepDone - tStart);

        //logger->debug("mvlc_eth_throttler: dtDelay=%d us, dtSleep=%d us, dtTotal=%d us",
        //          dtDelay.count(), dtSleep.count(), dtTotal.count());
    }

    timeEndPeriod(Win32TimePeriod);

    logger->debug("mvlc_eth_throttler leaving loop");
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
    , m_throttleCounters()
    , m_throttleContext()
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
    auto logger = get_logger("mvlc_eth");
    logger->trace("begin {}", __PRETTY_FUNCTION__);

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

    logger->trace("looking up host {}...", m_host.c_str());

    if (auto ec = lookup(m_host, CommandPort, m_cmdAddr))
    {
        logger->error("host lookup failed for host {}: {}",
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

    logger->trace("creating sockets...");

    m_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_cmdSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("socket() failed for command pipe: {}", ec.message().c_str());
        return ec;
    }

    m_dataSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_dataSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("socket() failed for data pipe: {}", ec.message().c_str());
        close_sockets();
        return ec;
    }

    m_delaySock = socket(AF_INET, SOCK_DGRAM, 0);

    if (m_delaySock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("socket() failed for delay port: {}", ec.message().c_str());
        close_sockets();
        return ec;
    }

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    logger->trace("binding sockets...");

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

    logger->trace("connecting sockets...");

    // Call connect on the sockets so that we receive only datagrams
    // originating from the MVLC.
    if (::connect(m_cmdSock, reinterpret_cast<struct sockaddr *>(&m_cmdAddr),
                            sizeof(m_cmdAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("connect() failed for command socket: {}", ec.message().c_str());
        close_sockets();
        return ec;
    }

    if (::connect(m_dataSock, reinterpret_cast<struct sockaddr *>(&m_dataAddr),
                            sizeof(m_dataAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("connect() failed for data socket: {}", ec.message().c_str());
        close_sockets();
        return ec;
    }

    if (::connect(m_delaySock, reinterpret_cast<struct sockaddr *>(&m_delayAddr),
                            sizeof(m_delayAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        logger->error("connect() failed for delay socket: {}", ec.message().c_str());
        close_sockets();
        return ec;
    }

    // Set read and write timeouts for the command and data ports.
    logger->trace("setting socket timeouts...");

    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        if (auto ec = set_socket_write_timeout(getSocket(pipe), DefaultWriteTimeout_ms))
        {
            logger->error("set_socket_write_timeout failed: {}, socket={}",
                      ec.message().c_str(),
                      getSocket(pipe));
            return ec;
        }

        if (auto ec = set_socket_read_timeout(getSocket(pipe), DefaultReadTimeout_ms))
        {
            logger->error("set_socket_read_timeout failed: {}", ec.message().c_str());
            return ec;
        }
    }

    // Set the write timeout for the delay socket
    if (auto ec = set_socket_write_timeout(m_delaySock, DefaultWriteTimeout_ms))
    {
        logger->error("set_socket_write_timeout failed: {}", ec.message().c_str());
        return ec;
    }

    // Set socket receive buffer size
    logger->trace("setting socket receive buffer sizes...");

#ifdef __WIN32
    int dataSocketReceiveBufferSize = 0;
#endif

    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        int actualReceiveBufferSize = 0;
        auto ec = set_socket_receive_buffer_size(getSocket(pipe),
            DesiredSocketReceiveBufferSize, &actualReceiveBufferSize);


        if (ec)
        {
            logger->error("setting socket receive buffer size failed: {}", ec.message().c_str());
            return ec;
        }
        else
        {
            logger->debug("pipe={}, SO_RCVBUF={}", static_cast<unsigned>(pipe), actualReceiveBufferSize);

            if (actualReceiveBufferSize < DesiredSocketReceiveBufferSize)
            {
                auto desiredMB = static_cast<double>(DesiredSocketReceiveBufferSize) / util::Megabytes(1);
                auto actualMB = static_cast<double>(actualReceiveBufferSize) / util::Megabytes(1);
                logger->info("pipe={}, requested SO_RCVBUF of {} bytes ({} MB), got {} bytes ({} MB)",
                             static_cast<unsigned>(pipe),
                             DesiredSocketReceiveBufferSize, desiredMB,
                             actualReceiveBufferSize, actualMB);
            }

#ifdef __WIN32
            // This is for the eth throttling code
            if (pipe == Pipe::Data)
                dataSocketReceiveBufferSize = actualBufferSize;
#endif
        }
    }

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    {
        MVLCDialog_internal dlg(this);

        logger->trace("reading MVLC firmware revision...");

        u32 fwRev = 0u;
        if (auto ec = dlg.readRegister(registers::firmware_revision, fwRev))
        {
            close_sockets();
            return ec;
        }

        logger->trace("reading MVLC DAQ mode register...");

        u32 daqMode = 0;
        if (auto ec = read_daq_mode(dlg, daqMode))
        {
            close_sockets();
            return ec;
        }

        if (daqMode && !disableTriggersOnConnect())
        {
            if (fwRev > 0x0034u)
            {
                logger->warn("MVLC is in use (DAQ mode register == 1)");
                close_sockets();
                return make_error_code(MVLCErrorCode::InUse);
            }

            logger->trace("DAQ mode is enabled but detected firmware FW{:04x} <= FW0034: "
                "leaving DAQ mode unchanged", fwRev);
        }
        else if (daqMode)
        {
            if (auto ec = disable_daq_mode(dlg))
            {
                logger->error("MVLC is in use and DAQ mode could not be disabled: {}", ec.message());
                close_sockets();
                return ec;
            }
        }
    }

    logger->trace("ETH connect sequence finished");

    assert(m_cmdSock >= 0 && m_dataSock >= 0 && m_delaySock >= 0);

    // Setup the EthThrottleContext
    {
        auto tc = m_throttleContext.access();
#ifndef __WIN32

        struct stat sb = {};

        if (fstat(m_dataSock, &sb) == 0)
            tc->dataSocketInode = sb.st_ino;

#else // __WIN32
        tc->dataSocket = m_dataSock;
        tc->dataSocketReceiveBufferSize = dataSocketReceiveBufferSize;
#endif

        tc->delaySocket = m_delaySock;
        tc->quit = false;
#if MVLC_ETH_THROTTLE_WRITE_DEBUG_FILE
        tc->debugOut = std::ofstream("mvlc-eth-throttle-debug.txt");
#endif
    }

    m_throttleCounters.access().ref() = {};

#if MVLC_ENABLE_ETH_THROTTLE
    m_throttleThread = std::thread(
        mvlc_eth_throttler,
        std::ref(m_throttleContext),
        std::ref(m_throttleCounters));
#endif

    spdlog::trace("end {}", __PRETTY_FUNCTION__);

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

PacketReadResult Impl::read_packet(Pipe pipe_, u8 *buffer, size_t size)
{
    auto logger = get_logger("mvlc_eth");

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

    size_t bytesTransferred = 0;
    res.ec = receive_one_packet(getSocket(pipe_), buffer, size,
                                bytesTransferred,
                                DefaultReadTimeout_ms);

    res.bytesTransferred = bytesTransferred; // size_t -> u16 but should be ok due to limited udp packet size
    res.buffer = buffer;

    if (res.ec && res.bytesTransferred == 0)
        return res;

    if (res.bytesTransferred >= sizeof(u32)
        && logger->should_log(spdlog::level::trace))
    {
        auto view = basic_string_view<const u32>(
            reinterpret_cast<const u32 *>(res.buffer),
            res.bytesTransferred / sizeof(u32));

        log_buffer(logger, spdlog::level::trace, view, "read_packet(): 32 bit words in packet");
    }

    {
        UniqueLock guard(m_statsMutex);
        ++pipeStats.receivedPackets;
        pipeStats.receivedBytes += res.bytesTransferred;
        ++pipeStats.packetSizes[res.bytesTransferred];
    }

    logger->trace("read_packet: pipe={}, res.bytesTransferred={}", pipe, res.bytesTransferred);

    if (!res.hasHeaders())
    {
        UniqueLock guard(m_statsMutex);
        ++pipeStats.shortPackets;
        logger->warn("read_packet: pipe={}, received data is smaller than the MVLC UDP header size", pipe);
        res.ec = make_error_code(MVLCErrorCode::ShortRead);
        return res;
    }

    logger->trace("read_packet: pipe={}, header0=0x{:008x} -> packetChannel={}, packetNumber={}, controllerId={}, wordCount={}",
              pipe, res.header0(), res.packetChannel(), res.packetNumber(), res.controllerId(), res.dataWordCount());

    logger->trace("read_packet: pipe={}, header1=0x{:008x} -> udpTimestamp={}, nextHeaderPointer={}",
              pipe, res.header1(), res.udpTimestamp(), res.nextHeaderPointer());

    logger->trace("read_packet: pipe={}, calculated available data words = {}, leftover bytes = {}",
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
        //logger->warn("Win10 Build 2004 UDP length hack code path reached!");
        res.bytesTransferred = res.dataWordCount() * sizeof(u32) + eth::HeaderBytes;
    }

    if (res.leftoverBytes() > 0)
    {
        logger->warn("read_packet: pipe={}, {} leftover bytes in received packet",
                 pipe, res.leftoverBytes());
        UniqueLock guard(m_statsMutex);
        ++pipeStats.packetsWithResidue;
    }

    if (res.packetChannel() >= NumPacketChannels)
    {
        logger->warn("read_packet: pipe={}, packet channel number out of range: {}", pipe, res.packetChannel());
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

        logger->trace("read_packet: pipe={}, packetChannel={}, packetNumber={}, lastPacketNumber={}",
                  pipe, res.packetChannel(), res.packetNumber(), lastPacketNumber);

        // Packet loss calculation. The initial lastPacketNumber value is -1.
        if (lastPacketNumber >= 0)
        {
            auto loss = calc_packet_loss(lastPacketNumber, res.packetNumber());

            if (loss > 0)
            {
                logger->debug("read_packet: pipe={}, packetChannel={}, lastPacketNumber={},"
                          " packetNumber={}, loss={}",
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

            logger->info("read_packet: pipe={}, nextHeaderPointer out of range: nHPtr={}, "
                     "availDataWords={}, pktChan={}, pktNum={}, pktSize={} bytes",
                     pipe, res.nextHeaderPointer(), res.availablePayloadWords(),
                     res.packetChannel(), res.packetNumber(), res.bytesTransferred);
        }
        else
        {
            u32 header = *headerp;
            logger->trace("read_packet: pipe={}, nextHeaderPointer={} -> header=0x{:008x}",
                      pipe, res.nextHeaderPointer(), header);
            u32 type = get_frame_type(header);
            UniqueLock guard(m_statsMutex);
            ++pipeStats.headerTypes[type];
            ++channelStats.headerTypes[type];
        }
    }
    else
    {
        logger->trace("read_packet: pipe={}, NoHeaderPointerPresent, eth header1=0x{:008x}",
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
    auto logger = get_logger("mvlc_eth");

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

    logger->trace("read: pipe={}, size={}, bufferAvail={}", pipe, requestedSize, receiveBuffer.available());

    copy_and_update();

    if (size == 0)
    {
        logger->trace("read: pipe={}, size={}, read request satisfied from buffer, new buffer size={}",
                  pipe, requestedSize, receiveBuffer.available());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue actual read requests.
    assert(receiveBuffer.available() == 0);

    size_t readCount = 0u;
    const auto tStart = std::chrono::steady_clock::now();

    while (size > 0)
    {
        assert(receiveBuffer.available() == 0);
        receiveBuffer.reset();

        logger->trace("read: pipe={}, requestedSize={}, remainingSize={}, reading from MVLC...",
                  pipe, requestedSize, size);

        auto rr = read_packet(pipe_, receiveBuffer.buffer.data(), receiveBuffer.buffer.size());

        ++readCount;

        logger->trace("read: pipe={}, received {} bytes, ec={}",
                  pipe, rr.bytesTransferred, rr.ec.message().c_str());

        if (rr.ec && rr.bytesTransferred == 0)
            return rr.ec;

        receiveBuffer.start = reinterpret_cast<u8 *>(rr.payloadBegin());
        receiveBuffer.end   = reinterpret_cast<u8 *>(rr.payloadEnd());

        // Copy to destination buffer
        copy_and_update();

        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

        if (elapsed.count() >= DefaultReadTimeout_ms)
        {
            logger->trace("read: pipe={}, read of size={} completes with {} bytes and timeout"
                      " after {} reads, remaining bytes in buffer={}",
                      pipe, requestedSize, bytesTransferred, readCount,
                      receiveBuffer.available());

            return make_error_code(MVLCErrorCode::SocketReadTimeout);
        }
    }

    logger->trace("read: pipe={}, read of size={} completed using {} reads, remaining bytes in buffer={}",
              pipe, requestedSize, readCount, receiveBuffer.available());

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
    u32 cmdAddress = getCmdAddress();
    std::string remoteIP = cmdAddress ? format_ipv4(cmdAddress) : std::string{};

    if (getHost() != remoteIP)
    {
        std::string result = "mvlc_eth: host=" + getHost();
        if (!remoteIP.empty())
            result += ", address=" + remoteIP;
        return result;
    }

    return "mvlc_eth: address=" + remoteIP;
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
