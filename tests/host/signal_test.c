/* signal host test: what sigaction() sends the kernel.
 *
 * The disposition has to arrive whole — signum, the chosen entry point, the
 * flags and sa_mask — because the kernel is what acts on all four (SA_ONSTACK
 * picks the stack, SA_SIGINFO picks the three-argument entry, SA_NODEFER and
 * sa_mask decide what is blocked while the handler runs).  The kernel side of
 * that is covered by the headless boot check; this covers the marshalling. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* The libc's <signal.h> cannot be included here: it pulls in its own <unistd.h>,
 * whose 32-bit ssize_t/off_t collide with the system headers.  These mirror the
 * parts this test uses — and the struct's field order *is* the contract the test
 * is checking, so a drift in the header shows up as a failure. */
typedef uint32_t sigset_t;

typedef struct {
    int   si_signo;
    int   si_code;
    void *si_addr;
    union { int sival_int; void *sival_ptr; } si_value;
} siginfo_t;

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int      sa_flags;
    void   (*sa_sigaction)(int, siginfo_t *, void *);
};

#define SIGINT  11
#define SIGQUIT 12
#define SIGALRM 5
#define SA_SIGINFO  0x0004
#define SA_NODEFER  0x40000000
#define SA_ONSTACK  0x08000000

int cact_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
/* The libc's own union sigval, mirrored: { int; void * } is 4 bytes on the
 * target and 8 here, and what matters is that both sides agree on this host. */
union cact_sigval { int sival_int; void *sival_ptr; };
struct cact_ts { long tv_sec; long tv_nsec; };
int cact_sigwait(const sigset_t *set, int *sig);
int cact_sigwaitinfo(const sigset_t *set, siginfo_t *info);
int cact_sigtimedwait(const sigset_t *set, siginfo_t *info, const struct cact_ts *timeout);
int cact_sigqueue(int pid, int sig, union cact_sigval value);

extern int           cact_host_ctl_calls;
extern unsigned long cact_host_ctl_cmd;
extern unsigned char cact_host_ctl_arg[24];
extern int           cact_host_ctl_ret;
extern unsigned char cact_host_ctl_out[24];
extern int           cact_host_ctl_out_on;

#define CACT_PROCCTL_SIGACTION 0x3208
#define CACT_PROCCTL_SIGWAIT   0x321A
#define CACT_PROCCTL_SIGQUEUE  0x321B

#define EAGAIN 11
#define EINVAL 22

static int checks, fails;

static void h_plain(int s) { (void)s; }
static void h_info(int s, siginfo_t *i, void *c) { (void)s; (void)i; (void)c; }

/* The libc's own errno slot, as the other units read it. */
int *cact_host_errno_ptr(void);
static int  host_errno(void)      { return *cact_host_errno_ptr(); }
static void errno_reset(void)     { *cact_host_errno_ptr() = 0; }

static uint32_t a32(int i) {
    return (uint32_t)cact_host_ctl_arg[i * 4]
         | ((uint32_t)cact_host_ctl_arg[i * 4 + 1] << 8)
         | ((uint32_t)cact_host_ctl_arg[i * 4 + 2] << 16)
         | ((uint32_t)cact_host_ctl_arg[i * 4 + 3] << 24);
}

