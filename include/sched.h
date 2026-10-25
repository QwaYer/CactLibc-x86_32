#ifndef _SCHED_H
#define _SCHED_H

#include "unistd.h"          /* pid_t */
#include "time.h"            /* struct timespec (sched_rr_get_interval) */

/* POSIX scheduling.
 *
 * The MLFQ has one time-sharing class, and that is what SCHED_OTHER names, so it
 * is the only supported policy.  SCHED_FIFO and SCHED_RR are defined because a
 * portable <sched.h> has to define them, but they are not supported: the
 * scheduler is a *dynamic* feedback queue and deliberately has no fixed-priority
 * class — a task's band is earned from its own run/sleep behaviour and decays
 * when it hogs the CPU, which a pinned priority would defeat — so
 * sched_setscheduler() and sched_get_priority_*() report EINVAL for them (the
 * failure POSIX specifies for a policy an implementation does not support)
 * rather than accept a priority range it could not honour.  That is also why
 * sched_priority is 0 for SCHED_OTHER, which is the only value POSIX gives it.
 *
 * Nothing here reaches the kernel except sched_yield(): every task already runs
 * in the SCHED_OTHER class, so setting that policy is not a state change.  Only
 * the calling process may be named — another pid gives EPERM, which POSIX allows
 * for a caller without permission. */
struct sched_param {
    int sched_priority;
};

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
int sched_getscheduler(pid_t pid);
int sched_getparam(pid_t pid, struct sched_param *param);
int sched_setparam(pid_t pid, const struct sched_param *param);
int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);
int sched_rr_get_interval(pid_t pid, struct timespec *interval);
int sched_yield(void);

#endif
