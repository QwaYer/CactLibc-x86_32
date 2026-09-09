#include <signalfd.h>
#include "nodeio.h"
#include "ioctl_abi.h"
#include "errno.h"
#include <stdint.h>

int signalfd(int fd, const sigset_t *mask, int flags) {
    cact_signalfd_create_arg_t a;
    a.mask  = mask ? (uint32_t)*mask : 0;
    a.flags = (uint32_t)flags & (CACT_SFD_NONBLOCK | CACT_SFD_CLOEXEC);

    if (fd < 0) {
        int r = nio_dev_cmd("signalfd", CACT_SIGNALFDCTL_CREATE, &a);
        if (r < 0) { errno = -r; return -1; }
        return r;
    }

    int r = nio_ioctl(fd, CACT_SIGNALFD_SETMASK, &a);
    if (r < 0) { errno = -r; return -1; }
    return fd;
}
