#include "socket.h"
#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <stdint.h>

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#ifndef EINVAL
#define EINVAL 22
#endif

static uint16_t _family(const struct sockaddr *sa) {
    uint16_t f;
    __builtin_memcpy(&f, sa, sizeof(f));
    return f;
}

static int _fd_apply_flags(int fd, int type_flags) {
    if (type_flags & SOCK_CLOEXEC) {
        cact_fcntl_arg_t f;
        f.cmd = F_SETFD;
        f.arg = FD_CLOEXEC;
        if (nio_map(nio_ioctl(fd, CACT_FDCTL_FCNTL, &f)) < 0)
            return -1;
    }
    if (type_flags & SOCK_NONBLOCK) {
        cact_fcntl_arg_t f;
        f.cmd = F_SETFL;
        f.arg = O_NONBLOCK;
        if (nio_map(nio_ioctl(fd, CACT_FDCTL_FCNTL, &f)) < 0)
            return -1;
    }
    return 0;
}

int socket(int domain, int type, int protocol) {
    int flags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK);
    cact_socket_arg_t a;
    a.domain = (uint32_t)domain;
    a.type   = (uint32_t)(type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK));
    a.proto  = (uint32_t)protocol;
    int r = nio_dev_cmd("net", CACT_NETCTL_SOCKET, &a);
    if (r < 0) { errno = -r; return -1; }
    if (flags && _fd_apply_flags(r, flags) < 0) {
        close(r);
        errno = EINVAL;
        return -1;
    }
    return r;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)protocol;
    if (domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
    if (!sv)               { errno = EINVAL;       return -1; }
    int flags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK);
    cact_socketpair_arg_t a;
    a.type = (uint32_t)(type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK));
    a.fds[0] = 0;
    a.fds[1] = 0;
    int r = nio_dev_cmd("net", CACT_NETCTL_SOCKETPAIR, &a);
    if (r < 0) { errno = -r; return -1; }
    sv[0] = (int)a.fds[0];
    sv[1] = (int)a.fds[1];
    if (flags) {
        if (_fd_apply_flags(sv[0], flags) < 0 || _fd_apply_flags(sv[1], flags) < 0) {
            close(sv[0]);
            close(sv[1]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int bind(int fd, const struct sockaddr *addr, uint32_t addrlen) {
    if (!addr) { errno = EINVAL; return -1; }
    uint16_t family = _family(addr);

    if (family == AF_UNIX) {
        if (addrlen <= sizeof(uint16_t)) { errno = EINVAL; return -1; }
        const struct sockaddr_un *sun = (const struct sockaddr_un *)addr;
        cact_unix_addr_t ua;
        __builtin_memset(&ua, 0, sizeof(ua));
        uint32_t n = addrlen - sizeof(uint16_t);
        if (n > sizeof(ua.path)) n = sizeof(ua.path);
        __builtin_memcpy(ua.path, sun->sun_path, n);
        ua.path[sizeof(ua.path) - 1] = '\0';
        int r = nio_ioctl(fd, CACT_SOCKCTL_UNIX_BIND, &ua);
        if (r < 0) { errno = -r; return -1; }
        return 0;
    }

    if (family == AF_INET) {
        if (addrlen < sizeof(struct sockaddr_in)) { errno = EINVAL; return -1; }
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        cact_sockaddr_arg_t a;
        a.addr.addr = sin->sin_addr;
        a.addr.port = sin->sin_port;
        int r = nio_ioctl(fd, CACT_SOCKCTL_BIND, &a);
        if (r < 0) { errno = -r; return -1; }
        return 0;
    }

    errno = EAFNOSUPPORT;
    return -1;
}

int connect(int fd, const struct sockaddr *addr, uint32_t addrlen) {
    if (!addr) { errno = EINVAL; return -1; }
    uint16_t family = _family(addr);

    if (family == AF_UNIX) {
        if (addrlen <= sizeof(uint16_t)) { errno = EINVAL; return -1; }
        const struct sockaddr_un *sun = (const struct sockaddr_un *)addr;
        cact_unix_addr_t ua;
        __builtin_memset(&ua, 0, sizeof(ua));
        uint32_t n = addrlen - sizeof(uint16_t);
        if (n > sizeof(ua.path)) n = sizeof(ua.path);
        __builtin_memcpy(ua.path, sun->sun_path, n);
        ua.path[sizeof(ua.path) - 1] = '\0';
        int r = nio_ioctl(fd, CACT_SOCKCTL_UNIX_CONNECT, &ua);
        if (r < 0) { errno = -r; return -1; }
        return 0;
    }

    if (family == AF_INET) {
        if (addrlen < sizeof(struct sockaddr_in)) { errno = EINVAL; return -1; }
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        cact_sockaddr_arg_t a;
        a.addr.addr = sin->sin_addr;
        a.addr.port = sin->sin_port;
        int r = nio_ioctl(fd, CACT_SOCKCTL_CONNECT, &a);
        if (r < 0) { errno = -r; return -1; }
        return 0;
    }

    errno = EAFNOSUPPORT;
    return -1;
}

int listen(int fd, int backlog) {
    (void)backlog;
    int r = nio_ioctl(fd, CACT_SOCKCTL_LISTEN, 0);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int accept(int fd, struct sockaddr *addr, uint32_t *addrlen) {
    cact_accept_arg_t a;
    a.addrlen = 0;
    int r = nio_ioctl(fd, CACT_SOCKCTL_ACCEPT, &a);
    if (r < 0) { errno = -r; return -1; }

    if (addrlen) {
        if (a.addrlen == sizeof(struct sockaddr_in)) {
            /* AF_INET peer filled in by the kernel */
            *addrlen = sizeof(struct sockaddr_in);
            if (addr) {
                struct sockaddr_in *sin = (struct sockaddr_in *)addr;
                sin->sin_family = AF_INET;
                sin->sin_port   = a.peer.port;
                sin->sin_addr   = a.peer.addr;
            }
        } else {
            /* AF_UNIX: peer is unnamed */
            *addrlen = sizeof(uint16_t);
            if (addr) {
                struct sockaddr *sa = addr;
                sa->sa_family = AF_UNIX;
            }
        }
    }
    return r;
}

int accept4(int fd, struct sockaddr *addr, uint32_t *addrlen, int flags) {
    int nfd = accept(fd, addr, addrlen);
    if (nfd < 0) return nfd;
    if ((flags & (SOCK_CLOEXEC | SOCK_NONBLOCK)) &&
        _fd_apply_flags(nfd, flags) < 0) {
        close(nfd);
        errno = EINVAL;
        return -1;
    }
    return nfd;
}

int send(int fd, const void *buf, uint32_t len, int flags) {
    (void)flags;
    int r = (int)syscall(SYS_WRITE, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)len);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int recv(int fd, void *buf, uint32_t len, int flags) {
    (void)flags;
    int r = (int)syscall(SYS_READ, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)len);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

/* ── sendmsg / recvmsg (SCM_RIGHTS over AF_UNIX stream) ─────────────────── */

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    (void)flags;
    if (!msg || !msg->msg_iov || msg->msg_iovlen <= 0) { errno = EINVAL; return -1; }

    uint32_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        uint32_t l = (uint32_t)msg->msg_iov[i].iov_len;
        if (total + l < total) { errno = EINVAL; return -1; }
        total += l;
    }

    unsigned char *buf = 0;
    if (total) {
        buf = (unsigned char *)malloc(total);
        if (!buf) { errno = ENOMEM; return -1; }
        uint32_t off = 0;
        for (int i = 0; i < msg->msg_iovlen; i++) {
            uint32_t l = (uint32_t)msg->msg_iov[i].iov_len;
            if (l) {
                memcpy(buf + off, msg->msg_iov[i].iov_base, l);
                off += l;
            }
        }
    }

    int32_t fds[16];
    uint32_t nfds = 0;
    if (msg->msg_control && msg->msg_controllen) {
        for (struct cmsghdr *c = CMSG_FIRSTHDR((struct msghdr *)msg);
             c && nfds < 16;
             c = CMSG_NXTHDR((struct msghdr *)msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                uint32_t hdr = CMSG_ALIGN(sizeof(struct cmsghdr));
                uint32_t bytes = (c->cmsg_len >= hdr) ? c->cmsg_len - hdr : 0;
                int cnt = (int)(bytes / sizeof(int32_t));
                if (cnt > 16 - (int)nfds) cnt = 16 - (int)nfds;
                memcpy(fds + nfds, CMSG_DATA(c), (uint32_t)cnt * sizeof(int32_t));
                nfds += (uint32_t)cnt;
            }
        }
    }

    cact_sendmsg_arg_t a;
    a.buf  = buf;
    a.len  = total;
    a.fds  = nfds ? fds : 0;
    a.nfds = nfds;
    int r = nio_ioctl(fd, CACT_SOCKCTL_SENDMSG, &a);
    if (total) free(buf);
    if (r < 0) { errno = -r; return -1; }
    return (ssize_t)r;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags) {
    (void)flags;
    if (!msg) { errno = EINVAL; return -1; }

    uint32_t cap = 0;
    if (msg->msg_iov && msg->msg_iovlen > 0) {
        for (int i = 0; i < msg->msg_iovlen; i++)
            cap += (uint32_t)msg->msg_iov[i].iov_len;
    }

    unsigned char *buf = 0;
    if (cap) {
        buf = (unsigned char *)malloc(cap);
        if (!buf) { errno = ENOMEM; return -1; }
    }

    int32_t fds[16];
    cact_recvmsg_arg_t a;
    a.buf      = buf;
    a.cap      = cap;
    a.fds      = fds;
    a.fds_cap  = 16;
    a.fds_len  = 0;
    int r = nio_ioctl(fd, CACT_SOCKCTL_RECVMSG, &a);
    if (r < 0) {
        if (cap) free(buf);
        errno = -r;
        return -1;
    }
    int got = r;
    uint32_t nfds = a.fds_len;

    /* scatter payload into the iov */
    uint32_t off = 0;
    for (int i = 0; i < msg->msg_iovlen && off < (uint32_t)got; i++) {
        uint32_t l = (uint32_t)msg->msg_iov[i].iov_len;
        uint32_t take = got - off;
        if (take > l) take = l;
        if (take && buf) memcpy(msg->msg_iov[i].iov_base, buf + off, take);
        off += take;
    }
    if (cap) free(buf);

    msg->msg_flags = 0;
    if (msg->msg_name && msg->msg_namelen >= sizeof(uint16_t))
        ((struct sockaddr *)msg->msg_name)->sa_family = AF_UNIX;
    msg->msg_namelen = 0;

    if (nfds == 0) {
        msg->msg_controllen = 0;
        return (ssize_t)got;
    }

    if (msg->msg_control && msg->msg_controllen >= sizeof(struct cmsghdr)) {
        uint32_t need = CMSG_SPACE(nfds * sizeof(int32_t));
        if (need <= msg->msg_controllen) {
            struct cmsghdr *c = (struct cmsghdr *)msg->msg_control;
            c->cmsg_len   = CMSG_LEN(nfds * sizeof(int32_t));
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type  = SCM_RIGHTS;
            memcpy(CMSG_DATA(c), fds, nfds * sizeof(int32_t));
            msg->msg_controllen = need;
        } else {
            for (uint32_t i = 0; i < nfds; i++) close((int)fds[i]);
            msg->msg_controllen = 0;
            msg->msg_flags |= MSG_CTRUNC;
        }
    } else {
        for (uint32_t i = 0; i < nfds; i++) close((int)fds[i]);
        msg->msg_controllen = 0;
        msg->msg_flags |= MSG_CTRUNC;
    }
    return (ssize_t)got;
}

int sendto(int fd, const void *buf, uint32_t len, int flags,
           const struct sockaddr *dest, uint32_t addrlen) {
    (void)flags;
    if (!dest) { errno = EINVAL; return -1; }
    if (_family(dest) != AF_INET) { errno = EAFNOSUPPORT; return -1; }
    if (addrlen < sizeof(struct sockaddr_in)) { errno = EINVAL; return -1; }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)dest;
    cact_sendto_arg_t a;
    a.dst.addr = sin->sin_addr;
    a.dst.port = sin->sin_port;
    a.buf      = (void *)buf;
    a.len      = len;
    int r = nio_ioctl(fd, CACT_SOCKCTL_SENDTO, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int recvfrom(int fd, void *buf, uint32_t len, int flags,
             struct sockaddr *src, uint32_t *addrlen) {
    (void)flags;
    cact_recvfrom_arg_t a;
    a.buf = buf;
    a.len = len;
    int r = nio_ioctl(fd, CACT_SOCKCTL_RECVFROM, &a);
    if (r < 0) { errno = -r; return -1; }
    if (src && r > 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)src;
        sin->sin_family = AF_INET;
        sin->sin_port   = a.src.port;
        sin->sin_addr   = a.src.addr;
        if (addrlen) *addrlen = sizeof(struct sockaddr_in);
    }
    return r;
}

int shutdown(int fd, int how) {
    uint32_t h = (uint32_t)how;
    int r = nio_ioctl(fd, CACT_SOCKCTL_SHUTDOWN, &h);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int setsockopt(int fd, int level, int optname,
               const void *optval, uint32_t optlen) {
    if (!optval || optlen < sizeof(int)) { errno = EINVAL; return -1; }
    cact_sockopt_arg_t a;
    a.level   = (uint32_t)level;
    a.optname = (uint32_t)optname;
    a.val     = (uint32_t)*(const int *)optval;
    int r = nio_ioctl(fd, CACT_SOCKCTL_SETSOCKOPT, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int getsockopt(int fd, int level, int optname,
               void *optval, uint32_t *optlen) {
    if (!optval || !optlen || *optlen < sizeof(int)) {
        if (optlen) errno = EINVAL;
        return -1;
    }
    cact_sockopt_arg_t a;
    a.level   = (uint32_t)level;
    a.optname = (uint32_t)optname;
    int r = nio_ioctl(fd, CACT_SOCKCTL_GETSOCKOPT, &a);
    if (r < 0) { errno = -r; return -1; }
    *(int *)optval   = (int)a.val_out;
    *optlen          = sizeof(int);
    return 0;
}
