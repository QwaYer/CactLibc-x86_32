/* Host shims for the libc units under test.
 *
 * Provides the two things a host build cannot take from the real libc: a
 * private errno slot (the units are compiled with __errno_location renamed to
 * cact_host_errno_location so glibc's own errno is left alone) and stubs for
 * the CactOS-only nodeio helpers, which are never exercised off-target. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static __thread int host_errno;

int *cact_host_errno_location(void) { return &host_errno; }
int *cact_host_errno_ptr(void)      { return &host_errno; }

/* ── the fd-based control channel (what src/futex.c uses) ───────────────────
 *
 * cact_ctl() caches a descriptor for /proc/self/ctl and drives the futex
 * through nio_ioctl(), not the nio_ctl() helper (a long-lived fd is cheaper
 * than an open per wait).  A unit that needs that path sets
 * cact_host_ctl_fd_on, and then the shim hands out a fake descriptor, records
 * the request, and can play "another process posted while we slept" — the
 * semaphore unit's only way to exercise a blocking wait without a kernel.
 *
 * `cact_host_futex_word` is the unit's *host* pointer to the word it expects to
 * be waited on: the ABI carries uaddr as 32 bits, so on this 64-bit host the
 * address the unit passes is truncated before the shim could use it (the same
 * reason the tls unit cannot check the %gs base it hands over).  The unit wires
 * the word up for the shim instead, and checks the truncated uaddr it was sent
 * against the truncated address of its own word.
 */
int           cact_host_ctl_fd_on;
int           cact_host_ioctl_calls;
unsigned long cact_host_ioctl_cmd;
int           cact_host_futex_op = -1;     /* the op the unit's code asked for */
unsigned int  cact_host_futex_uaddr;
int           cact_host_futex_val;
int           cact_host_futex_timeout;
int           cact_host_futex_waits;       /* WAITs seen since the last reset */
void         *cact_host_futex_word;        /* the word the unit is waiting on */
int           cact_host_futex_post;        /* 1 = a poster gets there first */
int           cact_host_futex_eagain_first;/* 1 = answer the first WAIT -EAGAIN */
/* The fake kernel's answer to a WAIT nobody posts for.  The default reports an
 * interrupted wait, which is a real thing the semaphore has to handle and is
 * also what stops a caller's retry loop from spinning forever. */
int           cact_host_futex_wait_ret = -4;      /* -EINTR */

/* Mirrors cact_futex_arg_t (include/ioctl_abi.h), which the unit's code passes
 * through nio_ioctl(): { uaddr, op, val, timeout_ms }. */
struct host_futex_arg { unsigned int uaddr; int op; int val; int timeout_ms; };

#define HOST_PROCCTL_FUTEX     0x3214u   /* CACT_PROCCTL_FUTEX */
#define HOST_FUTEX_WAIT        0          /* CACT_FUTEX_WAIT/_WAIT_SHARED */
#define HOST_FUTEX_WAIT_SHARED 2
#define HOST_FUTEX_WAIT_MAX    8          /* give up instead of looping forever */

int     nio_open(const char *path, int flags)  {
    if (cact_host_ctl_fd_on && path && strcmp(path, "/proc/self/ctl") == 0) return 7;
    (void)flags;
    return -1;
}
int     nio_close(int fd)                      { (void)fd; return -1; }
ssize_t nio_read(int fd, void *buf, size_t n)  { (void)fd; (void)buf; (void)n; return -1; }
ssize_t nio_write(int fd, const void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }

int     nio_ioctl(int fd, unsigned long c, void *a) {
    struct host_futex_arg f;
    (void)fd;
    if (c != HOST_PROCCTL_FUTEX || !a) return -1;

    memcpy(&f, a, sizeof f);
    cact_host_ioctl_calls++;
    cact_host_ioctl_cmd      = c;
    cact_host_futex_op       = f.op;
    cact_host_futex_uaddr    = f.uaddr;
    cact_host_futex_val      = f.val;
    cact_host_futex_timeout  = f.timeout_ms;

    if (f.op == HOST_FUTEX_WAIT || f.op == HOST_FUTEX_WAIT_SHARED) {
        if (++cact_host_futex_waits > HOST_FUTEX_WAIT_MAX) return -22;  /* -EINVAL */
        if (cact_host_futex_eagain_first && cact_host_futex_waits == 1) return -11;
        if (cact_host_futex_post) {
            /* The other process got there first: by the time we are woken the
             * word is non-zero, which is exactly what the caller re-checks. */
            if (cact_host_futex_word) *(volatile int *)cact_host_futex_word += 1;
            return 0;
        }
        return cact_host_futex_wait_ret;
    }
    return 0;                                   /* a WAKE: "woken", or nobody there */
}

/* nio_ctl is a recorder too: the thread_local unit checks that set_thread_area()
 * sent the right procctl with the base it was given, and the signal unit checks
 * the whole sigaction argument and the sigwait() one.  24 bytes covers the
 * largest argument any of them passes (cact_sigwait_arg_t, at 20). */
int           cact_host_ctl_calls;
unsigned long cact_host_ctl_cmd;
unsigned int  cact_host_ctl_arg0;
unsigned char cact_host_ctl_arg[24];
int           cact_host_ctl_ret = 0;
/* What the "kernel" writes back into the argument before returning, for the
 * calls whose answer travels in it (CACT_PROCCTL_SIGWAIT's signal/code/value).
 * Non-zero `cact_host_ctl_out_on` arms it. */
