/* Host-test renames for src/signal.c (see rename_tz.h for why they exist). */
#ifndef CACT_HOST_RENAME_SIGNAL_H
#define CACT_HOST_RENAME_SIGNAL_H

#undef errno
#define __errno_location cact_host_errno_location

#define sigaction   cact_sigaction
#define sigaltstack cact_sigaltstack
#define sigreturn   cact_sigreturn
#define sigaddset   cact_sigaddset
#define sigdelset   cact_sigdelset
#define sigfillset  cact_sigfillset
#define sigismember cact_sigismember
#define raise       cact_raise
#define sigprocmask cact_sigprocmask
#define sigpending  cact_sigpending
#define sigsuspend  cact_sigsuspend
#define setitimer   cact_setitimer
#define kill        cact_kill
#define killpg      cact_killpg
#define sigwait     cact_sigwait
#define sigwaitinfo cact_sigwaitinfo
#define sigtimedwait cact_sigtimedwait
#define sigqueue    cact_sigqueue

#endif
