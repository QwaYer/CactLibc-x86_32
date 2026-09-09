#ifndef FCNTL_H
#define FCNTL_H

/* Flags for open() */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_CLOEXEC   0x80000
#define O_EXCL      0x0080

/* fcntl() commands */
#define F_DUPFD         0   /* duplicate file descriptor (>= arg) */
#define F_GETFD         1   /* get file descriptor flags */
#define F_SETFD         2   /* set file descriptor flags */
#define F_GETFL         3   /* get file status flags */
#define F_SETFL         4   /* set file status flags */
#define F_DUPFD_CLOEXEC 1030 /* duplicate file descriptor (>= arg) + FD_CLOEXEC */

/* File descriptor flags (used with F_GETFD / F_SETFD) */
#define FD_CLOEXEC  1   /* close-on-exec */

int open(const char *pathname, int flags, ...);
int creat(const char *pathname, int mode);
int fcntl(int fd, int cmd, ...);

#endif
