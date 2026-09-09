#include "unistd.h"
#include "syscall.h"
#include "errno.h"
#include "nodeio.h"
#include "fcntl.h"
#include "select.h"
#include "poll.h"
#include "signal.h"
#include "uio.h"
#include <stdint.h>

/* ── жизненный цикл процесса (core traps) ── */

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall(SYS_READ, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall(SYS_WRITE, (uintptr_t)fd, (uintptr_t)buf, (uintptr_t)count);
}

int close(int fd) {
    return (int)syscall(SYS_CLOSE, (uintptr_t)fd, 0, 0);
}

pid_t fork(void) {
    return (pid_t)syscall(SYS_FORK, 0, 0, 0);
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    return (int)syscall(SYS_EXEC, (uintptr_t)pathname, (uintptr_t)argv, (uintptr_t)envp);
}

void _exit(int status) {
    syscall(SYS_EXIT, (uintptr_t)status, 0, 0);
    for (;;) {}
}

pid_t waitpid(pid_t pid, int *status, int options) {
    (void)options;
    return (pid_t)syscall(SYS_WAITPID, (uintptr_t)pid, (uintptr_t)status, 0);
}

/* sleep: poll(NULL, 0, ms) — poll is the core ready/timeout primitive. */
unsigned int sleep(unsigned int seconds) {
    __syscall3(SYS_POLL, 0, 0, (uintptr_t)(seconds * 1000));
    return 0;
}

int usleep(unsigned int usec) {
    unsigned int ms = (usec + 999) / 1000;
    if (ms == 0) ms = 1;
    __syscall3(SYS_POLL, 0, 0, (uintptr_t)ms);
    return 0;
}

/* ── идентификация через /proc/self/info ── */

static int _self_field(uint32_t which, uint32_t *out) {
    cact_proc_info_t info;
    if (nio_self_info(&info) != (int)sizeof(info)) return -1;
    switch (which) {
    case 0:  *out = info.pid;  break;
    case 1:  *out = info.ppid; break;
    case 2:  *out = info.pgid; break;
    case 3:  *out = info.sid;  break;
    case 4:  *out = info.uid;  break;
    case 5:  *out = info.gid;  break;
    case 6:  *out = info.euid; break;
    case 7:  *out = info.egid; break;
    case 8:  *out = info.umask; break;
    default: return -1;
    }
    return 0;
}

pid_t getpid(void) {
    uint32_t v = 0;
    _self_field(0, &v);
    return (pid_t)v;
}

pid_t getppid(void) {
    uint32_t v = 0;
    _self_field(1, &v);
    return (pid_t)v;
}

uid_t getuid(void)  { uint32_t v = 0; _self_field(4, &v); return v; }
gid_t getgid(void)  { uint32_t v = 0; _self_field(5, &v); return v; }
uid_t geteuid(void) { uint32_t v = 0; _self_field(6, &v); return v; }
gid_t getegid(void) { uint32_t v = 0; _self_field(7, &v); return v; }

pid_t getpgrp(void) {
    uint32_t v = 0;
    _self_field(2, &v);
    return (pid_t)v;
}

