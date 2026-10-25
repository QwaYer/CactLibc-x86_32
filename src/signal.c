#include "signal.h"
#include "syscall.h"
#include "nodeio.h"
#include "ioctl_abi.h"   /* the ctl commands and their argument structs */
#include "errno.h"
#include "string.h"
#include <stdint.h>

/* Signal indices/masks — in the kernel representation (bit = index). */

static int sig_number_to_sigindex(int signum) {
    unsigned u = (unsigned)signum;
    if (u > 0 && (u & (u - 1u)) == 0u) {
        int bit = 0;
        unsigned t = u;
        while (t > 1u) {
            t >>= 1;
            bit++;
        }
        if (bit >= 1 && bit < KERNEL_NSIG) return bit;
        return -1;   /* SIGKILL (bit 0) is handled as a forced kill */
    }
    switch (signum) {
    case 1:  return 10;
    case 2:  return 11;
    case 3:  return 12;
    case 13: return 4;
    case 14: return 5;
    case 15: return 1;
    case 17: return 6;
    case 18: return 3;
    case 19: return 2;
    default:
        if (signum >= 1 && signum < KERNEL_NSIG) return signum;
        return -1;
    }
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)oldact;
    if (!act) { errno = EINVAL; return -1; }
    int idx = sig_number_to_sigindex(signum);
    if (idx < 0) { errno = EINVAL; return -1; }
    /* sa_handler and sa_sigaction are two fields here (not the POSIX union),
     * so take whichever one the flags select.  With SA_SIGINFO the kernel
     * builds a siginfo_t and a ucontext_t into the signal frame and enters the
     * routine as void (*)(int, siginfo_t *, void *). */
    uint32_t h = (uint32_t)(uintptr_t)act->sa_handler;
    if ((act->sa_flags & SA_SIGINFO) && act->sa_sigaction)
        h = (uint32_t)(uintptr_t)act->sa_sigaction;

    cact_sigaction_arg_t a;
    a.signum  = (uint32_t)idx;
    a.handler = h;
    /* The flags travel with the disposition; the kernel acts on SA_ONSTACK,
     * SA_SIGINFO and SA_NODEFER. */
    a.flags   = (uint32_t)act->sa_flags;
    /* The set to block while this handler runs.  POSIX also blocks the
     * delivered signal itself, unless SA_NODEFER. */
    a.mask    = (uint32_t)act->sa_mask;
    int r = nio_ctl(CACT_PROCCTL_SIGACTION, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* Alternate signal stack.  The kernel keeps it per task and disables it across
 * exec(); as on Linux, the state cannot be changed while a handler is running
 * on it (EPERM), and SS_ONSTACK is only known to the kernel. */
int sigaltstack(const stack_t *ss, stack_t *old_ss) {
    cact_sigaltstack_arg_t a;
    memset(&a, 0, sizeof a);            /* all zero = query only */

    if (ss) {
        int r = nio_ctl(CACT_PROCCTL_SIGALTSTACK, &a);
        if (r < 0) { errno = -r; return -1; }
        if (a.old_flags & CACT_SS_ONSTACK) { errno = EPERM; return -1; }

        if (ss->ss_flags & ~SS_DISABLE) { errno = EINVAL; return -1; }
        if (ss->ss_flags & SS_DISABLE) {
            a.flags = CACT_SS_DISABLE;
        } else {
            if (!ss->ss_sp) { errno = EINVAL; return -1; }
            if (ss->ss_size < MINSIGSTKSZ) { errno = ENOMEM; return -1; }
            a.sp   = (uint32_t)(uintptr_t)ss->ss_sp;
            a.size = (uint32_t)ss->ss_size;
        }
    }

    int r = nio_ctl(CACT_PROCCTL_SIGALTSTACK, &a);
    if (r < 0) { errno = -r; return -1; }

    if (old_ss) {
        old_ss->ss_sp    = (void *)(uintptr_t)a.old_sp;
        old_ss->ss_size  = (size_t)a.old_size;
        old_ss->ss_flags = (int)(a.old_flags & (CACT_SS_ONSTACK | CACT_SS_DISABLE));
        if (a.old_sp == 0) old_ss->ss_flags |= SS_DISABLE;   /* never installed */
    }
    return 0;
}

int sigreturn(void) {
    return (int)syscall(SYS_SIGRETURN, 0, 0, 0);
}

int sigaddset(sigset_t *set, int signum) {
    if (!set) { errno = EINVAL; return -1; }
    int idx = sig_number_to_sigindex(signum);
    if (idx < 0) { errno = EINVAL; return -1; }
    *set |= (1u << idx);
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set) { errno = EINVAL; return -1; }
    int idx = sig_number_to_sigindex(signum);
    if (idx < 0) { errno = EINVAL; return -1; }
    *set &= ~(1u << idx);
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) { errno = EINVAL; return -1; }
    /* Every kernel signal bit, minus the two that cannot be caught. */
    *set = ((1u << KERNEL_NSIG) - 1u) & ~(unsigned)(SIGKILL | SIGSTOP);
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set) { errno = EINVAL; return -1; }
    int idx = sig_number_to_sigindex(signum);
    if (idx < 0) { errno = EINVAL; return -1; }
    return (*set & (1u << idx)) ? 1 : 0;
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    cact_sigprocmask_arg_t a;
    a.how    = (uint32_t)how;
    a.set    = set ? *set : 0;
    a.oldset = 0;
    int r = nio_ctl(CACT_PROCCTL_SIGPROCMASK, &a);
    if (r < 0) { errno = -r; return -1; }
    if (oldset) *oldset = a.oldset;
    return 0;
}

