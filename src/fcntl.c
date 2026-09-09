#include "fcntl.h"
#include "syscall.h"
#include "nodeio.h"
#include "errno.h"
#include <stdarg.h>
#include <stdint.h>

int open(const char *pathname, int flags, ...) {
    return (int)syscall(SYS_OPEN, (uintptr_t)pathname, (uintptr_t)flags, 0);
}

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    int arg = va_arg(ap, int);
    va_end(ap);

    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
        /* F_DUPFD: дублировать начиная со слота >= arg */
        cact_fcntl_arg_t a;
        a.cmd = CACT_F_DUPFD;
        a.arg = (uint32_t)arg;
        int r = nio_map(nio_ioctl(fd, CACT_FDCTL_FCNTL, &a));
        if (r < 0) return r;
        if (cmd == F_DUPFD_CLOEXEC) {
            cact_fcntl_arg_t f;
            f.cmd = F_SETFD;
            f.arg = FD_CLOEXEC;
            if (nio_map(nio_ioctl(r, CACT_FDCTL_FCNTL, &f)) < 0) {
                close(r);
                errno = EINVAL;
                return -1;
            }
        }
        return r;
    }
    if (cmd == F_GETFD || cmd == F_GETFL) {
        cact_fcntl_arg_t a;
        a.cmd = (uint32_t)cmd;
        a.arg = 0;
        return nio_map(nio_ioctl(fd, CACT_FDCTL_FCNTL, &a));
    }
    if (cmd == F_SETFD || cmd == F_SETFL) {
        cact_fcntl_arg_t a;
        a.cmd = (uint32_t)cmd;
        a.arg = (uint32_t)arg;
        return nio_map(nio_ioctl(fd, CACT_FDCTL_FCNTL, &a));
    }
    errno = EINVAL;
    return -1;
}

int creat(const char *pathname, int mode) {
    (void)mode;
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC);
}