static int _proc_info(pid_t pid, cact_proc_info_t *info) {
    if (pid == 0) pid = getpid();
    if (pid == getpid())
        return (nio_self_info(info) == (int)sizeof(*info)) ? 0 : -1;

    char path[32];
    int n = 0;
    {
        char nb[12];
        int m = 0;
        unsigned up = (unsigned)pid;
        if (up == 0) nb[m++] = '0';
        while (up) { nb[m++] = '0' + (up % 10); up /= 10; }
        for (int k = 0; k < m / 2; k++) {
            char t = nb[k];
            nb[k] = nb[m - 1 - k];
            nb[m - 1 - k] = t;
        }
        nb[m] = '\0';
        const char *pfx = "/proc/";
        while (*pfx && n < (int)sizeof(path) - 1) path[n++] = *pfx++;
        for (int k = 0; nb[k] && n < (int)sizeof(path) - 1; k++)
            path[n++] = nb[k];
        const char *sfx = "/info";
        while (*sfx && n < (int)sizeof(path) - 1) path[n++] = *sfx++;
        path[n] = '\0';
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { errno = ESRCH; return -1; }
    ssize_t r = read(fd, info, sizeof(*info));
    close(fd);
    if (r != (ssize_t)sizeof(*info)) { errno = ESRCH; return -1; }
    return 0;
}

pid_t getpgid(pid_t pid) {
    cact_proc_info_t info;
    if (_proc_info(pid, &info) < 0) return -1;
    return (pid_t)info.pgid;
}

pid_t getsid(pid_t pid) {
    cact_proc_info_t info;
    if (_proc_info(pid, &info) < 0) return -1;
    return (pid_t)info.sid;
}

/* ── сессии / контроль через /proc/self/ctl ── */

pid_t setsid(void) {
    return (pid_t)nio_map(nio_ctl(CACT_PROCCTL_SETSID, 0));
}

int setpgid(pid_t pid, pid_t pgid) {
    cact_pgid_arg_t a;
    a.pid  = (uint32_t)pid;
    a.pgid = (uint32_t)pgid;
    return nio_map(nio_ctl(CACT_PROCCTL_SETPGID, &a));
}

int setuid(uid_t uid) {
    return nio_map(nio_ctl(CACT_PROCCTL_SETUID, &uid));
}

int setgid(gid_t gid) {
    return nio_map(nio_ctl(CACT_PROCCTL_SETGID, &gid));
}

mode_t umask(mode_t mask) {
    return (mode_t)(unsigned)nio_ctl(CACT_PROCCTL_UMASK, &mask);
}

int chdir(const char *path) {
    return nio_map(nio_ctl(CACT_PROCCTL_CHDIR, (void *)path));
}

int chroot(const char *path) {
    return nio_map(nio_ctl(CACT_PROCCTL_CHROOT, (void *)path));
}

/* ── fd-level операции через FDCTL_* ── */

off_t lseek(int fd, off_t offset, int whence) {
    cact_lseek_arg_t a;
    a.offset = (int32_t)offset;
    a.whence = (uint32_t)whence;
    return (off_t)nio_map(nio_ioctl(fd, CACT_FDCTL_LSEEK, &a));
}

int ioctl(int fd, unsigned long cmd, void *arg) {
    return (int)syscall(SYS_IOCTL, (uintptr_t)fd, (uintptr_t)cmd, (uintptr_t)arg);
}

int dup(int oldfd) {
    return nio_map(nio_ioctl(oldfd, CACT_FDCTL_DUP, 0));
}

int dup2(int oldfd, int newfd) {
    cact_fd_arg_t a;
    a.newfd = (uint32_t)newfd;
    return nio_map(nio_ioctl(oldfd, CACT_FDCTL_DUP2, &a));
}

int dup3(int oldfd, int newfd, int flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) { errno = EINVAL; return -1; }
    int r = dup2(oldfd, newfd);
    if (r < 0) return -1;
    if (flags & O_CLOEXEC) {
        if (fcntl(newfd, F_SETFD, FD_CLOEXEC) < 0) return -1;
    }
    return newfd;
}

int pipe(int pipefd[2]) {
    uint32_t fds[2];
    int r = nio_dev_cmd("pipe", CACT_PIPECTL_CREATE, fds);
    if (r < 0) return nio_map(r);
    pipefd[0] = (int)fds[0];
    pipefd[1] = (int)fds[1];
    return 0;
}

int pipe2(int pipefd[2], int flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) { errno = EINVAL; return -1; }
    if (pipe(pipefd) < 0) return -1;
    if (flags & O_CLOEXEC) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & O_NONBLOCK) {
        int fl0 = fcntl(pipefd[0], F_GETFL, 0);
        int fl1 = fcntl(pipefd[1], F_GETFL, 0);
        if (fl0 >= 0) fcntl(pipefd[0], F_SETFL, fl0 | O_NONBLOCK);
        if (fl1 >= 0) fcntl(pipefd[1], F_SETFL, fl1 | O_NONBLOCK);
    }
    return 0;
}

int poll(struct pollfd *fds, int nfds, int timeout_ms) {
    return (int)__syscall3(SYS_POLL, (uintptr_t)fds, (uintptr_t)nfds,
                           (uintptr_t)timeout_ms);
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
    struct pollfd pfd[FD_SETSIZE];
    int           n = 0;

    for (int fd = 0; fd < nfds; fd++) {
        short ev = 0;
        if (readfds  && FD_ISSET(fd, readfds))  ev |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds)) ev |= POLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds)) ev |= POLLERR;
        if (!ev) continue;
        pfd[n].fd     = fd;
        pfd[n].events = ev;
        pfd[n].revents = 0;
        n++;
    }

    int ms;
    if (!timeout)      ms = -1;
    else if (timeout->tv_sec == 0 && timeout->tv_usec == 0) ms = 0;
    else               ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);

    int r = poll(pfd, n, ms);
    if (r < 0) return -1;
    if (readfds)   FD_ZERO(readfds);
    if (writefds)  FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (pfd[i].revents == 0) continue;
        count++;
        if (readfds   && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
            FD_SET(pfd[i].fd, readfds);
        if (writefds  && (pfd[i].revents & (POLLOUT | POLLERR)))
            FD_SET(pfd[i].fd, writefds);
        if (exceptfds && (pfd[i].revents & POLLERR))
            FD_SET(pfd[i].fd, exceptfds);
    }
    return count;
}

