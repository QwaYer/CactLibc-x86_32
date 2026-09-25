#ifndef NODEIO_H
#define NODEIO_H

#include <stdint.h>
#include <stddef.h>
#include "unistd.h"
#include "ioctl_abi.h"

/* Internal relay layer: POSIX-ish services on top of VFS nodes.
 * All functions return the "raw" kernel result: >=0 success,
 * <0 — -errno (or -1 for errors from the old open/... traps). */

/* Core traps */
int     nio_open(const char *path, int flags);
int     nio_close(int fd);
ssize_t nio_read(int fd, void *buf, size_t count);
ssize_t nio_write(int fd, const void *buf, size_t count);
int     nio_ioctl(int fd, unsigned long cmd, void *arg);

/* /proc/self/ctl */
int nio_ctl(unsigned long cmd, void *arg);

/* /proc/self/info → struct cact_proc_info_t */
int nio_self_info(cact_proc_info_t *info);

/* open+read+close a short node (path), returns bytes read */
int nio_read_file(const char *path, void *buf, size_t size);

/* open /dev/<name>, ioctl, close */
int nio_dev_cmd(const char *dev, unsigned long cmd, void *arg);

/* Split a path into directory+base, open the directory (or ".").
 * Returns the directory fd (>=0) or -1. base is filled with a single component. */
int nio_open_parent(const char *path, char *base, size_t base_max);

/* Convert a kernel result to the POSIX convention: -1 + errno. */
int nio_map(int r);

#endif
