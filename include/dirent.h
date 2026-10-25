#ifndef DIRENT_H
#define DIRENT_H

#include <stdint.h>

/* The layout the kernel's CACT_FDCTL_GETDENTS copies — keep it byte-identical
 * to struct cact_dirent (128 bytes, no d_type). */
struct dirent {
    uint32_t d_ino;
    char     d_name[124];
};

int getdents(int fd, struct dirent *buf, unsigned int count);

/* ── directory streams ──────────────────────────────────────────────────────
 * readdir() is built on getdents(), which the kernel keeps as an index into
 * the directory (it advances the fd's offset by the entries it returns).
 * Directory lseek is not implemented by any filesystem here, so rewinddir()
 * and seekdir() re-open the path instead of seeking. */

typedef struct __dirstream DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);
long           telldir(DIR *dirp);
void           seekdir(DIR *dirp, long loc);
int            dirfd(DIR *dirp);

/* Filter/compare for scandir(); compar is passed a pair of struct dirent *. */
int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **a, const struct dirent **b);

#endif
