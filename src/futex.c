#include "futex.h"
#include "nodeio.h"
#include "ioctl_abi.h"
#include "fcntl.h"
#include <stdint.h>

/* One long-lived /proc/self/ctl fd for the whole process.  Opening it per
 * futex operation would race on the shared fd table and churn descriptors. */
static int ctl_fd = -1;

int cact_ctl(unsigned long cmd, void *arg) {
    int fd = ctl_fd;
    if (fd < 0) {
        fd = nio_open("/proc/self/ctl", O_RDWR);
        if (fd < 0) return -1;
        ctl_fd = fd;
    }
    return nio_ioctl(fd, cmd, arg);
}

static int futex_op(volatile int *uaddr, int op, int val, int timeout_ms) {
    cact_futex_arg_t a;
    a.uaddr      = (uint32_t)(uintptr_t)uaddr;
    a.op         = op;
    a.val        = val;
    a.timeout_ms = timeout_ms;
    return cact_ctl(CACT_PROCCTL_FUTEX, &a);
}

int cact_futex_wait(volatile int *uaddr, int expected, int timeout_ms) {
    return futex_op(uaddr, CACT_FUTEX_WAIT, expected, timeout_ms);
}

int cact_futex_wake(volatile int *uaddr, int count) {
    return futex_op(uaddr, CACT_FUTEX_WAKE, count, 0);
}

int cact_futex_wait_shared(volatile int *uaddr, int expected, int timeout_ms) {
    return futex_op(uaddr, CACT_FUTEX_WAIT_SHARED, expected, timeout_ms);
}

int cact_futex_wake_shared(volatile int *uaddr, int count) {
    return futex_op(uaddr, CACT_FUTEX_WAKE_SHARED, count, 0);
}
