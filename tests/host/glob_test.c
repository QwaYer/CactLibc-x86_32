/* glob host test: the shipped src/glob.c against glibc's glob() on the same
 * real directory tree, so each flag is checked against the reference rather
 * than against a hand-written expectation.  The tree lives in a private temp
 * dir; the libc's stat/dirent shapes are presented by shims in shim.c.
 *
 * The libc's GLOB_* values differ from glibc's (GLOB_TILDE is 0x100 here vs
 * 0x1000 there), so the flags are given in the libc's numbering and translated
 * for the reference call. */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glob.h>

typedef struct { size_t gl_pathc; char **gl_pathv; size_t gl_offs; } cact_glob_t;
int  cact_glob_real(const char *pattern, int flags,
                    int (*errfunc)(const char *, int), cact_glob_t *pglob);
void cact_globfree_real(cact_glob_t *pglob);

/* the libc's flag values (include/glob.h) */
#define CG_ERR      0x0001
#define CG_MARK     0x0002
#define CG_NOSORT   0x0004
#define CG_DOOFFS   0x0008
#define CG_NOCHECK  0x0010
#define CG_APPEND   0x0020
#define CG_NOESCAPE 0x0040
#define CG_PERIOD   0x0080
#define CG_TILDE    0x0100

static char root[256];
static int checks, fails, ef_calls;

static int to_glibc(int f) {
    int g = 0;
    if (f & CG_ERR)      g |= GLOB_ERR;
    if (f & CG_MARK)     g |= GLOB_MARK;
    if (f & CG_NOSORT)   g |= GLOB_NOSORT;
    if (f & CG_DOOFFS)   g |= GLOB_DOOFFS;
    if (f & CG_NOCHECK)  g |= GLOB_NOCHECK;
    if (f & CG_APPEND)   g |= GLOB_APPEND;
    if (f & CG_NOESCAPE) g |= GLOB_NOESCAPE;
    if (f & CG_PERIOD)   g |= GLOB_PERIOD;
    if (f & CG_TILDE)    g |= GLOB_TILDE;
    return g;
}

static int errfn(const char *p, int e) { (void)p; (void)e; ef_calls++; return 0; }

static void path_of(char *out, size_t n, const char *rel) {
    snprintf(out, n, "%s/%s", root, rel);
}

static void mkfile(const char *rel) {
    char p[512]; int fd;
    path_of(p, sizeof p, rel);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}
static void mkdir_(const char *rel) {
    char p[512]; path_of(p, sizeof p, rel); mkdir(p, 0755);
}

static void cmp_pat(const char *lbl, const char *pat, int cflags, size_t offs) {
    glob_t g; cact_glob_t c;
    int rr, cr;

    memset(&g, 0, sizeof g); g.gl_offs = offs;
    memset(&c, 0, sizeof c); c.gl_offs = offs;

    rr = glob(pat, to_glibc(cflags), errfn, &g);
    cr = cact_glob_real(pat, cflags, errfn, &c);
    checks++;
    if (rr != cr) {
        fails++;
        printf("FAIL %-22s flags=0x%03x rc glibc=%d cact=%d\n", lbl, cflags, rr, cr);
    } else if (rr == 0) {
        if (g.gl_pathc != c.gl_pathc) {
            fails++;
            printf("FAIL %-22s flags=0x%03x n glibc=%zu cact=%zu\n",
                   lbl, cflags, (size_t)g.gl_pathc, c.gl_pathc);
        } else {
            for (size_t i = 0; i < g.gl_pathc; i++) {
                const char *a = g.gl_pathv[offs + i];
                const char *b = c.gl_pathv[offs + i];
                if (strcmp(a, b)) {
                    fails++;
                    printf("FAIL %-22s flags=0x%03x [%zu] glibc=\"%s\" cact=\"%s\"\n",
                           lbl, cflags, i, a, b);
                    break;
                }
            }
        }
    }
    if (rr == 0) globfree(&g);
    cact_globfree_real(&c);
}

