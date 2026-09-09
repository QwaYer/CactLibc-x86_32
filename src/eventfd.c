#include <eventfd.h>
#include "nodeio.h"
#include "ioctl_abi.h"
#include "errno.h"

int eventfd(unsigned int initval, int flags) {
    cact_eventfd_create_arg_t a;
    a.initval = initval;
    a.flags   = (uint32_t)flags & (CACT_EFD_SEMAPHORE | CACT_EFD_NONBLOCK | CACT_EFD_CLOEXEC);
    int r = nio_dev_cmd("eventfd", CACT_EVENTFDCTL_CREATE, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}
