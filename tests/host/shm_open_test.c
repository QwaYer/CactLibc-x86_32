/* shm_open host test: src/shm_open.c as a naming layer over /dev/shm.
 *
 * What is under test here is the whole of src/shm_open.c: the POSIX name rules
 * ("/" + one component, no further slash), the path it assembles under
 * /dev/shm, the flags and mode it hands to open(), the FD_CLOEXEC it sets on
 * the returned descriptor, and the errno it derives for the cases SYS_OPEN
 * cannot report (it returns a bare -1 with no error channel).
 *
 * The kernel half — the tmpfs mounted at /dev/shm, so that read()/write(),
 * ftruncate() and a MAP_SHARED mmap() of the descriptor share real frames, and
 * the shared futex works across two processes — is not reachable from a host
 * test; it needs the kernel and a boot.  The three libc calls shm_open.c makes
 * are replaced by the recording stubs below (see rename_shm_open.h).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>      /* the system's O_*/F_* — the numbers agree with the libc */
#include <errno.h>

/* The unit's entry points, renamed (see rename_shm_open.h).  This libc's
 * mode_t is `unsigned`. */
int cact_shm_open(const char *name, int oflag, unsigned mode);
int cact_shm_unlink(const char *name);
int *cact_host_errno_location(void);            /* the libc's errno slot (shim) */

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ── recording stand-ins for open()/unlink()/fcntl() ───────────────────────
 *
 * open() and unlink() are modelled faithfully for the one thing that matters
 * to shm_open.c: on failure the real open() returns -1 and leaves errno alone
 * (shm_open derives it), while the real unlink() returns -1 *with* errno set
 * (it goes through nio_map). */
static char     rec_open_path[256];
static int      rec_open_flags;
static unsigned rec_open_mode;
static int      rec_open_calls;
static int      stub_open_ret = 9;              /* a plausible fd */

static char rec_unlink_path[256];
static int  rec_unlink_calls;
static int  stub_unlink_errno;                  /* non-zero: fail with it */

static int rec_fcntl_calls, rec_fcntl_fd = -1, rec_fcntl_cmd = -1, rec_fcntl_arg = -1;

static void rec_str(char *dst, size_t max, const char *src) {
    size_t i = 0;
    if (src) for (; src[i] && i + 1 < max; i++) dst[i] = src[i];
    dst[i] = '\0';
}

int cact_shm_host_open(const char *path, int flags, ...) {
    va_list ap;
    rec_str(rec_open_path, sizeof rec_open_path, path);
    rec_open_flags = flags;
    va_start(ap, flags);
    rec_open_mode = va_arg(ap, unsigned);
    va_end(ap);
    rec_open_calls++;
    return stub_open_ret;
}

int cact_shm_host_unlink(const char *path) {
    rec_str(rec_unlink_path, sizeof rec_unlink_path, path);
    rec_unlink_calls++;
    if (stub_unlink_errno) { *cact_host_errno_location() = stub_unlink_errno; return -1; }
    return 0;
}

int cact_shm_host_fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    rec_fcntl_arg = va_arg(ap, int);
    va_end(ap);
    rec_fcntl_calls++;
    rec_fcntl_fd  = fd;
    rec_fcntl_cmd = cmd;
    return 0;
}

static void reset(void) {
    rec_open_path[0] = '\0'; rec_open_flags = 0; rec_open_mode = 0; rec_open_calls = 0;
    stub_open_ret = 9;
    rec_unlink_path[0] = '\0'; rec_unlink_calls = 0; stub_unlink_errno = 0;
    rec_fcntl_calls = 0; rec_fcntl_fd = -1; rec_fcntl_cmd = -1; rec_fcntl_arg = -1;
    *cact_host_errno_location() = 0;
}

static int bad_name(const char *name) {
    reset();
    int r = cact_shm_open(name, O_RDWR, 0600);
    return r == -1 && *cact_host_errno_location() == EINVAL && rec_open_calls == 0;
}

