/* mqueue host test: src/mqueue.c as a naming/flag/errno layer over /dev/mqueue.
 *
 * What is under test here is the whole of src/mqueue.c: the POSIX name rules
 * ("/" + one component, no further slash), the path it assembles under
 * /dev/mqueue, the open-or-create dance (O_CREAT|O_EXCL first, then a plain
 * open when it was not us who created the object), the CACT_MQCTL_INIT that
 * fixes the creation limits, the argument it hands each CACT_MQCTL_* ioctl, the
 * absolute-to-relative timeout conversion, the FD_CLOEXEC, and the errno it
 * derives for the cases SYS_OPEN cannot report (a bare -1 with no error
 * channel).
 *
 * The kernel half — the queue filesystem mounted at /dev/mqueue, real priority
 * ordering, real blocking and the timed deadline — is not reachable from a host
 * test; it needs a boot (the kernel's own `mqueue: selftest passed` runs there,
 * and QwaYer exercises the userspace path).  The six libc calls mqueue.c makes
 * are replaced by the recording stubs below (see rename_mqueue.h).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>      /* the system's O_*/F_* — the numbers agree with the libc */
#include <errno.h>
#include <time.h>
#include <signal.h>     /* struct sigevent (glibc's layout matches ours) */
#include <mqueue.h>     /* the system's struct mq_attr (same layout as ours) */

/* The unit's entry points, renamed (see rename_mqueue.h).  This libc's mqd_t is
 * `int`, and it declares them itself because the system headers' mq_* are the
 * real glibc ones. */
int cact_mq_open(const char *name, int oflag, ...);
int cact_mq_close(int mqdes);
int cact_mq_unlink(const char *name);
int cact_mq_send(int mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio);
int cact_mq_receive(int mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio);
int cact_mq_timedsend(int mqdes, const char *msg_ptr, size_t msg_len,
                      unsigned msg_prio, const struct timespec *abs);
int cact_mq_timedreceive(int mqdes, char *msg_ptr, size_t msg_len,
                         unsigned *msg_prio, const struct timespec *abs);
int cact_mq_getattr(int mqdes, struct mq_attr *attr);
int cact_mq_setattr(int mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr);
int cact_mq_notify(int mqdes, const struct sigevent *sevp);

int *cact_host_errno_location(void);            /* the libc's errno slot (shim) */

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ── the wire structures (mirror include/ioctl_abi.h) and command numbers ── */
#define HOST_MQCTL_INIT    0x4001u
#define HOST_MQCTL_SEND    0x4002u
#define HOST_MQCTL_RECEIVE 0x4003u
#define HOST_MQCTL_GETATTR 0x4004u
#define HOST_MQCTL_SETATTR 0x4005u
#define HOST_MQCTL_NOTIFY  0x4006u

typedef struct { int32_t mq_flags, mq_maxmsg, mq_msgsize, mq_curmsgs, reserved[4]; } wire_attr_t;
typedef struct { char *msg_ptr; uint32_t msg_len, msg_prio; int32_t timeout_ms; } wire_msg_t;
typedef struct { uint32_t signo; } wire_notify_t;

/* ── recording stand-ins ─────────────────────────────────────────────────── */

#define OPEN_MAX 8
static char open_path[OPEN_MAX][256];
static int  open_flags[OPEN_MAX];
static int  open_ret[OPEN_MAX];
static int  open_n;

static int  close_n;
static int  close_last;

static char unlink_path[256];
static int  unlink_n;
static int  unlink_errno;                       /* non-zero: fail with it */

static int  fcntl_n, fcntl_fd = -1, fcntl_cmd = -1, fcntl_arg = -1;

static int           ioctl_n;
static int           ioctl_fd[OPEN_MAX];
static unsigned long ioctl_cmd[OPEN_MAX];
static unsigned char ioctl_arg[OPEN_MAX][64];
static int           ioctl_ret;                 /* what the "kernel" answers */
static int           ioctl_out_on;
static unsigned char ioctl_out[64];             /* what it writes back */

