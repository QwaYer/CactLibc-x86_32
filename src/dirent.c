#include "dirent.h"
#include "nodeio.h"
#include "errno.h"
#include <stdint.h>

int getdents(int fd, struct dirent *buf, unsigned int count) {
    cact_getdents_arg_t a;
    a.buf   = buf;
    a.count = count;
    return nio_map(nio_ioctl(fd, CACT_FDCTL_GETDENTS, &a));
}
