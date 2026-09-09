#ifndef _SOCKET_H
#define _SOCKET_H

#include <stdint.h>
#include "syscall.h"
#include "uio.h"

/* ── Address families ─────────────────────────────────────────────────────── */
#define AF_UNIX     1
#define AF_INET     2
#define AF_LOCAL    AF_UNIX

#define PF_UNIX     AF_UNIX
#define PF_LOCAL    AF_UNIX
#define PF_INET     AF_INET

/* ── Socket types ─────────────────────────────────────────────────────────── */
#define SOCK_STREAM  1
#define SOCK_DGRAM   2

/* Accepted in the type argument of socket/socketpair/accept4 */
#define SOCK_CLOEXEC   0x80000   /* same bit as O_CLOEXEC */
#define SOCK_NONBLOCK  0x800     /* same bit as O_NONBLOCK */

typedef uint32_t socklen_t;

/* ── Protocol numbers ─────────────────────────────────────────────────────── */
#define IPPROTO_TCP   6
#define IPPROTO_UDP  17

/* ── INADDR_ANY ───────────────────────────────────────────────────────────── */
#define INADDR_ANY  0

/* ── shutdown(2) — how values ─────────────────────────────────────────────── */
#define SHUT_RD    0
#define SHUT_WR    1
#define SHUT_RDWR  2

/* ── sendmsg/recvmsg flags & message structures ──────────────────────────── */
#define MSG_CTRUNC    0x8
#define MSG_TRUNC     0x20
#define MSG_DONTWAIT  0x40
#define MSG_NOSIGNAL  0x4000

struct msghdr {
    void     *msg_name;
    uint32_t  msg_namelen;
    struct iovec *msg_iov;
    int       msg_iovlen;
    void     *msg_control;
    uint32_t  msg_controllen;
    int       msg_flags;
};

/* ── Control messages (only SCM_RIGHTS is supported) ─────────────────────── */
#define SCM_RIGHTS 1

struct cmsghdr {
    uint32_t cmsg_len;    /* includes this header */
    int      cmsg_level;
    int      cmsg_type;
    /* payload follows, 4-byte aligned */
};

#define CMSG_ALIGN(n)     (((n) + 3u) & ~3u)
#define CMSG_LEN(n)       (CMSG_ALIGN(sizeof(struct cmsghdr)) + (n))
#define CMSG_SPACE(n)     CMSG_LEN(n)
#define CMSG_DATA(cmsg)   ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))

static inline struct cmsghdr *__cmsg_first(struct msghdr *mh) {
    if (!mh->msg_control || mh->msg_controllen < sizeof(struct cmsghdr))
        return 0;
    return (struct cmsghdr *)mh->msg_control;
}

static inline struct cmsghdr *__cmsg_next(struct msghdr *mh,
                                          struct cmsghdr *c) {
    unsigned char *next = (unsigned char *)c + CMSG_ALIGN(c->cmsg_len);
    if ((unsigned long)(next + sizeof(struct cmsghdr)) >
        (unsigned long)mh->msg_control + mh->msg_controllen)
        return 0;
    return (struct cmsghdr *)next;
}

#define CMSG_FIRSTHDR(mhdr) __cmsg_first(mhdr)
#define CMSG_NXTHDR(mhdr, cmsg) __cmsg_next(mhdr, cmsg)

/* ── Socket option levels ─────────────────────────────────────────────────── */
#define SOL_SOCKET   1
/* IPPROTO_TCP (6) also used as option level */

/* ── SOL_SOCKET option names ──────────────────────────────────────────────── */
#define SO_REUSEADDR  2
#define SO_KEEPALIVE  9
#define SO_ERROR      4
#define SO_PEERCRED   17

/* Peer credentials (only used for AF_UNIX peer querying) */
struct ucred {
    uint32_t pid;
    uint32_t uid;
    uint32_t gid;
};

/* ── IPPROTO_TCP option names ─────────────────────────────────────────────── */
#define TCP_NODELAY   1

/* ── sockaddr structures ──────────────────────────────────────────────────── */
struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;   /* network byte order — use htons() */
    uint32_t sin_addr;   /* network byte order — use htonl() */
    uint8_t  sin_zero[8];
};

struct sockaddr_un {
    uint16_t sun_family;   /* AF_UNIX */
    char     sun_path[108];
};

/* ── Byte-order helpers ───────────────────────────────────────────────────── */
static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x >> 24) & 0xFF)
         | ((x >>  8) & 0xFF00)
         | ((x <<  8) & 0xFF0000)
         | ((x << 24) & 0xFF000000);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/* ── setsockopt / getsockopt args structs (passed via pointer to kernel) ──── */
typedef struct {
    int         fd;
    int         level;
    int         optname;
    const void *optval;
    uint32_t    optlen;
} setsockopt_args_t;

typedef struct {
    int       fd;
    int       level;
    int       optname;
    void     *optval;
    uint32_t *optlen;
} getsockopt_args_t;

/* ── sendto / recvfrom args struct (mirrors the kernel's definition) ──────── */
typedef struct {
    int                       fd;
    const void               *buf;
    uint32_t                  len;
    int                       flags;
    const struct sockaddr_in *dest;
    uint32_t                  addrlen;
} sendto_args_t;

typedef struct {
    int                  fd;
    void                *buf;
    uint32_t             len;
    int                  flags;
    struct sockaddr_in  *src;
    uint32_t            *addrlen;
} recvfrom_args_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

int socket    (int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind      (int fd, const struct sockaddr *addr, uint32_t addrlen);
int connect   (int fd, const struct sockaddr *addr, uint32_t addrlen);
int listen    (int fd, int backlog);
int accept    (int fd, struct sockaddr *addr, uint32_t *addrlen);
int accept4   (int fd, struct sockaddr *addr, uint32_t *addrlen, int flags);
int send      (int fd, const void *buf, uint32_t len, int flags);
int recv      (int fd, void *buf, uint32_t len, int flags);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int fd, struct msghdr *msg, int flags);
int sendto    (int fd, const void *buf, uint32_t len, int flags,
               const struct sockaddr *dest, uint32_t addrlen);
int recvfrom  (int fd, void *buf, uint32_t len, int flags,
               struct sockaddr *src, uint32_t *addrlen);
int shutdown  (int fd, int how);
int setsockopt(int fd, int level, int optname,
               const void *optval, uint32_t optlen);
int getsockopt(int fd, int level, int optname,
               void *optval, uint32_t *optlen);

/* Hostname or dotted-decimal IPv4 → host byte order IPv4 (blocking; needs DNS server from DHCP/netcfg). */
int dns_resolve(const char *name, uint32_t *out_ip_host);

#endif /* _SOCKET_H */
