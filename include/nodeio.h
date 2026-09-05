#ifndef NODEIO_H
#define NODEIO_H

#include <stdint.h>
#include <stddef.h>
#include "unistd.h"
#include "ioctl_abi.h"

/* Internal relay layer: POSIX-ишные сервисы поверх VFS-узлов.
 * Все функции возвращают «сырой» результат ядра: >=0 успех,
 * <0 — -errno (или -1 для ошибок старых трапов open/...). */

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

/* open+read+close короткого узла (path), возвращает прочитано байт */
int nio_read_file(const char *path, void *buf, size_t size);

/* открыть /dev/<name>, ioctl, закрыть */
int nio_dev_cmd(const char *dev, unsigned long cmd, void *arg);

/* Разбить путь на каталог+базу, открыть каталог (или ".").
 * Возвращает fd каталога (>=0) или -1. base заполняется одним компонентом. */
int nio_open_parent(const char *path, char *base, size_t base_max);

/* Преобразовать результат ядра в POSIX-соглашение: -1 + errno. */
int nio_map(int r);

#endif
