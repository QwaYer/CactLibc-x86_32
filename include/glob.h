#ifndef _GLOB_H
#define _GLOB_H

#include <stddef.h>

typedef struct {
    size_t   gl_pathc;      /* number of matched paths */
    char   **gl_pathv;      /* NULL-terminated list of paths */
    size_t   gl_offs;       /* reserved slots at the front of gl_pathv */
} glob_t;

#define GLOB_ERR      0x0001    /* call errfunc on a directory read error */
#define GLOB_MARK     0x0002    /* append '/' to a matched directory */
#define GLOB_NOSORT   0x0004    /* do not sort the results */
#define GLOB_DOOFFS   0x0008    /* leave gl_offs NULLs at the front of gl_pathv */
#define GLOB_NOCHECK  0x0010    /* no match: return the pattern itself */
#define GLOB_APPEND   0x0020    /* append to the previous results */
#define GLOB_NOESCAPE 0x0040    /* backslash is an ordinary character */
#define GLOB_PERIOD   0x0080    /* a wildcard may match a leading '.' */
#define GLOB_TILDE    0x0100    /* expand a leading ~ (and ~/ to $HOME) */

#define GLOB_NOSPACE  1
#define GLOB_ABORTED  2
#define GLOB_NOMATCH  3
#define GLOB_NOSYS    4

int  glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob);
void globfree(glob_t *pglob);

#endif