int main(void) {
    struct sigaction act;

    /* A plain handler: signum, handler, flags and sa_mask all travel. */
    memset(&act, 0, sizeof act);
    act.sa_handler = h_plain;
    act.sa_flags   = SA_ONSTACK;
    act.sa_mask    = 0x42;
    cact_host_ctl_calls = 0;
    int r = cact_sigaction(SIGINT, &act, 0);
    checks++;
    if (r != 0 || cact_host_ctl_calls != 1 || cact_host_ctl_cmd != CACT_PROCCTL_SIGACTION ||
        a32(0) != (uint32_t)SIGINT || a32(1) != (uint32_t)(uintptr_t)h_plain ||
        a32(2) != (uint32_t)SA_ONSTACK || a32(3) != 0x42u) {
        fails++;
        printf("FAIL sigaction: r=%d calls=%d cmd=%#lx signum=%u handler=%#x flags=%#x mask=%#x\n",
               r, cact_host_ctl_calls, cact_host_ctl_cmd, a32(0), a32(1), a32(2), a32(3));
    }

    /* SA_SIGINFO with both pointers set picks sa_sigaction, not sa_handler. */
    memset(&act, 0, sizeof act);
    act.sa_handler   = h_plain;
    act.sa_sigaction = h_info;
    act.sa_flags     = SA_SIGINFO | SA_NODEFER;
    act.sa_mask      = 0x1234;
    cact_sigaction(SIGQUIT, &act, 0);
    checks++;
    if (a32(0) != (uint32_t)SIGQUIT || a32(1) != (uint32_t)(uintptr_t)h_info ||
        a32(2) != (uint32_t)(SA_SIGINFO | SA_NODEFER) || a32(3) != 0x1234u) {
        fails++;
        printf("FAIL SA_SIGINFO: signum=%u handler=%#x flags=%#x mask=%#x\n",
               a32(0), a32(1), a32(2), a32(3));
    }

    /* Without SA_SIGINFO the plain handler is used even if sa_sigaction is set. */
    memset(&act, 0, sizeof act);
    act.sa_handler   = h_plain;
    act.sa_sigaction = h_info;
    act.sa_mask      = 0;
    cact_sigaction(SIGINT, &act, 0);
    checks++;
    if (a32(1) != (uint32_t)(uintptr_t)h_plain) {
        fails++;
        printf("FAIL no SA_SIGINFO: handler=%#x want %#x\n", a32(1), (uint32_t)(uintptr_t)h_plain);
    }

    /* A NULL disposition is rejected in the libc, without reaching the kernel. */
    cact_host_ctl_calls = 0;
    checks++;
    if (cact_sigaction(SIGINT, 0, 0) != -1 || cact_host_ctl_calls != 0) {
        fails++;
        printf("FAIL null act: calls=%d\n", cact_host_ctl_calls);
    }

    /* A kernel failure becomes -1. */
    cact_host_ctl_ret = -22;
    memset(&act, 0, sizeof act);
    act.sa_handler = h_plain;
    checks++;
    if (cact_sigaction(SIGINT, &act, 0) != -1) {
        fails++;
        printf("FAIL error propagation\n");
    }

    /* ── sigwait(): the set travels, the wait is unbounded (timeout_ms -1), and
     * the signal comes back as one of the SIG* values above. ── */
    {
        sigset_t set = (uint32_t)((1u << SIGINT) | (1u << SIGALRM));
        int sig = 0, r;

        cact_host_ctl_out_on = 0;
        cact_host_ctl_ret = 0;
        cact_host_ctl_calls = 0;
        /* what the kernel would write back: signal index 5 (SIGALRM), SI_QUEUE
         * and a value */
        memset(cact_host_ctl_out, 0, sizeof cact_host_ctl_out);
        cact_host_ctl_out[8] = 5;      /* signum = index 5 */
        cact_host_ctl_out[12] = 9;     /* code */
        cact_host_ctl_out[16] = 0x68;  /* value (low byte) */
        cact_host_ctl_out_on = 1;

        r = cact_sigwait(&set, &sig);
        checks++;
        if (r != 0 || cact_host_ctl_calls != 1 ||
            cact_host_ctl_cmd != CACT_PROCCTL_SIGWAIT ||
            a32(0) != set || (int32_t)a32(1) != -1) {      /* wait forever */
            fails++;
            printf("FAIL sigwait: r=%d cmd=%#lx set=%#x timeout=%d\n",
                   r, cact_host_ctl_cmd, a32(0), (int32_t)a32(1));
        }
        checks++;
        if (sig != (int)(1u << 5)) {
            fails++;
            printf("FAIL sigwait: signal %#x, want SIGALRM %#x\n", sig, 1u << 5);
        }

        /* sigwaitinfo(): the same wait, and si_* filled from the answer.  It
         * returns the signal number itself, not 0. */
        siginfo_t info;
        memset(&info, 0, sizeof info);
        cact_host_ctl_out_on = 1;
        r = cact_sigwaitinfo(&set, &info);
        checks++;
        if (r != (int)(1u << 5) || info.si_signo != (int)(1u << 5) ||
            info.si_code != 9 || info.si_value.sival_int != 0x68) {
            fails++;
            printf("FAIL sigwaitinfo: r=%#x si_signo=%#x si_code=%d si_value=%d\n",
                   r, info.si_signo, info.si_code, info.si_value.sival_int);
        }

        /* A kernel EAGAIN (a timed wait that expired) is -1 + errno for the
         * timed form, and the error number itself for sigwait(). */
        cact_host_ctl_out_on = 0;
        cact_host_ctl_ret = -EAGAIN;
        r = cact_sigwait(&set, &sig);
        checks++;
        if (r != EAGAIN) {
            fails++;
            printf("FAIL sigwait EAGAIN: r=%d\n", r);
        }
        errno_reset();
        cact_host_ctl_ret = -EAGAIN;
        r = cact_sigtimedwait(&set, &info, 0);
        checks++;
        if (r != -1 || host_errno() != EAGAIN) {
            fails++;
            printf("FAIL sigtimedwait EAGAIN: r=%d errno=%d\n", r, host_errno());
        }

        /* The timeout: NULL waits forever, a value is converted to ms (rounding
         * up, so the wait is never shorter than asked) and an invalid one is
         * refused in the libc. */
        cact_host_ctl_ret = 0;
        cact_host_ctl_out_on = 1;
        struct cact_ts ts;
        ts.tv_sec = 1; ts.tv_nsec = 500000000;      /* 1500 ms */
        cact_sigtimedwait(&set, 0, &ts);
        checks++;
        if ((int32_t)a32(1) != 1500) {
            fails++;
            printf("FAIL sigtimedwait timeout: %d ms, want 1500\n", (int32_t)a32(1));
        }
        ts.tv_sec = 0; ts.tv_nsec = 1;              /* rounds up to 1 ms, never 0 */
        cact_sigtimedwait(&set, 0, &ts);
        checks++;
        if ((int32_t)a32(1) != 1) {
            fails++;
            printf("FAIL sigtimedwait rounding: %d ms, want 1\n", (int32_t)a32(1));
        }
        ts.tv_sec = 0; ts.tv_nsec = 1000000000L;    /* out of range */
        cact_host_ctl_calls = 0;
        r = cact_sigtimedwait(&set, 0, &ts);
        checks++;
        if (r != -1 || cact_host_ctl_calls != 0) {
            fails++;
            printf("FAIL sigtimedwait bad timespec: r=%d calls=%d\n",
                   r, cact_host_ctl_calls);
        }

        /* A NULL set is refused without reaching the kernel. */
        cact_host_ctl_calls = 0;
        checks++;
        if (cact_sigwait(0, &sig) != EINVAL || cact_host_ctl_calls != 0) {
            fails++;
            printf("FAIL sigwait(NULL set): calls=%d\n", cact_host_ctl_calls);
        }
        checks++;
        if (cact_sigwait(&set, 0) != EINVAL) {
            fails++;
            printf("FAIL sigwait(NULL sig)\n");
        }
        cact_host_ctl_out_on = 0;
    }

    /* ── sigqueue(): the POSIX number becomes the kernel's index, and the value
     * rides along. ── */
    {
        union cact_sigval v;
        v.sival_int = 0x1234;
        cact_host_ctl_ret = 0;
        cact_host_ctl_calls = 0;
        int r = cact_sigqueue(4321, 15 /* SIGTERM */, v);     /* SIPIPE→index 1 */
        checks++;
        if (r != 0 || cact_host_ctl_calls != 1 ||
            cact_host_ctl_cmd != CACT_PROCCTL_SIGQUEUE ||
            a32(0) != 4321u || a32(1) != 1u || a32(2) != 0x1234u) {
            fails++;
            printf("FAIL sigqueue: r=%d pid=%u sig=%u value=%#x\n",
                   r, a32(0), a32(1), a32(2));
        }
        /* An unrouteable signal number is refused in the libc. */
        cact_host_ctl_calls = 0;
        r = cact_sigqueue(4321, 63, v);
        checks++;
        if (r != -1 || cact_host_ctl_calls != 0) {
            fails++;
            printf("FAIL sigqueue(bogus sig): r=%d calls=%d\n", r, cact_host_ctl_calls);
        }
        /* A kernel failure becomes -1. */
        cact_host_ctl_ret = -3;
        checks++;
        if (cact_sigqueue(4321, 15, v) != -1) {
            fails++;
            printf("FAIL sigqueue error propagation\n");
        }
    }

    printf("signal: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
