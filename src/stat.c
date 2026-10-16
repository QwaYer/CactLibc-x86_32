#include "stat.h"
#include "nodeio.h"
#include "fcntl.h"
#include "errno.h"
#include <stdint.h>

int stat(const char *path, struct stat *buf) {
    if (!path) { errno = ENOENT; return -1; }

    /* "/" directly */
    if (path[0] == '/' && path[1] == '\0') {
        int fd = nio_open("/", O_RDONLY);
        if (fd < 0) return -1;
        int r = nio_ioctl(fd, CACT_FDCTL_FSTAT, buf);
        nio_close(fd);
        return nio_map(r);
    }

    /* strip trailing '/' (except the root) */
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
    a.buf  = (cact_stat_t *)buf;   /* layout matches: ino/mode/size/type */
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

int lstat(const char *path, struct stat *buf) {
    if (!path || !buf) { errno = ENOENT; return -1; }

    /* "/" is never a symlink: nothing to not-follow. */
    if (path[0] == '/' && path[1] == '\0') return stat(path, buf);

    char pbuf[512];
    size_t i = 0;
    while (path[i] && i < sizeof(pbuf) - 1) { pbuf[i] = path[i]; i++; }
    pbuf[i] = '\0';
    while (i > 1 && pbuf[i - 1] == '/') pbuf[--i] = '\0';

    char base[128];
    int  fd = nio_open_parent(pbuf, base, sizeof(base));
    if (fd < 0) return -1;

    /* The 4-word cact_stat cannot carry a link target, so ask the rich stat
     * with NOFOLLOW and keep the fields struct stat has. */
    cact_statx_t sx;
    cact_statx_arg_t a;
    a.name  = base;
    a.buf   = &sx;
    a.flags = CACT_AT_SYMLINK_NOFOLLOW;
    int r = nio_ioctl(fd, CACT_DIRCTL_STATX, &a);
    nio_close(fd);
    if (r < 0) return nio_map(r);

    buf->st_ino  = sx.ino;
    buf->st_mode = sx.mode;
    buf->st_size = sx.size;
    buf->st_type = sx.type;
    return 0;
}

/* Resolve the filesystem containing `path` via DIRCTL_STATFS. */
int statfs(const char *path, cact_statfs_t *buf) {
    if (!path || !buf) { errno = ENOENT; return -1; }

    if (path[0] == '/' && path[1] == '\0') {
        int fd = nio_open("/", O_RDONLY);
        if (fd < 0) return -1;
        cact_statfs_arg_t a;
        a.name = 0;              /* the directory's own filesystem */
        a.buf  = buf;
        int r = nio_ioctl(fd, CACT_DIRCTL_STATFS, &a);
        nio_close(fd);
        return nio_map(r);
    }

    char pbuf[512];
    size_t i = 0;
    while (path[i] && i < sizeof(pbuf) - 1) { pbuf[i] = path[i]; i++; }
    pbuf[i] = '\0';
    while (i > 1 && pbuf[i - 1] == '/') pbuf[--i] = '\0';

    char base[128];
    int  fd = nio_open_parent(pbuf, base, sizeof(base));
    if (fd < 0) return -1;
    cact_statfs_arg_t a;
    a.name = base;
    a.buf  = buf;
    int r = nio_ioctl(fd, CACT_DIRCTL_STATFS, &a);
    nio_close(fd);
    return nio_map(r);
}

/* Rich stat (statx).  Follows a final symlink unless CACT_AT_SYMLINK_NOFOLLOW. */
int cact_statx(const char *path, cact_statx_t *buf, uint32_t flags) {
    if (!path || !buf) { errno = ENOENT; return -1; }

    if (path[0] == '/' && path[1] == '\0') {
        int fd = nio_open("/", O_RDONLY);
        if (fd < 0) return -1;
        int r = nio_ioctl(fd, CACT_FDCTL_FSTATX, buf);
        nio_close(fd);
        return nio_map(r);
    }

    char pbuf[512];
    size_t i = 0;
    while (path[i] && i < sizeof(pbuf) - 1) { pbuf[i] = path[i]; i++; }
    pbuf[i] = '\0';
    while (i > 1 && pbuf[i - 1] == '/') pbuf[--i] = '\0';

    char base[128];
    int  fd = nio_open_parent(pbuf, base, sizeof(base));
    if (fd < 0) return -1;
    cact_statx_arg_t a;
    a.name  = base;
    a.buf   = buf;
    a.flags = flags;
    int r = nio_ioctl(fd, CACT_DIRCTL_STATX, &a);
    nio_close(fd);
    return nio_map(r);
}
