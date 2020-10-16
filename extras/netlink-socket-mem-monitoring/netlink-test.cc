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
#include <iostream>

using std::cerr;
using std::cout;
using std::endl;

static int
send_query(int fd)
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
    req.diagReq.id.idiag_dport = htons(0x8001);

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
    unsigned int peer = 0;
    size_t path_len = 0;
    char path[sizeof(((struct sockaddr_un *) 0)->sun_path) + 1];

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

#if 0
            case UNIX_DIAG_NAME:
                if (!path_len) {
                    path_len = RTA_PAYLOAD(attr);
                    if (path_len > sizeof(path) - 1)
                        path_len = sizeof(path) - 1;
                    memcpy(path, RTA_DATA(attr), path_len);
                    path[path_len] = '\0';
                }
                break;

            case UNIX_DIAG_PEER:
                if (RTA_PAYLOAD(attr) >= sizeof(peer))
                    peer = *(unsigned int *) RTA_DATA(attr);
                break;
#endif
        }
    }

    //printf("inode=%u", diag->udiag_ino);

    if (peer)
        printf(", peer=%u", peer);

    if (path_len)
        printf(", name=%s%s", *path ? "" : "@",
               *path ? path : path + 1);

    putchar('\n');
    return 0;
}

static int
receive_responses(int fd)
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
                fputs("NLMSG_DONE\n", stderr);
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

            if (print_diag((const inet_diag_msg *) NLMSG_DATA(h), h->nlmsg_len))
                return -1;
        }
    }
}

int
main(void)
{
    /*
    printf("some rta_type values:"
           "INET_DIAG_TOS=%u,"
           "INET_DIAG_TCLASS=%u,"
           "INET_DIAG_MEMINFO=%u,"
           "INET_DIAG_SKMEMINFO=%u,"
           "INET_DIAG_INFO=%u,"
           "INET_DIAG_CONG=%u\n",
           INET_DIAG_TOS,
           INET_DIAG_TCLASS,
           INET_DIAG_MEMINFO,
           INET_DIAG_SKMEMINFO,
           INET_DIAG_INFO,
           INET_DIAG_CONG);
    */

    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);

    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int ret = send_query(fd) || receive_responses(fd);

    close(fd);
    return ret;
}
