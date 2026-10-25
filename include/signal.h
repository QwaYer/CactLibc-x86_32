#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <stddef.h>
#include <stdint.h>
#include "time.h"
#include "unistd.h"

typedef uint32_t sigset_t;

/* how values for sigprocmask */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/*
 * Signal bitmasks — each signal occupies one bit.
 * These values are used with sigprocmask/sigpending/sigsuspend
 * and match the kernel's internal representation.
 */
#define SIGKILL  (1u << 0)
#define SIGTERM  (1u << 1)
#define SIGSTOP  (1u << 2)
#define SIGCONT  (1u << 3)
#define SIGPIPE  (1u << 4)
#define SIGALRM  (1u << 5)
#define SIGCHLD  (1u << 6)
#define SIGFPE   (1u << 7)
#define SIGSEGV  (1u << 8)
#define SIGWINCH (1u << 9)
#define SIGHUP   (1u << 10)   /* hangup — terminal closed          */
#define SIGINT   (1u << 11)   /* interrupt — Ctrl-C from TTY       */
#define SIGQUIT  (1u << 12)   /* quit     — Ctrl-\ from TTY        */

/* ioctl requests for terminal window size */
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* itimer types for setitimer/getitimer */
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
    struct timeval it_interval; /* interval for periodic timer */
    struct timeval it_value;    /* time until next expiry      */
};

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)(uintptr_t)-1)

#define WNOHANG 1

/* Additional POSIX signal numbers (kernel supports bits 0-12 only) */
#ifndef SIGHUP
#  define SIGHUP    10
#endif
#ifndef SIGINT
#  define SIGINT    11
#endif
#ifndef SIGQUIT
#  define SIGQUIT   12
#endif
#define SIGILL    13
#define SIGABRT   14
#define SIGBUS    15

/* SA_SIGINFO: sigaction flag for sa_sigaction handler.
 * The kernel builds the siginfo_t and ucontext_t into the signal frame, so the
 * handler is entered as void handler(int, siginfo_t *, void *).  si_code is
 * SI_USER for a sent signal and SI_KERNEL for one the kernel generated (a
 * fault), and si_addr is then the address that faulted. */
#define SA_SIGINFO 0x0004
#define SA_NODEFER 0x40000000
/* Run this handler on the alternate signal stack (see sigaltstack below).
 * The value matches CACT_SA_ONSTACK in ioctl_abi.h. */
#define SA_ONSTACK 0x08000000

/* FPE signal code constants */
#define FPE_INTDIV 1
#define FPE_FLTDIV 2

/* sigqueue()'s payload: POSIX writes it as `union sigval`, and the kernel
 * carries it (as one word) with the pending signal, so both a sigwaitinfo() and
 * an SA_SIGINFO handler read it back as si_value. */
union sigval {
    int   sival_int;
    void *sival_ptr;
};

typedef struct {
    int   si_signo;
    int   si_code;
    void *si_addr;
    union sigval si_value;
} siginfo_t;

/* ── sigevent ─────────────────────────────────────────────────────────────
 * POSIX's event-delivery descriptor, used by mq_notify(2) (and, in time, the
 * *_async() family).  Only SIGEV_SIGNAL and SIGEV_NONE are honoured: a
 * SIGEV_SIGNAL notification delivers sigev_signo to the process, and
 * SIGEV_THREAD is refused with EINVAL.  The layout matches glibc, so a
 * caller compiled against either header passes the same thing. */
#define SIGEV_SIGNAL 0      /* deliver sigev_signo to the process */
#define SIGEV_NONE   1      /* do not notify */
#define SIGEV_THREAD 2      /* not supported on this kernel */

struct sigevent {
    union sigval sigev_value;
    int          sigev_signo;
    int          sigev_notify;
    union {
        int _tid;                                  /* SIGEV_THREAD_ID (unused) */
        struct {
            void (*_function)(union sigval);       /* SIGEV_THREAD (unused)   */
            void  *_attribute;
        } _sigev_thread;
    } _sigev_un;
};

typedef struct sigevent sigevent_t;

/* ── alternate signal stack ───────────────────────────────────────────────
 * A stack the kernel switches to for handlers installed with SA_ONSTACK — the
 * way a SIGSEGV from stack overflow gets a chance to report anything.  The
 * kernel carries the state per task and disables it across exec(). */
typedef struct {
    void  *ss_sp;
    int    ss_flags;
    size_t ss_size;
} stack_t;

#define SS_ONSTACK 1        /* a handler is running on it right now */
#define SS_DISABLE 2        /* not in use */
#define MINSIGSTKSZ 2048    /* matches CACT_MINSIGSTKSZ */

int sigaltstack(const stack_t *ss, stack_t *old_ss);

#define sigemptyset(set)    (*(set) = 0)

/* Every signal a process can block — all of them except the two that cannot be
 * caught (SIGKILL, SIGSTOP). */
int sigfillset(sigset_t *set);

/* sigaddset/sigdelset/sigismember translate their signum argument like
 * sigaction()/kill() do, so either kernel bitmask constants (SIGTERM etc.)
 * or classic numbers are accepted. */
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

/* Index in the kernel handler table (same mask bit number for delivered signals). */
#define KERNEL_NSIG 13

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void         (*sa_sigaction)(int, siginfo_t *, void *);
};

int          sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int          sigreturn(void);
int          sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int          sigpending(sigset_t *set);
int          sigsuspend(const sigset_t *mask);
unsigned int alarm(unsigned int seconds);
int          setitimer(int which, const struct itimerval *new_value,
                       struct itimerval *old_value);

sighandler_t signal(int signum, sighandler_t handler);
int          kill(pid_t pid, int sig);
int          killpg(pid_t pgid, int sig);
int          raise(int sig);

/* The synchronous half of POSIX signals: wait for one of `set` and *consume* it
 * — no handler runs, `sig` comes back as the signal's number (the same value the
 * SIG* macros above hold, so `case SIGINT:` works).  POSIX has the caller block
 * the signals in `set` first.  All three return 0 or an error number directly
 * (they do not set errno); sigtimedwait() also reports EAGAIN on timeout, and a
 * NULL timeout waits forever.  SIGKILL/SIGSTOP cannot be waited for (they can be
 * neither blocked nor caught): they are ignored if present in `set`. */
int sigwait(const sigset_t *set, int *sig);
int sigwaitinfo(const sigset_t *set, siginfo_t *info);
int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout);

/* Like kill(), but the signal carries `value`, which the receiver reads back as
 * si_value (si_code SI_QUEUE).  A second sigqueue() for a signal that is already
 * pending does not queue another instance — POSIX's rule for non-realtime
 * signals, and the only behaviour this kernel has (a value is one word per
 * pending signal). */
int sigqueue(pid_t pid, int sig, const union sigval value);

#endif /* _SIGNAL_H */