#include "stat.h"
#include "nodeio.h"
#include "fcntl.h"
#include "errno.h"
#include <stdint.h>

int stat(const char *path, struct stat *buf) {
    if (!path) { errno = ENOENT; return -1; }

    /* "/" напрямую */
    if (path[0] == '/' && path[1] == '\0') {
        int fd = nio_open("/", O_RDONLY);
        if (fd < 0) return -1;
        int r = nio_ioctl(fd, CACT_FDCTL_FSTAT, buf);
        nio_close(fd);
        return nio_map(r);
    }

    /* срезать хвостовые '/' (кроме корня) */
    char pbuf[512];
    size_t i = 0;
    while (path[i] && i < sizeof(pbuf) - 1) { pbuf[i] = path[i]; i++; }
    pbuf[i] = '\0';
    while (i > 1 && pbuf[i - 1] == '/') pbuf[--i] = '\0';

    char base[128];
    int  fd = nio_open_parent(pbuf, base, sizeof(base));
    if (fd < 0) return -1;
    cact_statat_arg_t a;
    a.name = base;
    a.buf  = (cact_stat_t *)buf;   /* layout совпадает: ino/mode/size/type */
    int r = nio_ioctl(fd, CACT_DIRCTL_STAT, &a);
    nio_close(fd);
    return nio_map(r);
}

int fstat(int fd, struct stat *buf) {
    return nio_map(nio_ioctl(fd, CACT_FDCTL_FSTAT, buf));
}

int chmod(const char *path, int mode) {
    char base[128];
    int  fd = nio_open_parent(path, base, sizeof(base));
    if (fd < 0) return -1;
    cact_chmod_arg_t a;
    a.name = base;
    a.mode = (uint32_t)mode;
    int r = nio_ioctl(fd, CACT_DIRCTL_CHMOD, &a);
    nio_close(fd);
    return nio_map(r);
}

int chown(const char *path, int uid, int gid) {
    char base[128];
    int  fd = nio_open_parent(path, base, sizeof(base));
    if (fd < 0) return -1;
    cact_chown_arg_t a;
    a.name = base;
    a.uid  = (uint32_t)uid;
    a.gid  = (uint32_t)gid;
    int r = nio_ioctl(fd, CACT_DIRCTL_CHOWN, &a);
    nio_close(fd);
    return nio_map(r);
}
