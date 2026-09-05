#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
#include "fcntl.h"
#include <stdint.h>

/* ── core traps ────────────────────────────────────────────────────────── */

int nio_open(const char *path, int flags) {
    return (int)syscall(SYS_OPEN, (uintptr_t)path, (uintptr_t)flags, 0);
}

int nio_close(int fd) {
    return (int)syscall(SYS_CLOSE, (uintptr_t)fd, 0, 0);
}

ssize_t nio_read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall(SYS_READ, (uintptr_t)fd, (uintptr_t)buf,
                            (uintptr_t)count);
}

ssize_t nio_write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall(SYS_WRITE, (uintptr_t)fd, (uintptr_t)buf,
                            (uintptr_t)count);
}

int nio_ioctl(int fd, unsigned long cmd, void *arg) {
    return (int)syscall(SYS_IOCTL, (uintptr_t)fd, (uintptr_t)cmd, (uintptr_t)arg);
}

/* ── /proc/self/ctl ────────────────────────────────────────────────────── */

int nio_ctl(unsigned long cmd, void *arg) {
    int fd = nio_open("/proc/self/ctl", O_RDWR);
    if (fd < 0) return -1;
    int r = nio_ioctl(fd, cmd, arg);
    nio_close(fd);
    return r;
}

/* ── /proc/self/info ───────────────────────────────────────────────────── */

int nio_self_info(cact_proc_info_t *info) {
    return nio_read_file("/proc/self/info", info, sizeof(*info));
}

/* ── short node read helper ────────────────────────────────────────────── */

int nio_read_file(const char *path, void *buf, size_t size) {
    int fd = nio_open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = nio_read(fd, buf, size);
    nio_close(fd);
    return (r < 0) ? (int)r : (int)r;
}

/* ── /dev/<name> ioctl helper ──────────────────────────────────────────── */

int nio_dev_cmd(const char *dev, unsigned long cmd, void *arg) {
    char path[32];
    int  p = 0;
    path[p++] = '/';
    path[p++] = 'd';
    path[p++] = 'e';
    path[p++] = 'v';
    path[p++] = '/';
    for (int i = 0; dev[i] && p < (int)sizeof(path) - 1; i++)
        path[p++] = dev[i];
    path[p] = '\0';

    int fd = nio_open(path, O_RDWR);
    if (fd < 0) return -1;
    int r = nio_ioctl(fd, cmd, arg);
    nio_close(fd);
    return r;
}

/* ── path → parent dir + base name ─────────────────────────────────────── */

int nio_open_parent(const char *path, char *base, size_t base_max) {
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }

    /* найти последний '/' */
    const char *slash = 0;
    for (const char *s = path; *s; s++)
        if (*s == '/') slash = s;

    if (!slash) {
        /* один компонент относительно cwd: открываем "." */
        size_t n = 0;
        while (path[n] && n + 1 < base_max) { base[n] = path[n]; n++; }
        base[n] = '\0';
        return nio_open(".", O_RDONLY);
    }

    /* имя базы — всё после последнего '/' */
    const char *b = slash + 1;
    if (!b[0]) {           /* trailing slash — базы нет */
        errno = EINVAL;
        return -1;
    }
    size_t n = 0;
    while (b[n] && n + 1 < base_max) { base[n] = b[n]; n++; }
    base[n] = '\0';

    /* каталог — всё до последнего '/' (включая его) */
    size_t dlen = (size_t)(slash - path) + 1;
    char  dirbuf[512];
    if (dlen >= sizeof(dirbuf)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (size_t i = 0; i < dlen - 1; i++)
        dirbuf[i] = path[i];
    dirbuf[dlen - 1] = '\0';
    if (dirbuf[0] == '\0') { dirbuf[0] = '/'; dirbuf[1] = '\0'; }

    return nio_open(dirbuf, O_RDONLY);
}

/* ── errno mapping ─────────────────────────────────────────────────────── */

int nio_map(int r) {
    if (r >= 0) return r;
    if (r == -1) {
        errno = EIO;
    } else {
        int e = -r;
        errno = (e > 0 && e <= 133) ? e : EIO;
    }
    return -1;
}
