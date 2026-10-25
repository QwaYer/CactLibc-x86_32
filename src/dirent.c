#include "dirent.h"
#include "nodeio.h"
#include "errno.h"
#include "fcntl.h"
#include "stat.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include <stdint.h>

int getdents(int fd, struct dirent *buf, unsigned int count) {
    cact_getdents_arg_t a;
    a.buf   = buf;
    a.count = count;
    return nio_map(nio_ioctl(fd, CACT_FDCTL_GETDENTS, &a));
}

/* ── directory streams ──────────────────────────────────────────────────── */

#define DIR_BUF_ENTRIES 32                  /* 32 * 128 = 4 KiB */

struct __dirstream {
    int           fd;
    char         *path;                     /* for rewinddir()/seekdir() */
    struct dirent buf[DIR_BUF_ENTRIES];
    int           n;                        /* entries in buf */
    int           i;                        /* next entry in buf */
    long          pos;                      /* entries consumed so far */
    int           eof;
};

DIR *opendir(const char *name) {
    if (!name) { errno = ENOENT; return 0; }

    int fd = open(name, O_RDONLY);
    if (fd < 0) return 0;                   /* open() already set errno */

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        close(fd);
        errno = ENOTDIR;
        return 0;
    }

    DIR *d = (DIR *)calloc(1, sizeof(*d));
    if (!d) { close(fd); errno = ENOMEM; return 0; }

    size_t len = strlen(name);
    d->path = (char *)malloc(len + 1);
    if (!d->path) { close(fd); free(d); errno = ENOMEM; return 0; }
    memcpy(d->path, name, len + 1);

    d->fd = fd;
    return d;
}

static void dir_reset(DIR *d) {
    d->n = d->i = 0;
    d->pos = 0;
    d->eof = 0;
}

/* Point the stream at entry number loc by re-opening the directory: no
 * filesystem here implements directory lseek, so entries have to be skipped. */
static int dir_goto(DIR *d, long loc) {
    int fd = open(d->path, O_RDONLY);
    if (fd < 0) return -1;
    close(d->fd);
    d->fd = fd;
    dir_reset(d);

    while (d->pos < loc) {
        int bytes = getdents(d->fd, d->buf, sizeof(d->buf));
        if (bytes <= 0) { d->eof = 1; return 0; }
        d->n = bytes / (int)sizeof(struct dirent);
        d->i = d->n;
        d->pos += d->n;
    }
    /* The last block may overshoot: rewind within it. */
    if (d->pos > loc) {
        d->i = d->n - (int)(d->pos - loc);
        d->pos = loc;
    }
    return 0;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) { errno = EBADF; return 0; }

    if (dirp->i >= dirp->n) {
        if (dirp->eof) return 0;
        int bytes = getdents(dirp->fd, dirp->buf, sizeof(dirp->buf));
        if (bytes < 0) return 0;
        if (bytes == 0) { dirp->eof = 1; return 0; }
        dirp->n = bytes / (int)sizeof(struct dirent);
        dirp->i = 0;
        if (dirp->n == 0) { dirp->eof = 1; return 0; }
    }
    dirp->pos++;
    return &dirp->buf[dirp->i++];
}

/* Deprecated reentrant form (POSIX.1-2008 removed it): the caller supplies the
 * struct dirent.  readdir() is itself thread-safe per stream, and the result
 * is a copy, so this is simply that copy; end of stream is 0 with *result
 * NULL, as the old contract requires. */
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {
    if (!dirp || !entry || !result) return EINVAL;
    struct dirent *de = readdir(dirp);
    if (!de) { *result = 0; return 0; }
    memcpy(entry, de, sizeof(*entry));
    *result = entry;
    return 0;
}

int closedir(DIR *dirp) {
    if (!dirp) { errno = EBADF; return -1; }
    int r = 0;
    if (dirp->fd >= 0) r = close(dirp->fd);
    free(dirp->path);
    free(dirp);
    return r;
}

void rewinddir(DIR *dirp) {
    if (!dirp) return;
    if (dir_goto(dirp, 0) != 0) dir_reset(dirp);
}

long telldir(DIR *dirp) {
    return dirp ? dirp->pos : -1;
}

void seekdir(DIR *dirp, long loc) {
    if (!dirp || loc < 0) return;
    if (dirp->pos == loc) return;
    /* Both directions take the same re-open: the stream only runs forward in
     * the kernel.  A failure leaves the stream where it was. */
    if (dir_goto(dirp, loc) != 0) errno = EBADF;
}

int dirfd(DIR *dirp) {
    if (!dirp) { errno = EBADF; return -1; }
    return dirp->fd;
}

/* ── scandir ────────────────────────────────────────────────────────────── */

static int dirent_name_cmp(const struct dirent **a, const struct dirent **b) {
    return strcoll((*a)->d_name, (*b)->d_name);
}

int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
    DIR *d = opendir(dirp);
    if (!d) return -1;

    struct dirent **list = 0;
    int count = 0;
    struct dirent *de;

    while ((de = readdir(d)) != 0) {
        if (filter && !filter(de)) continue;

        struct dirent **grown =
            (struct dirent **)realloc(list, (size_t)(count + 1) * sizeof(*list));
        if (!grown) {
            for (int i = 0; i < count; i++) free(list[i]);
            free(list);
            closedir(d);
            errno = ENOMEM;
            return -1;
        }
        list = grown;

        list[count] = (struct dirent *)malloc(sizeof(struct dirent));
        if (!list[count]) {
            for (int i = 0; i < count; i++) free(list[i]);
            free(list);
            closedir(d);
            errno = ENOMEM;
            return -1;
        }
        memcpy(list[count], de, sizeof(struct dirent));
        count++;
    }
    closedir(d);

    qsort(list, (size_t)count, sizeof(*list),
          (int (*)(const void *, const void *))(compar ? compar : dirent_name_cmp));
    *namelist = list;
    return count;
}

int alphasort(const struct dirent **a, const struct dirent **b) {
    return strcoll((*a)->d_name, (*b)->d_name);
}