unsigned char cact_host_ctl_out[24];
int           cact_host_ctl_out_on;

int     nio_ctl(unsigned long c, void *a) {
    cact_host_ctl_calls++;
    cact_host_ctl_cmd = c;
    if (a) {
        memcpy(&cact_host_ctl_arg0, a, 4);
        memcpy(cact_host_ctl_arg, a, sizeof cact_host_ctl_arg);
        if (cact_host_ctl_out_on) memcpy(a, cact_host_ctl_out, sizeof cact_host_ctl_out);
    }
    return cact_host_ctl_ret;
}
int     nio_read_file(const char *p, void *b, size_t n) { (void)p; (void)b; (void)n; return -1; }

/* nio_dev_cmd is a recorder: the time unit test checks that clock_settime()
 * built the right /dev/sys request.  cact_time_t is { u32 sec, u32 usec }, so
 * eight bytes are read back out of the argument. */
int           cact_host_devcmd_calls;
char          cact_host_devcmd_dev[16];
unsigned long cact_host_devcmd_cmd;
unsigned int  cact_host_devcmd_sec;
unsigned int  cact_host_devcmd_usec;
int           cact_host_devcmd_ret = 0;

int     nio_dev_cmd(const char *d, unsigned long c, void *a) {
    cact_host_devcmd_calls++;
    size_t i = 0;
    if (d) for (; d[i] && i < sizeof cact_host_devcmd_dev - 1; i++) cact_host_devcmd_dev[i] = d[i];
    cact_host_devcmd_dev[i] = '\0';
    cact_host_devcmd_cmd = c;
    if (a) { memcpy(&cact_host_devcmd_sec, a, 4); memcpy(&cact_host_devcmd_usec, (char *)a + 4, 4); }
    return cact_host_devcmd_ret;
}
int     nio_open_parent(const char *p, char *base, size_t max)
    { (void)p; if (max) base[0] = '\0'; return -1; }
int     nio_map(int r)                         { return r; }

/* set_thread_area is a recorder: the tls unit checks that the block it built
 * was handed to the kernel as the %gs base.  The libc's <sys/ldt.h> struct
 * user_desc has base_addr as its second word, which is all we need. */
struct host_user_desc { unsigned int entry_number, base_addr, limit, flags; };

int          cact_host_tls_calls;
unsigned int cact_host_tls_base;

int cact_host_set_thread_area(struct host_user_desc *ud) {
    cact_host_tls_calls++;
    cact_host_tls_base = ud ? ud->base_addr : 0;
    return 0;
}

/* Minimal host stand-in for CactOS glob(), used by the wordexp test.  It
 * implements GLOB_NOCHECK (hand the pattern back unchanged), which is what a
 * non-matching pattern yields anyway; real pathname expansion is exercised by
 * the glob unit's own test. */
typedef struct {
    unsigned long gl_pathc;
    char        **gl_pathv;
    unsigned long gl_offs;
} cact_glob_t;

int cact_glob(const char *pattern, int flags, int (*errfunc)(const char *, int), cact_glob_t *g) {
    (void)flags; (void)errfunc;
    char **v = malloc(2 * sizeof(char *));
    if (!v) return 1;
    v[0] = strdup(pattern);
    v[1] = 0;
    if (!v[0]) { free(v); return 1; }
    g->gl_pathc = 1;
    g->gl_pathv = v;
    g->gl_offs  = 0;
    return 0;
}

void cact_globfree(cact_glob_t *g) {
    if (g && g->gl_pathv) { free(g->gl_pathv[0]); free(g->gl_pathv); }
    if (g) { g->gl_pathv = 0; g->gl_pathc = 0; }
}

/* ── dirent/stat stand-ins for the glob unit ────────────────────────────────
 *
 * src/glob.c is compiled with the CactOS headers, so its `struct stat` and
 * `struct dirent` are the libc's own 16- and 128-byte shapes, not glibc's.
 * These wrappers call the host and repack into the libc's layout; the glob
 * unit renames stat/opendir/readdir/closedir to these names.  Only the fields
 * glob.c actually reads are filled: st_mode (offset 4 of the libc struct) and
 * d_ino/d_name. */
#include <dirent.h>
#include <sys/stat.h>

int cact_host_stat(const char *path, void *buf) {
    struct stat st;
    int r = stat(path, &st);
    if (r == 0 && buf) {
        unsigned int mode = (unsigned int)st.st_mode;
        memcpy((char *)buf + 4, &mode, 4);       /* struct stat { ino; mode; ... } */
    }
    return r;
}

void *cact_host_opendir(const char *name) { return opendir(name); }

/* The libc's struct dirent is { uint32_t d_ino; char d_name[124]; }. */
struct cact_de { unsigned int d_ino; char d_name[124]; };
static struct cact_de cact_de_buf;

void *cact_host_readdir(void *d) {
    struct dirent *de = readdir((DIR *)d);
    size_t i = 0;
    if (!de) return 0;
    cact_de_buf.d_ino = (unsigned int)de->d_ino;
    for (; de->d_name[i] && i < sizeof cact_de_buf.d_name - 1; i++)
        cact_de_buf.d_name[i] = de->d_name[i];
    cact_de_buf.d_name[i] = '\0';
    return &cact_de_buf;
}

int cact_host_closedir(void *d) { return closedir((DIR *)d); }

/* The pthread unit renames the thread start stub away (the .S file is not part
 * of a host build); this stands in for it. */
void cact_pth_thread_exit_stub(void) {}
void cact_pth_pid_init(void) {}
