/* netlink-monitor-rmem.cc - Testbed for MVLC Eth data rate throttling
 *
 * f.lueke@mesytec.com
 *
 * Uses the Linux netlink API to get detailed information about the data socket buffer
 * size and fill level from the kernel (SK_MEMINFO). The buffer memory information is then
 * used to calculate a delay value to send to the MVLCs delay port.
 *
 * Both netlink stats updates and delay commands are done in a loop with a QueryDelay
 * between iterations.
 *
 * The program produces an output file containing some of the data of the SK_MEMINFO data
 * and information about the last sent delay command. netlink-monitor-rmem-plotter.py can
 * be used to produce visualizations of the data.
 *
 * The socket to monitor is currently filtered by dstPort only. Running multiple instances
 * of mvme or the mini-daq tools will confuse this program. Stricter matching of the
 * socket would be needed to handle these cases.
 *
 */


#include <chrono>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>

#include <arpa/inet.h>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <signal.h>
#include <thread>

#include <mesytec-mvlc/mesytec-mvlc.h>

using std::cerr;
using std::cout;
using std::endl;

using namespace mesytec::mvlc;

static int
send_query(int fd, u16 srcPort, u16 dstPort)
{
    struct sockaddr_nl nladdr = {
        .nl_family = AF_NETLINK
    };

    struct
    {
        struct nlmsghdr nlh;
        struct inet_diag_req_v2 req;
    } req = {
        .nlh = {
            .nlmsg_len = sizeof(req),
            .nlmsg_type = SOCK_DIAG_BY_FAMILY,
            .nlmsg_flags = NLM_F_REQUEST | NLM_F_MATCH,
        },
        .req = {
            .sdiag_family = AF_INET,
            .sdiag_protocol = IPPROTO_UDP,
            .idiag_ext = (1u << (INET_DIAG_SKMEMINFO - 1)),
            .pad = 0,
            .idiag_states = 0xffffffffu, // All states (0 filters out all sockets).
            .id = {
                .idiag_sport = ntohs(srcPort),
                .idiag_dport = htons(dstPort),
                // Note: idiag_dst and idiag_src don't have any effect (at least with kernel 4.19).
            }
        }
    };

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
        if (sendmsg(fd, &msg, 0) < 0) {
            if (errno == EINTR)
                continue;

            perror("sendmsg");
            return -1;
        }

        return 0;
    }
}

static int
print_diag(const struct inet_diag_msg *diag, unsigned int len)
{
    if (len < NLMSG_LENGTH(sizeof(*diag))) {
        fputs("short response\n", stderr);
        return -1;
    }
    if (diag->idiag_family != AF_INET) {
        fprintf(stderr, "unexpected family %u\n", diag->idiag_family);
        return -1;
    }

    printf("diag: state=%u, timer=%u, retrans=%u\n",
           diag->idiag_state, diag->idiag_timer, diag->idiag_retrans);
    printf("      id.sport=%u, id.dport=%u\n",
           ntohs(diag->id.idiag_sport), ntohs(diag->id.idiag_dport));


    char srcAddr[50] = { 0 };
    char dstAddr[50] = { 0 };

    strcpy(srcAddr, inet_ntoa(*(struct in_addr *)diag->id.idiag_src));
    strcpy(dstAddr, inet_ntoa(*(struct in_addr *)diag->id.idiag_dst));

    printf("      id.idiag_src=%s, id.idiag_dst=%s\n",
           srcAddr, dstAddr);
    printf("      rqueue=%u, wqueue=%u\n",
           diag->idiag_rqueue, diag->idiag_wqueue);
    printf("      uid=%u, inode=%u\n",
           diag->idiag_uid, diag->idiag_inode);

    struct rtattr *attr = nullptr;
    unsigned int rta_len = len - NLMSG_LENGTH(sizeof(*diag));

    struct inet_diag_meminfo memInfo = {};

    for (attr = (struct rtattr *) (diag + 1);
         RTA_OK(attr, rta_len);
         attr = RTA_NEXT(attr, rta_len))
    {
        printf("attr->rta_type=%u\n", attr->rta_type);

        switch (attr->rta_type) {

            case INET_DIAG_MEMINFO:
                if (RTA_PAYLOAD(attr) >= sizeof(memInfo))
                {
                    memInfo = *(const inet_diag_meminfo *) RTA_DATA(attr);
                    printf("INET_DIAG_MEMINFO: rmem=%u, wmem=%u, fmem=%u, tmem=%u\n",
                           memInfo.idiag_rmem,
                           memInfo.idiag_wmem,
                           memInfo.idiag_fmem,
                           memInfo.idiag_tmem);
                }
                break;

            case INET_DIAG_SKMEMINFO:
                if (RTA_PAYLOAD(attr) >= sizeof(uint32_t) * SK_MEMINFO_VARS)
                {
                    const uint32_t *memInfo = (const uint32_t *) RTA_DATA(attr);

                    printf("INET_DIAG_SKMEMINFO: rmem_alloc=%u, rcvbuf=%u,"
                           "wmem_alloc=%u, sndbuf=%u, fwd_alloc=%u, wmem_queued=%u,"
                           "optmem=%u, backlog=%u\n",
                           memInfo[SK_MEMINFO_RMEM_ALLOC],
                           memInfo[SK_MEMINFO_RCVBUF],
                           memInfo[SK_MEMINFO_WMEM_ALLOC],
                           memInfo[SK_MEMINFO_SNDBUF],
                           memInfo[SK_MEMINFO_FWD_ALLOC],
                           memInfo[SK_MEMINFO_WMEM_QUEUED],
                           memInfo[SK_MEMINFO_OPTMEM],
                           memInfo[SK_MEMINFO_BACKLOG]);
                }
                break;
        }
    }

    //printf("inode=%u", diag->udiag_ino);
    putchar('\n');
    return 0;
}