int sigpending(sigset_t *set) {
    if (!set) { errno = EINVAL; return -1; }
    uint32_t v = 0;
    int r = nio_ctl(CACT_PROCCTL_SIGPENDING, &v);
    if (r < 0) { errno = -r; return -1; }
    *set = v;
    return 0;
}

int sigsuspend(const sigset_t *mask) {
    uint32_t m = mask ? *mask : 0;
    int r = nio_ctl(CACT_PROCCTL_SIGSUSPEND, &m);
    if (r < 0) { errno = -r; return -1; }
    errno = EINTR;
    return -1;
}

unsigned int alarm(unsigned int seconds) {
    uint32_t s = seconds;
    int r = nio_ctl(CACT_PROCCTL_ALARM, &s);
    if (r < 0) { errno = -r; return 0; }
    return (unsigned int)r;
}

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value) {
    if (which != 0) { errno = EINVAL; return -1; }   /* ITIMER_REAL only */

    cact_itimerval_arg_t a;
    a.it_value_ms   = 0;
    a.it_interval_ms = 0;
    if (new_value) {
        if (new_value->it_value.tv_sec < 0 || new_value->it_value.tv_usec < 0 ||
            new_value->it_interval.tv_sec < 0 || new_value->it_interval.tv_usec < 0) {
            errno = EINVAL;
            return -1;
        }
        a.it_value_ms = (uint32_t)(new_value->it_value.tv_sec * 1000) +
                        (uint32_t)(new_value->it_value.tv_usec / 1000);
        a.it_interval_ms = (uint32_t)(new_value->it_interval.tv_sec * 1000) +
                           (uint32_t)(new_value->it_interval.tv_usec / 1000);
    }
    int r = nio_ctl(CACT_PROCCTL_SETITIMER, &a);
    if (r < 0) { errno = -r; return -1; }
    if (old_value) {
        old_value->it_value.tv_sec  = (long)(a.old_value_ms / 1000);
        old_value->it_value.tv_usec = (long)((a.old_value_ms % 1000) * 1000);
        old_value->it_interval.tv_sec  = (long)(a.old_interval_ms / 1000);
        old_value->it_interval.tv_usec = (long)((a.old_interval_ms % 1000) * 1000);
    }
    return 0;
}

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction sa;
    /* Validate the signal, but hand `sigaction()` the *original* number: it
     * translates by itself, so passing the already-converted index made it
     * translate twice — `signal(SIGTERM)` (=2) became `sigaction(1)` which the
     * translator rejects as SIGKILL's slot, so the handler was silently not
     * installed. */
    if (sig_number_to_sigindex(signum) < 0) return SIG_ERR;
    sa.sa_handler = handler;
    if (sigaction(signum, &sa, NULL) < 0) return SIG_ERR;
    return handler;
}

