#include "sys/random.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"

int getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    if (!buf && buflen) { errno = EINVAL; return -1; }
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { errno = EIO; return -1; }
    size_t got = 0;
    while (got < buflen) {
        ssize_t r = read(fd, (char *)buf + got, buflen - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    return (int)got;
}
