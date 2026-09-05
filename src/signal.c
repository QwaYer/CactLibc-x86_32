#include "signal.h"
#include "syscall.h"
#include "nodeio.h"
#include "errno.h"
#include <stdint.h>

/* Индексы/маски сигналов — в представлении ядра (бит = индекс). */

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
        return -1;   /* SIGKILL (bit 0) обрабатывается как принудительный kill */
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
    cact_sigaction_arg_t a;
    a.signum  = (uint32_t)idx;
    a.handler = (uint32_t)(uintptr_t)act->sa_handler;
    int r = nio_ctl(CACT_PROCCTL_SIGACTION, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int sigreturn(void) {
    return (int)syscall(SYS_SIGRETURN, 0, 0, 0);
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
    int idx = sig_number_to_sigindex(signum);
    if (idx < 0) return SIG_ERR;
    sa.sa_handler = handler;
    if (sigaction(idx, &sa, NULL) < 0) return SIG_ERR;
    return handler;
}

int kill(pid_t pid, int sig) {
    int idx = sig_number_to_sigindex(sig);
    if (idx < 0) idx = 0;   /* SIGKILL / немаршрутизируемый → task_kill */
    cact_signal_arg_t a;
    a.pid    = (uint32_t)pid;
    a.signum = (uint32_t)idx;
    int r = nio_ctl(CACT_PROCCTL_SIGNAL, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
