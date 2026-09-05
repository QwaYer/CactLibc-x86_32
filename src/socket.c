#include "socket.h"
#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
#include <stdint.h>

int socket(int domain, int type, int protocol) {
    cact_socket_arg_t a;
    a.domain = (uint32_t)domain;
    a.type   = (uint32_t)type;
    a.proto  = (uint32_t)protocol;
    int r = nio_dev_cmd("net", CACT_NETCTL_SOCKET, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

static cact_sockaddr_arg_t _sa(const struct sockaddr_in *addr) {
    cact_sockaddr_arg_t a;
    a.addr.addr = addr->sin_addr;
    a.addr.port = addr->sin_port;
    return a;
}

int bind(int fd, const struct sockaddr_in *addr, uint32_t addrlen) {
    (void)addrlen;
    cact_sockaddr_arg_t a = _sa(addr);
    int r = nio_ioctl(fd, CACT_SOCKCTL_BIND, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int connect(int fd, const struct sockaddr_in *addr, uint32_t addrlen) {
    (void)addrlen;
    cact_sockaddr_arg_t a = _sa(addr);
    int r = nio_ioctl(fd, CACT_SOCKCTL_CONNECT, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int listen(int fd, int backlog) {
    (void)backlog;
    int r = nio_ioctl(fd, CACT_SOCKCTL_LISTEN, 0);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int accept(int fd, struct sockaddr_in *peer, uint32_t *addrlen) {
    cact_accept_arg_t a;
    a.addrlen = 0;
    int r = nio_ioctl(fd, CACT_SOCKCTL_ACCEPT, &a);
    if (r < 0) { errno = -r; return -1; }
    if (peer) {
        peer->sin_family = AF_INET;
        peer->sin_port   = a.peer.port;
        peer->sin_addr   = a.peer.addr;
    }
    if (addrlen) *addrlen = sizeof(struct sockaddr_in);
    return r;
}

int send(int fd, const void *buf, uint32_t len, int flags) {
    (void)flags;
    return (int)syscall(SYS_WRITE, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)len);
}

int recv(int fd, void *buf, uint32_t len, int flags) {
    (void)flags;
    return (int)syscall(SYS_READ, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)len);
}

int sendto(int fd, const void *buf, uint32_t len, int flags,
           const struct sockaddr_in *dest, uint32_t addrlen) {
    (void)flags; (void)addrlen;
    cact_sendto_arg_t a;
    a.dst.addr = dest->sin_addr;
    a.dst.port = dest->sin_port;
    a.buf      = (void *)buf;
    a.len      = len;
    int r = nio_ioctl(fd, CACT_SOCKCTL_SENDTO, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int recvfrom(int fd, void *buf, uint32_t len, int flags,
             struct sockaddr_in *src, uint32_t *addrlen) {
    (void)flags;
    cact_recvfrom_arg_t a;
    a.buf = buf;
    a.len = len;
    int r = nio_ioctl(fd, CACT_SOCKCTL_RECVFROM, &a);
    if (r < 0) { errno = -r; return -1; }
    if (src && r > 0) {
        src->sin_family = AF_INET;
        src->sin_port   = a.src.port;
        src->sin_addr   = a.src.addr;
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