int kill(pid_t pid, int sig) {
    int idx = sig_number_to_sigindex(sig);
    if (idx < 0) idx = 0;   /* SIGKILL / unroutable → task_kill */
    cact_signal_arg_t a;
    a.pid    = (uint32_t)pid;
    a.signum = (uint32_t)idx;
    int r = nio_ctl(CACT_PROCCTL_SIGNAL, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* Signal every process of `pgid` (POSIX killpg).  A shell uses it to resume a
 * stopped job as a whole instead of only its first/last member. */
int killpg(pid_t pgid, int sig) {
    int idx = sig_number_to_sigindex(sig);
    if (idx < 0) idx = 0;
    cact_killpg_arg_t a;
    a.pgid   = (uint32_t)pgid;
    a.signum = (uint32_t)idx;
    int r = nio_ctl(CACT_PROCCTL_KILLPG, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

/* ── the synchronous half: sigwait/sigwaitinfo/sigtimedwait ───────────────
 *
 * The kernel does the waiting and the *consuming* (CACT_PROCCTL_SIGWAIT): a
 * signal in `set` is taken out of the pending set instead of being delivered, so
 * no handler runs — that is what makes sigwait() different from sigsuspend().
 * The kernel hands back the signal's *index*, which becomes one of the SIG*
 * values above, so `case SIGINT:` works on the result.
 *
 * The kernel reports its own error numbers (which are also this libc's), so the
 * wrappers below only have to decide which of the two POSIX conventions each
 * entry point uses: sigwait() returns an error *number*, sigwaitinfo() and
 * sigtimedwait() return the signal number or -1/errno. */

static int sigwait_core(const sigset_t *set, int timeout_ms, int *signum_out,
                        uint32_t *code_out, uint32_t *value_out) {
    cact_sigwait_arg_t a;
    int r;

    if (!set) return EINVAL;
    a.set        = *set;
    a.timeout_ms = timeout_ms;
    a.signum     = 0;
    a.code       = 0;
    a.value      = 0;
    r = nio_ctl(CACT_PROCCTL_SIGWAIT, &a);
    if (r < 0) return -r;                  /* EINVAL, EAGAIN, … */
    if (signum_out) *signum_out = (int)(1u << a.signum);
    if (code_out)   *code_out   = a.code;
    if (value_out)  *value_out  = a.value;
    return 0;
}

int sigwait(const sigset_t *set, int *sig) {
    int s = 0;
    int r;
    if (!sig) return EINVAL;
    r = sigwait_core(set, -1, &s, 0, 0);
    if (r != 0) return r;                  /* POSIX: the error number itself */
    *sig = s;
    return 0;
}

/* Fill in what the kernel reported.  si_addr stays 0: a waited-for signal comes
 * from a sender, not from a fault, so it carries a value rather than an address.
 * (The window's own int is the value, as POSIX's union sigval allows.) */
static void sigwait_fill_info(siginfo_t *info, int signum, uint32_t code,
                              uint32_t value) {
    if (!info) return;
    info->si_signo = signum;
    info->si_code  = (int)code;
    info->si_addr  = 0;
    info->si_value.sival_int = (int)value;
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info) {
    int s = 0;
    uint32_t code = 0, value = 0;
    int r = sigwait_core(set, -1, &s, &code, &value);
    if (r != 0) { errno = r; return -1; }
    sigwait_fill_info(info, s, code, value);
    return s;                              /* POSIX: the signal number */
}

int sigtimedwait(const sigset_t *set, siginfo_t *info,
                 const struct timespec *timeout) {
    int s = 0, ms, r;
    uint32_t code = 0, value = 0;

    if (timeout) {
        long long ns, msv;
        if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
            timeout->tv_nsec >= 1000000000L)
            { errno = EINVAL; return -1; }
        ns  = (long long)timeout->tv_sec * 1000000000LL + timeout->tv_nsec;
        msv = (ns + 999999LL) / 1000000LL;   /* round up: never wait less */
        ms  = (msv > 2147483647LL) ? 2147483647 : (int)msv;
    } else {
        ms = -1;                           /* wait forever */
    }

    r = sigwait_core(set, ms, &s, &code, &value);
    if (r != 0) { errno = r; return -1; }  /* EAGAIN when it timed out */
    sigwait_fill_info(info, s, code, value);
    return s;
}

int sigqueue(pid_t pid, int sig, const union sigval value) {
    cact_sigqueue_arg_t a;
    int idx = sig_number_to_sigindex(sig);
    int r;

    if (idx < 0) { errno = EINVAL; return -1; }
    a.pid    = (uint32_t)pid;
    a.signum = (uint32_t)idx;
    a.value  = (uint32_t)value.sival_int;
    r = nio_ctl(CACT_PROCCTL_SIGQUEUE, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