/* ── память (core traps) ── */

void *sbrk(int increment) {
    uintptr_t cur = (uintptr_t)syscall(SYS_BRK, 0, 0, 0);
    if (cur == UINTPTR_MAX) return (void*)-1;
    if (increment == 0) return (void*)cur;
    uintptr_t req = cur + (uintptr_t)increment;
    if (req < cur) return (void*)-1;
    uintptr_t new_brk = (uintptr_t)syscall(SYS_BRK, req, 0, 0);
    if (new_brk == UINTPTR_MAX) return (void*)-1;
    return (void*)cur;
}

int brk(void *addr) {
    uintptr_t ret = (uintptr_t)syscall(SYS_BRK, (uintptr_t)addr, 0, 0);
    return (ret == UINTPTR_MAX) ? -1 : 0;
}

/* ── файловая система: DIRCTL_* по открытому каталогу ── */

char *getcwd(char *buf, int size) {
    char tmp[512];
    int  r = nio_read_file("/proc/self/cwd", tmp, sizeof(tmp));
    if (r < 0) return 0;
    if (r >= size) { errno = ERANGE; return 0; }
    for (int i = 0; i < r; i++) buf[i] = tmp[i];
    buf[r] = '\0';
    return buf;
}

int mkdir(const char *pathname, mode_t mode) {
    (void)mode;
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    int r = nio_ioctl(fd, CACT_DIRCTL_MKDIR, base);
    nio_close(fd);
    return nio_map(r);
}

int rmdir(const char *pathname) {
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    int r = nio_ioctl(fd, CACT_DIRCTL_RMDIR, base);
    nio_close(fd);
    return nio_map(r);
}

int unlink(const char *pathname) {
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    int r = nio_ioctl(fd, CACT_DIRCTL_UNLINK, base);
    nio_close(fd);
    return nio_map(r);
}

int cact_create(const char *pathname) {
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    cact_openat_arg_t a;
    a.name  = base;
    a.flags = 0;   /* O_RDONLY */
    int r = nio_ioctl(fd, CACT_DIRCTL_CREATE, &a);
    nio_close(fd);
    return nio_map(r);
}

int cact_delete(const char *pathname) {
    return unlink(pathname);
}

int access(const char *pathname, int mode) {
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    cact_access_arg_t a;
    a.name = base;
    a.mode = (uint32_t)mode;
    int r = nio_ioctl(fd, CACT_DIRCTL_ACCESS, &a);
    nio_close(fd);
    return nio_map(r);
}

int truncate(const char *path, off_t length) {
    char base[128];
    int  fd = nio_open_parent(path, base, sizeof(base));
    if (fd < 0) return -1;
    cact_truncate_arg_t a;
    a.name   = base;
    a.length = (uint32_t)length;
    int r = nio_ioctl(fd, CACT_DIRCTL_TRUNCATE, &a);
    nio_close(fd);
    return nio_map(r);
}

int ftruncate(int fd, off_t length) {
    uint32_t len = (uint32_t)length;
    return nio_map(nio_ioctl(fd, CACT_FDCTL_FTRUNCATE, &len));
}

int sync(void) { return 0; }

int fsync(int fd) {
    return nio_map(nio_ioctl(fd, CACT_FDCTL_FSYNC, 0));
}

