#include <epoll.h>
#include "nodeio.h"
#include "ioctl_abi.h"
#include "errno.h"
#include <stdint.h>

int epoll_create1(int flags) {
    cact_epoll_create_arg_t a;
    a.flags = (uint32_t)flags & CACT_EPOLL_CLOEXEC;
    int r = nio_dev_cmd("epoll", CACT_EPOLLCTL_CREATE, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int epoll_create(int size) {
    (void)size;
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    cact_epoll_ctl_arg_t a;
    a.op     = op;
    a.fd     = fd;
    a.events = event ? event->events : 0;
    a.data   = event ? event->data.u64 : 0;

    int r = nio_ioctl(epfd, CACT_EPOLL_CTL, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout_ms) {
    if (maxevents <= 0 || !events) { errno = EINVAL; return -1; }

    cact_epoll_wait_arg_t a;
    a.events     = events;
    a.maxevents  = (uint32_t)maxevents;
    a.timeout_ms = timeout_ms;

    int r = nio_ioctl(epfd, CACT_EPOLL_WAIT, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}
