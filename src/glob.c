#include "glob.h"
#include "fnmatch.h"
#include "dirent.h"
#include "stat.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"

/* Component-wise walk: split the pattern on '/', expand one component at a
 * time (a literal component has to exist; a wildcard component is matched
 * against the directory's entries), and carry the surviving paths along. */

struct pathlist {
    char  **v;
    size_t  n;
    size_t  cap;
};

static int list_add(struct pathlist *l, const char *s) {
    if (l->n + 1 >= l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 16;
        char **v = (char **)realloc(l->v, cap * sizeof(*v));
        if (!v) return -1;
        l->v = v;
        l->cap = cap;
    }
    char *copy = (char *)malloc(strlen(s) + 1);
    if (!copy) return -1;
    strcpy(copy, s);
    l->v[l->n++] = copy;
    l->v[l->n] = 0;
    return 0;
}

static void list_free(struct pathlist *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    l->v = 0;
    l->n = l->cap = 0;
}

static int has_magic(const char *s, int flags) {
    for (; *s; s++) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
        if (*s == '\\' && !(flags & GLOB_NOESCAPE)) {
            if (s[1]) s++;
            else return 1;              /* trailing backslash */
        }
    }
    return 0;
}

/* Join dir + "/" + name (dir may be "" or "/"). */
static char *join(const char *dir, const char *name) {
    size_t dl = strlen(dir);
    int slash = !(dl == 1 && dir[0] == '/') && dl > 0;
    size_t n = dl + (slash ? 1 : 0) + strlen(name) + 1;
    char *p = (char *)malloc(n);
    if (!p) return 0;
    size_t k = 0;
    for (size_t i = 0; i < dl; i++) p[k++] = dir[i];
    if (slash) p[k++] = '/';
    for (const char *q = name; *q; q++) p[k++] = *q;
    p[k] = '\0';
    return p;
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Expand one component into `out` from every path in `in`.  `last` says this is
 * the final component, which matters only for error reporting — glibc calls
 * errfunc for an *intermediate* directory it cannot resolve but not for a final
 * name that simply does not match.  Glibc's rule, which this follows: errfunc is
 * called on any such failure, and a non-zero return from it, or GLOB_ERR, makes
 * glob() give up with GLOB_ABORTED.  Returns 0, GLOB_ABORTED, or -1 (out of
 * memory). */
static int expand_component(struct pathlist *in, struct pathlist *out,
                            const char *comp, int flags, int last,
                            int (*errfunc)(const char *, int)) {
    int magic = has_magic(comp, flags);

    for (size_t i = 0; i < in->n; i++) {
        const char *base = in->v[i];

        if (!magic) {                   /* a literal component must exist */
            /* Escapes are removed first: a literal component with no real magic
             * (`q\*star`) names the file `q*star`, not one containing the
             * backslash.  With GLOB_NOESCAPE the backslash is ordinary. */
            char lit[256];
            size_t li = 0;
            if (!(flags & GLOB_NOESCAPE)) {
                const char *cs;
                for (cs = comp; *cs && li + 1 < sizeof lit; cs++) {
                    if (*cs == '\\' && cs[1]) cs++;
                    lit[li++] = *cs;
                }
                lit[li] = '\0';
            } else {
                memcpy(lit, comp, strlen(comp) + 1);
            }

            char *p = join(base, lit);
            if (!p) return -1;
            struct stat st;
            if (stat(p, &st) == 0) {
                if (list_add(out, p) != 0) { free(p); return -1; }
            } else if (!last) {
                int e = errno;
                int stop = errfunc ? errfunc(p, e) : 0;
                free(p);
                if (stop != 0 || (flags & GLOB_ERR)) return GLOB_ABORTED;
                continue;
            }
            free(p);
            continue;
        }

        DIR *d = opendir(*base ? base : ".");
        if (!d) {
            int e = errno;
            int stop = errfunc ? errfunc(base, e) : 0;
            if (stop != 0 || (flags & GLOB_ERR)) return GLOB_ABORTED;
            continue;
        }
        struct dirent *de;
        while ((de = readdir(d)) != 0) {
            int fnflags = (flags & GLOB_PERIOD) ? 0 : FNM_PERIOD;
            if (flags & GLOB_NOESCAPE) fnflags |= FNM_NOESCAPE;
            if (fnmatch(comp, de->d_name, fnflags) != 0) continue;

            char *p = join(base, de->d_name);
            if (!p) { closedir(d); return -1; }
            if (list_add(out, p) != 0) { free(p); closedir(d); return -1; }
            free(p);
        }
        closedir(d);
    }
    return 0;
}

static int sort_cmp(const void *a, const void *b) {
    return strcoll(*(const char *const *)a, *(const char *const *)b);
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob) {
    if (!pattern || !pglob) return GLOB_NOMATCH;

    char *pat = 0;
    if ((flags & GLOB_TILDE) && pattern[0] == '~' &&
        (pattern[1] == '/' || pattern[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            pat = (char *)malloc(strlen(home) + strlen(pattern + 1) + 1);
            if (!pat) return GLOB_NOSPACE;
            strcpy(pat, home);
            strcat(pat, pattern + 1);
        }
    }
    if (!pat) {
        pat = (char *)malloc(strlen(pattern) + 1);
        if (!pat) return GLOB_NOSPACE;
        strcpy(pat, pattern);
    }

    struct pathlist cur = { 0, 0, 0 }, next = { 0, 0, 0 };
    int dir_only = 0;

    /* A pattern ending in '/' matches directories only, and the '/' stays in
     * the result. */
    {
        size_t plen = strlen(pat);
        if (plen > 1 && pat[plen - 1] == '/') dir_only = 1;
    }

    /* Is the pattern absolute? */
    char *work = pat;
    if (work[0] == '/') {
        if (list_add(&cur, "/") != 0) goto nospace;
        work++;
    } else if (list_add(&cur, "") != 0) {
        goto nospace;
    }

    while (*work) {
        char *slash = strchr(work, '/');
        size_t len = slash ? (size_t)(slash - work) : strlen(work);

        if (len == 0) {                 /* "//" or a trailing '/' */
            dir_only = (slash && slash[1] == '\0');
            work = slash + 1;
            continue;
        }

        char comp[256];
        if (len >= sizeof(comp)) { free(pat); list_free(&cur); return GLOB_NOMATCH; }
        memcpy(comp, work, len);
        comp[len] = '\0';

        /* The component is final when no slash follows it (a trailing slash is
         * not one). */
        int last = !slash || slash[1] == '\0';
        int r = expand_component(&cur, &next, comp, flags, last, errfunc);
        if (r == GLOB_ABORTED) goto aborted;
        if (r != 0) goto nospace;
        list_free(&cur);
        cur = next;
        next.v = 0; next.n = next.cap = 0;

        if (cur.n == 0 && !(flags & GLOB_NOCHECK)) {
            free(pat);
            return GLOB_NOMATCH;
        }
        work = slash ? slash + 1 : work + len;
    }

    /* A trailing '/' matches directories only, and it is kept in the result
     * (as is a directory with GLOB_MARK). */
    if (dir_only || (flags & GLOB_MARK)) {
        struct pathlist kept = { 0, 0, 0 };
        int ok = 1;
        for (size_t i = 0; i < cur.n && ok; i++) {
            int isd = is_dir(cur.v[i]);
            if (dir_only && !isd) continue;

            if (isd && (dir_only || (flags & GLOB_MARK))) {
                size_t l = strlen(cur.v[i]);
                if (l > 0 && cur.v[i][l - 1] == '/') {          /* already ends in '/' */
                    if (list_add(&kept, cur.v[i]) != 0) ok = 0;
                    continue;
                }
                char *p = (char *)malloc(l + 2);
                if (!p) { ok = 0; break; }
                strcpy(p, cur.v[i]);
                strcat(p, "/");
                if (list_add(&kept, p) != 0) ok = 0;
                free(p);
            } else if (list_add(&kept, cur.v[i]) != 0) {
                ok = 0;
            }
        }
        if (!ok) { list_free(&kept); goto nospace; }
        list_free(&cur);
        cur = kept;
    }

    if (cur.n == 0) {                   /* GLOB_NOCHECK with no match */
        if (list_add(&cur, pattern) != 0) goto nospace;
    }

    if (!(flags & GLOB_NOSORT))
        qsort(cur.v, cur.n, sizeof(*cur.v), sort_cmp);

    size_t offs = (flags & GLOB_DOOFFS) ? pglob->gl_offs : 0;
    size_t old  = (flags & GLOB_APPEND) ? pglob->gl_pathc : 0;

    char **v = (char **)malloc((offs + old + cur.n + 1) * sizeof(*v));
    if (!v) goto nospace;
    for (size_t i = 0; i < offs; i++) v[i] = 0;
    if (old) for (size_t i = 0; i < old; i++) v[offs + i] = pglob->gl_pathv[offs + i];
    for (size_t i = 0; i < cur.n; i++) v[offs + old + i] = cur.v[i];
    v[offs + old + cur.n] = 0;

    if (old) free(pglob->gl_pathv);
    free(cur.v);                        /* the strings themselves moved to v */
    free(pat);

    pglob->gl_pathc = old + cur.n;
    pglob->gl_pathv = v;
    return 0;

nospace:
    free(pat);
    list_free(&cur);
    list_free(&next);
    return GLOB_NOSPACE;

aborted:
    free(pat);
    list_free(&cur);
    list_free(&next);
    return GLOB_ABORTED;
}

void globfree(glob_t *pglob) {
    if (!pglob || !pglob->gl_pathv) return;
    size_t offs = pglob->gl_offs;
    for (size_t i = 0; i < pglob->gl_pathc; i++) free(pglob->gl_pathv[offs + i]);
    free(pglob->gl_pathv);
    pglob->gl_pathv = 0;
    pglob->gl_pathc = 0;
}