int fdatasync(int fd) {
    return fsync(fd);
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t r = read(fd, buf, count);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t r = write(fd, buf, count);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if (r < (ssize_t)iov[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t r = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if (r < (ssize_t)iov[i].iov_len) break;
    }
    return total;
}

int mknod(const char *pathname, mode_t mode, dev_t dev) {
    char base[128];
    int  fd = nio_open_parent(pathname, base, sizeof(base));
    if (fd < 0) return -1;
    cact_mknod_arg_t a;
    a.name = base;
    a.mode = (uint32_t)mode;
    a.dev  = (uint32_t)dev;
    int r = nio_ioctl(fd, CACT_DIRCTL_MKNOD, &a);
    nio_close(fd);
    return nio_map(r);
}

int symlink(const char *target, const char *linkpath) {
    char base[128];
    int  fd = nio_open_parent(linkpath, base, sizeof(base));
    if (fd < 0) return -1;
    cact_symlink_arg_t a;
    a.target   = (char *)target;
    a.linkname = base;
    int r = nio_ioctl(fd, CACT_DIRCTL_SYMLINK, &a);
    nio_close(fd);
    return nio_map(r);
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    char base[128];
    int  fd = nio_open_parent(path, base, sizeof(base));
    if (fd < 0) return -1;
    cact_readlink_arg_t a;
    a.name = base;
    a.buf  = buf;
    a.len  = (uint32_t)bufsiz;
    int r = nio_ioctl(fd, CACT_DIRCTL_READLINK, &a);
    nio_close(fd);
    return (ssize_t)nio_map(r);
}

int link(const char *oldpath, const char *newpath) {
    /* DIRCTL hard-link работает в пределах одного каталога */
    char obase[128], nbase[128];
    int  fd = nio_open_parent(oldpath, obase, sizeof(obase));
    if (fd < 0) return -1;
    cact_link_arg_t a;
    a.target  = obase;
    a.newname = nbase;
    /* имя нового файла берём из newpath */
    const char *slash = 0;
    for (const char *s = newpath; *s; s++)
        if (*s == '/') slash = s;
    const char *b = slash ? slash + 1 : newpath;
    size_t n = 0;
    while (b[n] && n + 1 < sizeof(nbase)) { nbase[n] = b[n]; n++; }
    nbase[n] = '\0';
    if (!nbase[0]) { nio_close(fd); errno = EINVAL; return -1; }
    int r = nio_ioctl(fd, CACT_DIRCTL_LINK, &a);
    nio_close(fd);
    return nio_map(r);
}

/* ── системные операции через /dev/sys ── */

int mount(const char *src, const char *target, const char *fstype,
          unsigned long flags, const void *data) {
    (void)flags; (void)data;
    cact_mount_arg_t a;
    a.src    = (char *)src;
    a.target = (char *)target;
    a.fstype = (char *)fstype;
    return nio_map(nio_dev_cmd("sys", CACT_SYSCTL_MOUNT, &a));
}

int umount(const char *target) {
    return nio_map(nio_dev_cmd("sys", CACT_SYSCTL_UMOUNT, (void *)target));
}

int reboot(int cmd) {
    uint32_t c = (uint32_t)cmd;
    return nio_map(nio_dev_cmd("sys", CACT_SYSCTL_REBOOT, &c));
}

int module_load(const char *path, unsigned vendor_id, unsigned device_id) {
    cact_module_arg_t a;
    a.path      = (char *)path;
    a.vendor_id = vendor_id;
    a.device_id = device_id;
    return nio_map(nio_dev_cmd("sys", CACT_SYSCTL_MODULE_LOAD, &a));
}

int module_unload(const char *target) {
    return nio_map(nio_dev_cmd("sys", CACT_SYSCTL_MODULE_UNLOAD, (void *)target));
}

int uname(struct utsname *buf) {
    cact_uname_t u;
    int r = nio_read_file("/proc/uname", &u, sizeof(u));
    if (r != (int)sizeof(u)) return -1;
    int i;
    for (i = 0; i < 65; i++) buf->sysname[i]  = u.sysname[i];
    for (i = 0; i < 65; i++) buf->nodename[i] = u.nodename[i];
    for (i = 0; i < 65; i++) buf->release[i]  = u.release[i];
    for (i = 0; i < 65; i++) buf->version[i]  = u.version[i];
    for (i = 0; i < 65; i++) buf->machine[i]  = u.machine[i];
    return 0;
}

int execvp(const char *file, char *const argv[]) {
    return execve(file, argv, environ);
}

int isatty(int fd) {
    /* TIOCGWINSZ is answered by the kernel only for terminal nodes. */
    struct winsize ws;
    struct winsize *p = &ws;
    int r = nio_ioctl(fd, TIOCGWINSZ, p);
    return (r >= 0) ? 1 : 0;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts,
          const sigset_t *sigmask) {
    (void)sigmask;
    int ms = -1;
    if (timeout_ts) {
        long long m = timeout_ts->tv_sec * 1000LL +
                      timeout_ts->tv_nsec / 1000000LL;
        if (m < 0) m = 0;
        if (m > 2147483647LL) m = 2147483647LL;
        ms = (int)m;
    }
    return poll(fds, (int)nfds, ms);
}