using DiagCallback = std::function<int (const inet_diag_msg *msg, unsigned len)>;

static int
receive_responses(int fd, DiagCallback callback)
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

    for (;;) {
        struct msghdr msg = {
            .msg_name = (void *) &nladdr,
            .msg_namelen = sizeof(nladdr),
            .msg_iov = &iov,
            .msg_iovlen = 1
        };

        ssize_t ret = recvmsg(fd, &msg, flags);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            perror("recvmsg");
            return -1;
        }
        if (ret == 0)
            return 0;

        const struct nlmsghdr *h = (struct nlmsghdr *) buf;

        if (!NLMSG_OK(h, ret)) {
            fputs("!NLMSG_OK\n", stderr);
            return -1;
        }

        for (; NLMSG_OK(h, ret); h = NLMSG_NEXT(h, ret)) {
            if (h->nlmsg_type == NLMSG_DONE)
            {
                //fputs("NLMSG_DONE\n", stderr);
                return 0;
            }

            if (h->nlmsg_type == NLMSG_ERROR) {
                const struct nlmsgerr *err = (const struct nlmsgerr *) NLMSG_DATA(h);

                if (h->nlmsg_len < NLMSG_LENGTH(sizeof(*err))) {
                    fputs("NLMSG_ERROR\n", stderr);
                } else {
                    errno = -err->error;
                    perror("NLMSG_ERROR");
                }

                return -1;
            }

            if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY) {
                fprintf(stderr, "unexpected nlmsg_type %u\n",
                        (unsigned) h->nlmsg_type);
                return -1;
            }

            if (int res = callback((const inet_diag_msg *) NLMSG_DATA(h), h->nlmsg_len))
                return res;
        }
    }
}

static volatile bool signal_received = false;

void signal_handler(int signum)
{
#ifndef _WIN32
    cout << "signal " << signum << endl;
    cout.flush();
    signal_received = true;
#endif
}

void setup_signal_handlers()
{
#ifndef _WIN32
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
#endif
}

static ssize_t send_delay(int delaySock, u16 delay)
{
    const u32 cmd = (0x0207u << 16) | delay;

    ssize_t res = ::send(delaySock, reinterpret_cast<const char *>(&cmd), sizeof(cmd), 0);

    if (res < 0)
        perror("send_delay");

    return res;
}

struct ReadoutDelayContext
{
    std::ofstream &diagOut;
    int delaySock = -1;
    u16 currentDelay = 0u;
};

