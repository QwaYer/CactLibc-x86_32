#ifndef _FNMATCH_H
#define _FNMATCH_H

/* Flags for fnmatch(). */
#define FNM_NOMATCH   1
#define FNM_PATHNAME  0x01     /* wildcards do not match '/' */
#define FNM_NOESCAPE  0x02     /* backslash is an ordinary character */
#define FNM_PERIOD    0x04     /* a leading '.' must be matched explicitly */
#define FNM_FILE_NAME FNM_PATHNAME
#define FNM_LEADING_DIR 0x08   /* ignore a trailing "/..." in string */
#define FNM_CASEFOLD  0x10     /* case-insensitive (GNU extension) */

int fnmatch(const char *pattern, const char *string, int flags);

#endif
