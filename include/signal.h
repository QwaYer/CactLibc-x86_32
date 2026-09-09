#ifndef _SIGNAL_H
#define _SIGNAL_H

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

/* SA_SIGINFO: sigaction flag for sa_sigaction handler */
#define SA_SIGINFO 0x0004
#define SA_NODEFER 0x40000000

/* FPE signal code constants */
#define FPE_INTDIV 1
#define FPE_FLTDIV 2

typedef struct {
    int   si_signo;
    int   si_code;
    void *si_addr;
} siginfo_t;

#define sigemptyset(set)    (*(set) = 0)

/* sigaddset/sigdelset/sigismember translate their signum argument like
 * sigaction()/kill() do, so either kernel bitmask constants (SIGTERM etc.)
 * or classic numbers are accepted. */
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

/* Индекс в таблице обработчиков ядра (тот же номер бита маски для доставленных сигналов). */
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
int          raise(int sig);

#endif /* _SIGNAL_H */