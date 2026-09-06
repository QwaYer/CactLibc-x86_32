#include "socket.h"
#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
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

int socket(int domain, int type, int protocol) {
    cact_socket_arg_t a;
    a.domain = (uint32_t)domain;
    a.type   = (uint32_t)type;
    a.proto  = (uint32_t)protocol;
    int r = nio_dev_cmd("net", CACT_NETCTL_SOCKET, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)protocol;
    if (domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
    if (!sv)               { errno = EINVAL;       return -1; }
    cact_socketpair_arg_t a;
    a.type = (uint32_t)type;
    a.fds[0] = 0;
    a.fds[1] = 0;
    int r = nio_dev_cmd("net", CACT_NETCTL_SOCKETPAIR, &a);
    if (r < 0) { errno = -r; return -1; }
    sv[0] = (int)a.fds[0];
    sv[1] = (int)a.fds[1];
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