static struct timespec rec_now;

static void rec_str(char *dst, size_t max, const char *src) {
    size_t i = 0;
    if (src) for (; src[i] && i + 1 < max; i++) dst[i] = src[i];
    dst[i] = '\0';
}

int cact_mq_host_open(const char *path, int flags, ...) {
    /* Only the path and the flags are under test; mqueue.c passes no mode to
     * the kernel, so nothing else is read out of the varargs. */
    if (open_n >= OPEN_MAX) return -1;
    rec_str(open_path[open_n], sizeof open_path[0], path);
    open_flags[open_n] = flags;
    return open_ret[open_n++];
}

int cact_mq_host_close(int fd) {
    close_n++; close_last = fd;
    return 0;
}

int cact_mq_host_unlink(const char *path) {
    rec_str(unlink_path, sizeof unlink_path, path);
    unlink_n++;
    if (unlink_errno) { *cact_host_errno_location() = unlink_errno; return -1; }
    return 0;
}

int cact_mq_host_fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    fcntl_arg = va_arg(ap, int);
    va_end(ap);
    fcntl_n++; fcntl_fd = fd; fcntl_cmd = cmd;
    return 0;
}

/* The size of the wire argument a given command carries, so the stubs copy
 * exactly that many bytes (no over-read past the caller's buffer). */
static size_t arg_size(unsigned long cmd) {
    if (cmd == HOST_MQCTL_SEND || cmd == HOST_MQCTL_RECEIVE) return sizeof(wire_msg_t);
    if (cmd == HOST_MQCTL_NOTIFY) return sizeof(wire_notify_t);
    if (cmd == HOST_MQCTL_INIT || cmd == HOST_MQCTL_GETATTR || cmd == HOST_MQCTL_SETATTR)
        return sizeof(wire_attr_t);
    return 0;
}

int cact_mq_host_ioctl(int fd, unsigned long cmd, void *arg) {
    int i = ioctl_n < OPEN_MAX ? ioctl_n : OPEN_MAX - 1;
    size_t n = arg_size(cmd);
    ioctl_fd[i]  = fd;
    ioctl_cmd[i] = cmd;
    if (arg && n) memcpy(ioctl_arg[i], arg, n);
    if (ioctl_n < OPEN_MAX) ioctl_n++;
    if (ioctl_out_on && arg && n) memcpy(arg, ioctl_out, n);
    return ioctl_ret;
}

int cact_mq_host_clock_gettime(int clk, struct timespec *tp) {
    (void)clk;
    if (tp) *tp = rec_now;
    return 0;
}

static void reset(void) {
    open_n = 0;
    for (int i = 0; i < OPEN_MAX; i++) {
        open_path[i][0] = '\0';
        open_flags[i] = 0;
        open_ret[i] = 9;                        /* a plausible fd: open succeeds */
    }
    close_n = 0; close_last = -1;
    unlink_path[0] = '\0'; unlink_n = 0; unlink_errno = 0;
    fcntl_n = 0; fcntl_fd = -1; fcntl_cmd = -1; fcntl_arg = -1;
    ioctl_n = 0; ioctl_ret = 0; ioctl_out_on = 0;
    memset(ioctl_out, 0, sizeof ioctl_out);
    rec_now.tv_sec = 0; rec_now.tv_nsec = 0;
    *cact_host_errno_location() = 0;
}

