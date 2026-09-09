#ifndef UNISTD_H
#define UNISTD_H

#include <stddef.h>

typedef int      pid_t;
typedef int      ssize_t;
typedef int      off_t;
typedef unsigned uid_t;
typedef unsigned gid_t;
typedef unsigned mode_t;
typedef unsigned dev_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* access() mode bits */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

/* reboot() commands */
#define RB_AUTOBOOT  0x01234567
#define RB_HALT_SYSTEM 0xCDEF0123
#define RB_POWER_OFF 0x4321FEDC

/* ── жизненный цикл процесса ── */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
pid_t   fork(void);
int     execve(const char *pathname, char *const argv[], char *const envp[]);
pid_t   getpid(void);
pid_t   getppid(void);
void    _exit(int status);
pid_t   waitpid(pid_t pid, int *status, int options);
unsigned int sleep(unsigned int seconds);
int     usleep(unsigned int usec);

/* ── группы и сессии ── */
pid_t   setsid(void);
int     setpgid(pid_t pid, pid_t pgid);
pid_t   getpgid(pid_t pid);
pid_t   getpgrp(void);
pid_t   getsid(pid_t pid);

/* ── файловые дескрипторы ── */
off_t   lseek(int fd, off_t offset, int whence);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int     fdatasync(int fd);
int     ioctl(int fd, unsigned long cmd, void *arg);
int     dup(int oldfd);
int     dup2(int oldfd, int newfd);
int     dup3(int oldfd, int newfd, int flags);
int     pipe(int pipefd[2]);
int     pipe2(int pipefd[2], int flags);

/* ── память ── */
void   *sbrk(int increment);
int     brk(void *addr);

/* ── файловая система ── */
char   *getcwd(char *buf, int size);
int     chdir(const char *path);
int     chroot(const char *path);
int     mkdir(const char *pathname, mode_t mode);
int     rmdir(const char *pathname);
int     unlink(const char *pathname);
int     cact_create(const char *pathname);
int     cact_delete(const char *pathname);
int     access(const char *pathname, int mode);
int     truncate(const char *path, off_t length);
int     ftruncate(int fd, off_t length);
int     sync(void);
int     fsync(int fd);
mode_t  umask(mode_t mask);
int     mknod(const char *pathname, mode_t mode, dev_t dev);
int     symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int     link(const char *oldpath, const char *newpath);

/* ── системные операции ── */
int     mount(const char *src, const char *target, const char *fstype,
              unsigned long flags, const void *data);
int     umount(const char *target);
int     reboot(int cmd);

/* ── загрузка PCI-модулей ядра (только euid 0) ── */
/* vendor_id & device_id both (unsigned)-1: kernel reads cact_pci_* from the module ELF */
extern char **environ;

#define MODULE_LOAD_ID_AUTO  ((unsigned)0xFFFFFFFFu)
int     module_load(const char *path, unsigned vendor_id, unsigned device_id);
/* NULL: unload usermod slot; "NAME": driver name; decimal string: [pci N] index (see /dev/modinfo) */
int     module_unload(const char *target);

/* ── uname ── */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};
int uname(struct utsname *buf);

/* ── идентификация ── */
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     setuid(uid_t uid);
int     setgid(gid_t gid);

int     execvp(const char *file, char *const argv[]);

/* ── getopt (POSIX) ── */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int argc, char *const argv[], const char *optstring);

#endif

int isatty(int fd);