int main(void) {
    static const struct { const char *sfx; int flags; } abs_cases[] = {
        {"/nofile",  0},            {"/nofile",  CG_ERR},
        {"/*",       0},            {"/*",       CG_NOSORT},
        {"/*",       CG_PERIOD},    {"/*",       CG_MARK},
        {"/*",       CG_PERIOD|CG_MARK},
        {"/*",       CG_NOCHECK},   {"/*",       CG_ERR},
        {"/*.c",     0},            {"/*.c",     CG_NOSORT},
        {"/*.c",     CG_NOCHECK},
        {"/?.c",     0},            {"/[ab].txt", 0},   {"/[!a]*", 0},
        {"/.*",      0},            {"/.*",      CG_PERIOD},
        {"/sub",     0},            {"/sub",     CG_MARK},
        {"/sub/",    0},            {"/sub/*",   0},    {"/sub/*", CG_MARK},
        {"/*/*",     0},            {"/*/",      0},
        {"/empty*",  0},            {"/emptydir/*", 0},
        {"/zzz*",    0},            {"/zzz*",    CG_NOCHECK},
        {"/nope/*",  0},            {"/nope/*",  CG_ERR},
        {"/noread/*", 0},           {"/noread/*", CG_ERR},
        {"/q\\*star", 0},           {"/q\\*star", CG_NOESCAPE},
        {"/q*star",  0},            {"/q*star",  CG_NOESCAPE},
    };
    static const char *const rel_cases[] = {
        "*.c", "*", "sub/*", "nope/*", "noread/*", "*/*",
    };

    char tmpl[64] = "/tmp/cactglobXXXXXX";
    if (!mkdtemp(tmpl)) { printf("glob: mkdtemp failed\n"); return 1; }
    strcpy(root, tmpl);

    mkfile("a.txt"); mkfile("b.c"); mkfile("c.c"); mkfile(".hid"); mkfile("q*star");
    mkdir_("sub"); mkfile("sub/inner.txt");
    mkdir_("emptydir");
    mkdir_("noread");
    { char p[512]; path_of(p, sizeof p, "noread"); chmod(p, 0000); }

    for (size_t i = 0; i < sizeof abs_cases / sizeof abs_cases[0]; i++) {
        char pat[512];
        snprintf(pat, sizeof pat, "%s%s", root, abs_cases[i].sfx);
        cmp_pat(abs_cases[i].sfx, pat, abs_cases[i].flags, 0);
    }

    /* GLOB_TILDE: ~ expands to $HOME, which is the tree here. */
    setenv("HOME", root, 1);
    { char pat[512];
      snprintf(pat, sizeof pat, "~/a.txt");   cmp_pat("~/a.txt",  pat, CG_TILDE, 0);
      snprintf(pat, sizeof pat, "~/*.c");     cmp_pat("~/*.c",    pat, CG_TILDE, 0);
    }

    /* Relative patterns exercise the empty-base path. */
    if (chdir(root) == 0) {
        for (size_t i = 0; i < sizeof rel_cases / sizeof rel_cases[0]; i++)
            cmp_pat(rel_cases[i], rel_cases[i], 0, 0);
    }

    /* GLOB_DOOFFS leaves gl_offs NULLs in front. */
    { char pat[512];
      snprintf(pat, sizeof pat, "%s/*.c", root);
      cmp_pat("dooffs", pat, CG_DOOFFS, 4);
    }

    /* GLOB_APPEND appends to what is already there; both sides start from a
     * first call, then append a second result set. */
    { char p1[512], p2[512];
      glob_t g; cact_glob_t c;
      int rr, cr, ok = 1;
      snprintf(p1, sizeof p1, "%s/*.txt", root);
      snprintf(p2, sizeof p2, "%s/*.c", root);
      memset(&g, 0, sizeof g); memset(&c, 0, sizeof c);
      glob(p1, to_glibc(0), 0, &g);
      cact_glob_real(p1, 0, 0, &c);
      rr = glob(p2, to_glibc(CG_APPEND), 0, &g);
      cr = cact_glob_real(p2, CG_APPEND, 0, &c);
      checks++;
      if (rr != cr || g.gl_pathc != c.gl_pathc) {
          fails++; printf("FAIL append rc %d/%d n %zu/%zu\n",
                          rr, cr, (size_t)g.gl_pathc, c.gl_pathc);
      } else {
          for (size_t i = 0; i < g.gl_pathc; i++)
              if (strcmp(g.gl_pathv[i], c.gl_pathv[i])) ok = 0;
          if (!ok) { fails++; printf("FAIL append paths differ\n"); }
      }
      if (rr == 0) globfree(&g);
      cact_globfree_real(&c);
    }

    /* errfunc must be reported for an unreadable directory (only meaningful as
     * a non-root user: root can open a 0000 directory, in which case glibc does
     * not report it either, and the comparison above already passed). */
    if (geteuid() != 0) {
        char pat[512]; cact_glob_t c;
        path_of(pat, sizeof pat, "noread/*");
        memset(&c, 0, sizeof c);
        ef_calls = 0;
        cact_glob_real(pat, 0, errfn, &c);
        checks++;
        if (ef_calls == 0) { fails++; printf("FAIL errfunc not called for unreadable dir\n"); }
        cact_globfree_real(&c);
    }

    /* clean up the tree */
    { char p[512];
      const char *files[] = { "a.txt","b.c","c.c",".hid","q*star","sub/inner.txt" };
      const char *dirs[]  = { "sub","emptydir","noread" };
      for (size_t i = 0; i < sizeof files/sizeof files[0]; i++) { path_of(p,sizeof p,files[i]); unlink(p); }
      { char nr[512]; path_of(nr, sizeof nr, "noread"); chmod(nr, 0755); }
      for (size_t i = 0; i < sizeof dirs/sizeof dirs[0]; i++) { path_of(p,sizeof p,dirs[i]); rmdir(p); }
      rmdir(root);
    }

    printf("glob: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