int main(void) {
    /* ── name validation (EINVAL, and no syscall attempted) ── */
    reset();
    CK(cact_mq_open(0, O_RDWR, 0600, (struct mq_attr *)0) == -1
       && *cact_host_errno_location() == EINVAL && open_n == 0,
       "NULL name: EINVAL, no open");
    { const char *bad[] = { "", "foo", "/", "//", "/a/b", "/a/" };
      for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
          reset();
          CK(cact_mq_open(bad[i], O_RDWR, 0600, (struct mq_attr *)0) == -1
             && *cact_host_errno_location() == EINVAL && open_n == 0,
             "bad name \"%s\": EINVAL, no open", bad[i]);
      }
    }

    /* ── name length: the dirent holds 127 characters ── */
    {
        char nm[200];
        nm[0] = '/';
        for (int i = 0; i < 127; i++) nm[1 + i] = 'a';
        nm[128] = '\0';
        reset();
        CK(cact_mq_open(nm, O_RDWR, 0600, (struct mq_attr *)0) == 9 && open_n == 1,
           "127-character name accepted");
        nm[128] = 'a'; nm[129] = '\0';          /* now 128 characters */
        reset();
        CK(cact_mq_open(nm, O_RDWR, 0600, (struct mq_attr *)0) == -1
           && *cact_host_errno_location() == ENAMETOOLONG && open_n == 0,
           "128-character name: ENAMETOOLONG, no open");
    }

    /* ── access mode: neither bit (O_ACCMODE) is invalid ── */
    reset();
    CK(cact_mq_open("/q", O_ACCMODE, 0600, (struct mq_attr *)0) == -1
       && *cact_host_errno_location() == EINVAL && open_n == 0,
       "O_ACCMODE (both bits): EINVAL, no open");

    /* ── a plain open: path, flags, FD_CLOEXEC, and no INIT ── */
    reset();
    {
        int fd = cact_mq_open("/abc.def", O_RDWR, 0600, (struct mq_attr *)0);
        CK(fd == 9, "the fd is returned unchanged (got %d)", fd);
        CK(strcmp(open_path[0], "/dev/mqueue/abc.def") == 0,
           "path assembled as %s", open_path[0]);
        CK(open_flags[0] == O_RDWR, "plain open passes the read/write mode (%#x)",
           open_flags[0]);
        CK(ioctl_n == 0, "no CACT_MQCTL_INIT when the call did not create it");
        CK(fcntl_n == 1 && fcntl_fd == 9 && fcntl_cmd == F_SETFD
           && fcntl_arg == FD_CLOEXEC,
           "FD_CLOEXEC set on the returned fd (n=%d fd=%d cmd=%d arg=%d)",
           fcntl_n, fcntl_fd, fcntl_cmd, fcntl_arg);
    }

    /* ── O_NONBLOCK travels into the open flags ── */
    reset();
    cact_mq_open("/nb", O_RDONLY | O_NONBLOCK, 0600, (struct mq_attr *)0);
    CK(open_flags[0] == (O_RDONLY | O_NONBLOCK),
       "O_NONBLOCK passed through (%#x)", open_flags[0]);

    /* ── O_CREAT: asked for with O_EXCL first, and INIT fixes the defaults ── */
    {
        struct mq_attr attr;
        wire_attr_t got;
        reset();
        int fd = cact_mq_open("/new", O_RDWR | O_CREAT, 0600, (struct mq_attr *)0);
        CK(fd == 9 && open_n == 1, "O_CREAT create succeeded (fd=%d opens=%d)", fd, open_n);
        CK(open_flags[0] == (O_RDWR | O_CREAT | O_EXCL),
           "creation is attempted with O_CREAT|O_EXCL (%#x)", open_flags[0]);
        CK(ioctl_n == 1 && ioctl_cmd[0] == HOST_MQCTL_INIT && ioctl_fd[0] == 9,
           "CACT_MQCTL_INIT issued once on the new fd (n=%d cmd=%#lx)",
           ioctl_n, ioctl_n ? ioctl_cmd[0] : 0);
        memcpy(&got, ioctl_arg[0], sizeof got);
        CK(got.mq_maxmsg == 10 && got.mq_msgsize == 8192,
           "INIT carries the defaults (maxmsg=%d msgsize=%d)",
           got.mq_maxmsg, got.mq_msgsize);

        /* with an explicit attr, its limits are what INIT carries */
        memset(&attr, 0, sizeof attr);
        attr.mq_maxmsg = 4;
        attr.mq_msgsize = 64;
        reset();
        fd = cact_mq_open("/sized", O_RDWR | O_CREAT, 0600, &attr);
        CK(fd == 9 && ioctl_n == 1, "sized create: fd=%d ioctls=%d", fd, ioctl_n);
        memcpy(&got, ioctl_arg[0], sizeof got);
        CK(got.mq_maxmsg == 4 && got.mq_msgsize == 64,
           "INIT carries attr's limits (maxmsg=%d msgsize=%d)",
           got.mq_maxmsg, got.mq_msgsize);

        /* out-of-range attr: EINVAL, the fd is closed, and no INIT is sent */
        memset(&attr, 0, sizeof attr);
        attr.mq_maxmsg = 0;
        attr.mq_msgsize = 64;
        reset();
        CK(cact_mq_open("/bad", O_RDWR | O_CREAT, 0600, &attr) == -1
           && *cact_host_errno_location() == EINVAL && ioctl_n == 0
           && close_n == 1 && close_last == 9,
           "mq_maxmsg 0: EINVAL, fd closed, no INIT");
        memset(&attr, 0, sizeof attr);
        attr.mq_maxmsg = 4;
        attr.mq_msgsize = 8192 + 1;
        reset();
        CK(cact_mq_open("/bad2", O_RDWR | O_CREAT, 0600, &attr) == -1
           && *cact_host_errno_location() == EINVAL && ioctl_n == 0 && close_n == 1,
           "mq_msgsize over the limit: EINVAL, fd closed, no INIT");
    }

    /* ── O_CREAT without O_EXCL on an existing queue: fall back, no INIT ── */
    reset();
    open_ret[0] = -1;                            /* the create attempt "fails" */
    {
        int fd = cact_mq_open("/exists", O_RDWR | O_CREAT, 0600, (struct mq_attr *)0);
        CK(fd == 9 && open_n == 2,
           "O_CREAT finding the object opens it (fd=%d opens=%d)", fd, open_n);
        CK(strcmp(open_path[0], open_path[1]) == 0,
           "both attempts use the same path (%s / %s)", open_path[0], open_path[1]);
        CK((open_flags[0] & O_EXCL) && !(open_flags[1] & O_EXCL),
           "the retry drops O_EXCL (%#x then %#x)", open_flags[0], open_flags[1]);
        CK(ioctl_n == 0, "no INIT when the object already existed");
    }

    /* ── O_CREAT|O_EXCL on an existing queue: EEXIST, one attempt only ── */
    reset();
    open_ret[0] = -1;
    CK(cact_mq_open("/dup", O_RDWR | O_CREAT | O_EXCL, 0600, (struct mq_attr *)0) == -1
       && *cact_host_errno_location() == EEXIST && open_n == 1 && ioctl_n == 0,
       "O_CREAT|O_EXCL on an existing queue: EEXIST");

    /* ── opening a queue that does not exist: ENOENT ── */
    reset();
    open_ret[0] = -1;
    CK(cact_mq_open("/gone", O_RDWR, 0600, (struct mq_attr *)0) == -1
       && *cact_host_errno_location() == ENOENT && fcntl_n == 0,
       "absent object without O_CREAT: ENOENT, no FD_CLOEXEC");

    /* ── mq_close is close ── */
    reset();
    CK(cact_mq_close(9) == 0 && close_n == 1 && close_last == 9, "mq_close closes the fd");

    /* ── mq_unlink ── */
    reset();
    CK(cact_mq_unlink("/abc.def") == 0 && unlink_n == 1
       && strcmp(unlink_path, "/dev/mqueue/abc.def") == 0,
       "unlink path assembled as %s", unlink_path);
    reset();
    CK(cact_mq_unlink("no-slash") == -1 && *cact_host_errno_location() == EINVAL
       && unlink_n == 0, "mq_unlink validates the name the same way");
    reset();
    CK(cact_mq_unlink("/") == -1 && *cact_host_errno_location() == EINVAL
       && unlink_n == 0, "mq_unlink rejects \"/\"");
    reset();
    unlink_errno = ENOENT;
    CK(cact_mq_unlink("/gone") == -1 && *cact_host_errno_location() == ENOENT,
       "mq_unlink reports the filesystem's error");

    /* ── mq_send: the ioctl and its argument ── */
    reset();
    CK(cact_mq_send(9, "hello", 5, 3) == 0 && ioctl_n == 1
       && ioctl_cmd[0] == HOST_MQCTL_SEND && ioctl_fd[0] == 9,
       "mq_send issues CACT_MQCTL_SEND on the fd");
    {
        wire_msg_t m;
        memcpy(&m, ioctl_arg[0], sizeof m);
        CK(m.msg_len == 5 && m.msg_prio == 3 && m.timeout_ms == -1
           && (char *)m.msg_ptr == (char *)"hello",
           "send arg: len=%u prio=%u timeout=%d", m.msg_len, m.msg_prio, m.timeout_ms);
    }
    reset();
    ioctl_ret = -90;                             /* -EMSGSIZE */
    CK(cact_mq_send(9, "x", 1, 0) == -1 && *cact_host_errno_location() == EMSGSIZE,
       "mq_send maps the kernel's error to errno");

    /* ── mq_receive: length and priority come back ── */
    reset();
    ioctl_out_on = 1;
    {
        wire_msg_t out;
        memset(&out, 0, sizeof out);
        out.msg_len = 4; out.msg_prio = 7;
        memcpy(ioctl_out, &out, sizeof out);
    }
    {
        unsigned prio = 0;
        char buf[64];
        int r = cact_mq_receive(9, buf, sizeof buf, &prio);
        CK(r == 4 && prio == 7, "mq_receive returns the message length and priority (r=%d prio=%u)", r, prio);
        CK(ioctl_cmd[0] == HOST_MQCTL_RECEIVE, "mq_receive issues CACT_MQCTL_RECEIVE");
        wire_msg_t m;
        memcpy(&m, ioctl_arg[0], sizeof m);
        CK(m.msg_len == sizeof buf && (char *)m.msg_ptr == buf && m.timeout_ms == -1,
           "receive arg: capacity=%u timeout=%d", m.msg_len, m.timeout_ms);
    }
    reset();
    ioctl_ret = -11;                             /* -EAGAIN */
    CK(cact_mq_receive(9, "", 0 + 1 /*dummy*/, 0) == -1
       && *cact_host_errno_location() == EAGAIN,
       "mq_receive maps the kernel's error to errno");

    /* ── the timed forms: absolute CLOCK_REALTIME -> relative ms ── */
    {
        struct timespec abs;
        wire_msg_t m;

        reset();
        rec_now.tv_sec = 100; rec_now.tv_nsec = 0;   /* "now" for the conversion */
        abs.tv_sec = 105; abs.tv_nsec = 500000000L;  /* +5.5 s */
        cact_mq_timedsend(9, "x", 1, 0, &abs);
        memcpy(&m, ioctl_arg[0], sizeof m);
        CK(m.timeout_ms == 5500, "timed send converts +5.5s to 5500 ms (got %d)", m.timeout_ms);

        reset();
        rec_now.tv_sec = 100; rec_now.tv_nsec = 0;
        cact_mq_timedsend(9, "x", 1, 0, (const struct timespec *)0);
        memcpy(&m, ioctl_arg[0], sizeof m);
        CK(m.timeout_ms == -1, "timed send with a NULL deadline waits forever (%d)", m.timeout_ms);

        /* a deadline already past: the kernel is polled, and its EAGAIN is
         * reported as ETIMEDOUT */
        reset();
        rec_now.tv_sec = 100; rec_now.tv_nsec = 0;
        ioctl_ret = -11;
        abs.tv_sec = 50; abs.tv_nsec = 0;
        CK(cact_mq_timedreceive(9, "x", 1, 0, &abs) == -1
           && *cact_host_errno_location() == ETIMEDOUT && ioctl_n == 1,
           "expired deadline: ETIMEDOUT, not EAGAIN");
        memcpy(&m, ioctl_arg[0], sizeof m);
        CK(m.timeout_ms == 0, "expired deadline polls the kernel (timeout_ms=%d)", m.timeout_ms);
    }

    /* ── mq_getattr fills the attribute struct ── */
    reset();
    ioctl_out_on = 1;
    {
        wire_attr_t out;
        memset(&out, 0, sizeof out);
        out.mq_flags = O_NONBLOCK; out.mq_maxmsg = 4; out.mq_msgsize = 64; out.mq_curmsgs = 2;
        memcpy(ioctl_out, &out, sizeof out);
    }
    {
        struct mq_attr attr;
        CK(cact_mq_getattr(9, &attr) == 0 && ioctl_cmd[0] == HOST_MQCTL_GETATTR
           && attr.mq_maxmsg == 4 && attr.mq_msgsize == 64 && attr.mq_curmsgs == 2
           && attr.mq_flags == O_NONBLOCK,
           "mq_getattr reports the kernel's attributes");
    }

    /* ── mq_setattr passes mq_flags and reports the old attr ── */
    reset();
    ioctl_out_on = 1;
    {
        wire_attr_t out;
        memset(&out, 0, sizeof out);
        out.mq_maxmsg = 4; out.mq_msgsize = 64; out.mq_curmsgs = 2;
        memcpy(ioctl_out, &out, sizeof out);
    }
    {
        struct mq_attr nw, old;
        memset(&nw, 0, sizeof nw);
        nw.mq_flags = O_NONBLOCK;
        nw.mq_maxmsg = 99;                        /* must be ignored */
        CK(cact_mq_setattr(9, &nw, &old) == 0 && ioctl_cmd[0] == HOST_MQCTL_SETATTR,
           "mq_setattr issues CACT_MQCTL_SETATTR");
        wire_attr_t in;
        memcpy(&in, ioctl_arg[0], sizeof in);
        CK(in.mq_flags == O_NONBLOCK && in.mq_maxmsg == 0,
           "setattr passes mq_flags only (flags=%d maxmsg=%d)", in.mq_flags, in.mq_maxmsg);
        CK(old.mq_maxmsg == 4 && old.mq_msgsize == 64 && old.mq_curmsgs == 2,
           "setattr reports the previous attributes");
    }

    /* ── mq_notify maps the sigevent to the wire form ── */
    reset();
    {
        struct sigevent sev;
        memset(&sev, 0, sizeof sev);
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo  = SIGALRM;
        CK(cact_mq_notify(9, &sev) == 0 && ioctl_cmd[0] == HOST_MQCTL_NOTIFY,
           "mq_notify(SIGEV_SIGNAL) issues CACT_MQCTL_NOTIFY");
        wire_notify_t n;
        memcpy(&n, ioctl_arg[0], sizeof n);
        CK(n.signo == (uint32_t)SIGALRM, "notify passes the signal (%#x)", n.signo);
    }
    reset();
    CK(cact_mq_notify(9, (const struct sigevent *)0) == 0
       && ioctl_cmd[0] == HOST_MQCTL_NOTIFY,
       "mq_notify(NULL) cancels through the same ioctl");
    {
        wire_notify_t n;
        memcpy(&n, ioctl_arg[0], sizeof n);
        CK(n.signo == 0, "NULL sigevent unregisters (signo=0)");
    }
    reset();
    {
        struct sigevent sev;
        memset(&sev, 0, sizeof sev);
        sev.sigev_notify = SIGEV_NONE;
        CK(cact_mq_notify(9, &sev) == 0 && ioctl_n == 1, "SIGEV_NONE unregisters");
    }
    reset();
    {
        struct sigevent sev;
        memset(&sev, 0, sizeof sev);
        sev.sigev_notify = SIGEV_THREAD;
        CK(cact_mq_notify(9, &sev) == -1 && *cact_host_errno_location() == EINVAL
           && ioctl_n == 0, "SIGEV_THREAD: EINVAL, no ioctl");
    }

    printf("mqueue: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
