#ifndef _WORDEXP_H
#define _WORDEXP_H

#include <stddef.h>

/* POSIX wordexp: the shell's expansions (parameter, command, arithmetic,
 * tilde, field splitting, pathname expansion, quote removal) applied to a
 * string, without running a command itself. */

typedef struct {
    size_t   we_wordc;      /* number of expanded words */
    char   **we_wordv;      /* NULL-terminated vector of words */
    size_t   we_offs;       /* reserved NULL slots before the words */
} wordexp_t;

#define WRDE_APPEND  0x01   /* append to a previous expansion */
#define WRDE_DOOFFS  0x02   /* leave we_offs NULLs at the front */
#define WRDE_NOCMD   0x04   /* fail (WRDE_CMDSUB) on command substitution */
#define WRDE_REUSE   0x08   /* free a previous result first */
#define WRDE_SHOWERR 0x10   /* let the command's stderr through (no-op here) */
#define WRDE_UNDEF   0x20   /* fail (WRDE_BADVAL) on an unset variable */

#define WRDE_NOSPACE 1
#define WRDE_BADCHAR 2
#define WRDE_BADVAL  3
#define WRDE_CMDSUB  4
#define WRDE_SYNTAX  5

int  wordexp(const char *words, wordexp_t *pwordexp, int flags);
void wordfree(wordexp_t *pwordexp);

#endif