int main(void) {
    /* ── name validation (EINVAL, and no syscall attempted) ── */
    reset();
    CK(cact_shm_open(0, O_RDWR, 0600) == -1 && *cact_host_errno_location() == EINVAL
       && rec_open_calls == 0, "NULL name: EINVAL, no open");
    CK(bad_name(""),       "empty name: EINVAL, no open");
    CK(bad_name("foo"),    "name without a leading '/': EINVAL, no open");
    CK(bad_name("/"),      "\"/\" alone: EINVAL, no open");
    CK(bad_name("//"),     "\"//\": EINVAL, no open");
    CK(bad_name("/a/b"),   "embedded '/': EINVAL, no open");
    CK(bad_name("/a/"),    "trailing '/': EINVAL, no open");

    /* ── name length: a tmpfs dirent holds 127 characters ── */
    {
        char nm[200];
        nm[0] = '/';
        for (int i = 0; i < 127; i++) nm[1 + i] = 'a';
        nm[128] = '\0';
        reset();
        CK(cact_shm_open(nm, O_RDWR, 0600) == 9 && rec_open_calls == 1,
           "127-character name is accepted");
        nm[128] = 'a'; nm[129] = '\0';                 /* now 128 characters */
        reset();
        CK(cact_shm_open(nm, O_RDWR, 0600) == -1
           && *cact_host_errno_location() == ENAMETOOLONG && rec_open_calls == 0,
           "128-character name: ENAMETOOLONG, no open");
    }

    /* ── access mode: POSIX allows O_RDONLY or O_RDWR only ── */
    reset();
    CK(cact_shm_open("/w", O_WRONLY, 0600) == -1 && *cact_host_errno_location() == EINVAL
       && rec_open_calls == 0, "O_WRONLY: EINVAL, no open");
    reset();
    CK(cact_shm_open("/w", O_WRONLY | O_CREAT, 0600) == -1
       && *cact_host_errno_location() == EINVAL && rec_open_calls == 0,
       "O_WRONLY|O_CREAT: EINVAL, no open");
    reset();
    CK(cact_shm_open("/r", O_RDONLY, 0600) == 9 && rec_open_calls == 1,
       "O_RDONLY is accepted");

    /* ── the path, the flags, the mode and FD_CLOEXEC ── */
    reset();
    {
        int fd = cact_shm_open("/abc.def", O_RDWR | O_CREAT, 0600);
        CK(fd == 9, "the fd is returned unchanged (got %d)", fd);
        CK(strcmp(rec_open_path, "/dev/shm/abc.def") == 0,
           "path assembled as %s", rec_open_path);
        CK(rec_open_flags == (O_RDWR | O_CREAT), "flags passed through (%#x)", rec_open_flags);
        CK(rec_open_mode == 0600, "mode passed through (%#o)", rec_open_mode);
        CK(rec_fcntl_calls == 1 && rec_fcntl_fd == 9 && rec_fcntl_cmd == F_SETFD
           && rec_fcntl_arg == FD_CLOEXEC,
           "FD_CLOEXEC set on the returned fd (calls=%d fd=%d cmd=%d arg=%d)",
           rec_fcntl_calls, rec_fcntl_fd, rec_fcntl_cmd, rec_fcntl_arg);
    }

    reset();
    cact_shm_open("/e", O_RDWR | O_CREAT | O_EXCL, 0600);
    CK(rec_open_flags == (O_RDWR | O_CREAT | O_EXCL),
       "O_EXCL passed through (%#x)", rec_open_flags);

    reset();
    cact_shm_open("/t", O_RDWR | O_TRUNC, 0600);
    CK(rec_open_flags == (O_RDWR | O_TRUNC), "O_TRUNC passed through (%#x)", rec_open_flags);

    /* ── errno derivation when open() fails (it reports a bare -1) ── */
    reset(); stub_open_ret = -1;
    CK(cact_shm_open("/gone", O_RDWR, 0600) == -1
       && *cact_host_errno_location() == ENOENT,
       "absent object, no O_CREAT: ENOENT");
    reset(); stub_open_ret = -1;
    CK(cact_shm_open("/taken", O_RDWR | O_CREAT | O_EXCL, 0600) == -1
       && *cact_host_errno_location() == EEXIST,
       "O_CREAT|O_EXCL on an existing object: EEXIST");
    reset(); stub_open_ret = -1;
    CK(cact_shm_open("/x", O_RDWR | O_CREAT, 0600) == -1
       && *cact_host_errno_location() == EACCES,
       "O_CREAT failure without O_EXCL: EACCES");
    CK(rec_fcntl_calls == 0, "no FD_CLOEXEC is set when open() failed");

    /* ── shm_unlink ── */
    reset();
    CK(cact_shm_unlink("/abc.def") == 0 && rec_unlink_calls == 1
       && strcmp(rec_unlink_path, "/dev/shm/abc.def") == 0,
       "unlink path assembled as %s", rec_unlink_path);
    CK(bad_name("/a/b"), "shm_open rejects an embedded '/'");
    reset();
    CK(cact_shm_unlink("no-slash") == -1 && *cact_host_errno_location() == EINVAL
       && rec_unlink_calls == 0, "shm_unlink validates the name the same way");
    reset();
    CK(cact_shm_unlink("/") == -1 && *cact_host_errno_location() == EINVAL
       && rec_unlink_calls == 0, "shm_unlink rejects \"/\"");
    reset();
    stub_unlink_errno = ENOENT;
    CK(cact_shm_unlink("/gone") == -1 && *cact_host_errno_location() == ENOENT,
       "shm_unlink reports the filesystem's error");

    printf("shm_open: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