// TODO: manually filter by srcAddr and dstAddr (kernel side filtering doesn't seem to work with addresses).
int rmem_monitor(const inet_diag_msg *diag, unsigned len, ReadoutDelayContext &ctx)
{
    auto &out = ctx.diagOut;

    if (len < NLMSG_LENGTH(sizeof(*diag))) {
        fputs("short response\n", stderr);
        return -1;
    }

    if (diag->idiag_family != AF_INET) {
        fprintf(stderr, "unexpected family %u\n", diag->idiag_family);
        return -1;
    }

    char srcAddr[50] = { 0 };
    char dstAddr[50] = { 0 };

    strcpy(srcAddr, inet_ntoa(*(struct in_addr *)diag->id.idiag_src));
    strcpy(dstAddr, inet_ntoa(*(struct in_addr *)diag->id.idiag_dst));

    struct rtattr *attr = nullptr;
    unsigned int rta_len = len - NLMSG_LENGTH(sizeof(*diag));

    bool skMemSeen = false;
    u32 rmem_alloc = 0u;
    u32 rcvbuf_capacity = 0u;

    for (attr = (struct rtattr *) (diag + 1);
         RTA_OK(attr, rta_len) && !skMemSeen;
         attr = RTA_NEXT(attr, rta_len))
    {
        printf("attr->rta_type=%u\n", attr->rta_type);

        switch (attr->rta_type)
        {
            case INET_DIAG_SKMEMINFO:
                if (RTA_PAYLOAD(attr) >= sizeof(uint32_t) * SK_MEMINFO_VARS)
                {
                    const uint32_t *memInfo = (const uint32_t *) RTA_DATA(attr);

                    rmem_alloc = memInfo[SK_MEMINFO_RMEM_ALLOC];
                    rcvbuf_capacity = memInfo[SK_MEMINFO_RCVBUF];

                    skMemSeen = true;
                }
                break;
        }
    }

    if (skMemSeen)
    {
        double bufferUse = rmem_alloc * 1.0 / rcvbuf_capacity;
        /* At 50% buffer level start throttling. Delay value scales in a range of 32% of buffer usage,
         * starting from 1us at 50% usage, doubling every 2%. So we get 16 2% steps in the 32% range which
         * will yield a delay value of 2^16 at 50%+32% buffer fill level.
         */
        const u16 StartDelay_us = 1u;
        const double ThrottleRange = 0.45;
        const unsigned ThrottleSteps = 16;
        const double ThrottleIncrement = ThrottleRange / ThrottleSteps;
        const double ThrottleMinThreshold = 0.5;
        const double ThrottleMaxThreshold = ThrottleMinThreshold + ThrottleRange;

        double aboveThreshold = 0.0;
        u32 increments = 0u;

        if (bufferUse <= ThrottleMinThreshold)
            ctx.currentDelay = 0;
        else
        {
            aboveThreshold = bufferUse - ThrottleMinThreshold;
            increments = std::floor(aboveThreshold / ThrottleIncrement);

            if (increments > ThrottleSteps)
                increments = ThrottleSteps;

            ctx.currentDelay = (StartDelay_us << increments) /*- 1u*/;
        }

        auto res = send_delay(ctx.delaySock, ctx.currentDelay);

        if (res < 0)
            return res;


        out << srcAddr << ":" << ntohs(diag->id.idiag_sport)
            << "<-"
            << dstAddr << ":" << ntohs(diag->id.idiag_dport)
            << " rmem_alloc=" << rmem_alloc
            << " rcvbuf=" << rcvbuf_capacity
            << " delay=" << ctx.currentDelay
            << " bufferUse=" << bufferUse
            << " aboveThreshold=" << aboveThreshold
            << " increments=" << increments
            << " inode=" << diag->idiag_inode
            << endl;
    }

    return 0;
}

#if 0
                    printf("INET_DIAG_SKMEMINFO: rmem_alloc=%u, rcvbuf=%u,"
                           "wmem_alloc=%u, sndbuf=%u, fwd_alloc=%u, wmem_queued=%u,"
                           "optmem=%u, backlog=%u\n",
                           memInfo[SK_MEMINFO_RMEM_ALLOC],
                           memInfo[SK_MEMINFO_RCVBUF],
                           memInfo[SK_MEMINFO_WMEM_ALLOC],
                           memInfo[SK_MEMINFO_SNDBUF],
                           memInfo[SK_MEMINFO_FWD_ALLOC],
                           memInfo[SK_MEMINFO_WMEM_QUEUED],
                           memInfo[SK_MEMINFO_OPTMEM],
                           memInfo[SK_MEMINFO_BACKLOG]);
#endif

static const auto QueryDelay = std::chrono::milliseconds(10);

int
main(void)
{
    std::string srcHost = "0.0.0.0";
    std::string dstHost = "192.168.168.160";
    u16 srcPort = 0;
    u16 dstPort = 0x8001;
    u16 delayPort = 0x8002;
    std::string outputFilename = "rmem-usage.txt";

    // TODO: allow overriding address and port values via the command line

    //
    // Netlink socket
    //
    int netlinkSock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);

    if (netlinkSock < 0) {
        perror("error creating netlink socket");
        return 1;
    }

    //
    // Delay socket
    //
    int delaySock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (delaySock < 0)
    {
        perror("error creating delay socket");
        return 1;
    }

    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(delaySock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
            perror("error binding delay socket");
            return 1;
        }
    }

    {
        struct sockaddr_in delayAddr = {};
        delayAddr.sin_family = AF_INET;
        inet_aton(dstHost.c_str(), &delayAddr.sin_addr);
        delayAddr.sin_port = htons(delayPort);

        if (::connect(delaySock, reinterpret_cast<struct sockaddr *>(&delayAddr),
                      sizeof(delayAddr)))
        {
            perror("error connecting delay socket");
            return 1;
        }
    }

    //
    // Output file, callback setup, main loop
    //
    std::ofstream logOut(outputFilename);

    ReadoutDelayContext delayContext { logOut, delaySock, 0 };

    auto the_callback = [&delayContext] (const inet_diag_msg *diag, unsigned len)
    {
        return rmem_monitor(diag, len, delayContext);
    };

    setup_signal_handlers();

    int ret = 0;

    while (!signal_received)
    {
        if ((ret = send_query(netlinkSock, srcPort, dstPort)) != 0)
            break;

        if ((ret = receive_responses(netlinkSock, the_callback)) != 0)
            break;

        std::this_thread::sleep_for(QueryDelay);
    }

    close(netlinkSock);
    return ret;
}
