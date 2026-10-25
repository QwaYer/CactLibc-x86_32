/*
 * POSIX scheduling — see <sched.h> for what is and is not supported.
 *
 * There is one policy, SCHED_OTHER, and one priority, 0.  A task is already in
 * that class when it is created, so sched_setscheduler()/sched_setparam() only
 * have to validate their arguments; nothing has to be told to the kernel.  The
 * one operation that does reach the kernel is sched_yield(), which asks the
 * scheduler to hand the CPU to another runnable task.
 */

#include "sched.h"
#include "errno.h"
#include "nodeio.h"
#include "ioctl_abi.h"

/* POSIX's sched_* calls name a process; 0 is the calling process.  Naming
 * another one needs a privilege this libc does not model, so it is EPERM. */
static int names_self(pid_t pid) {
    return pid == 0 || pid == getpid();
}

static int policy_supported(int policy) {
    return policy == SCHED_OTHER;
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) {
    if (pid < 0 || !param) { errno = EINVAL; return -1; }
    if (!policy_supported(policy)) { errno = EINVAL; return -1; }
    if (param->sched_priority != 0) { errno = EINVAL; return -1; }
    if (!names_self(pid)) { errno = EPERM; return -1; }
    return 0;
}

int sched_getscheduler(pid_t pid) {
    if (pid < 0) { errno = EINVAL; return -1; }
    if (!names_self(pid)) { errno = EPERM; return -1; }
    return SCHED_OTHER;                         /* the only policy there is */
}

int sched_setparam(pid_t pid, const struct sched_param *param) {
    if (pid < 0 || !param) { errno = EINVAL; return -1; }
    if (param->sched_priority != 0) { errno = EINVAL; return -1; }
    if (!names_self(pid)) { errno = EPERM; return -1; }
    return 0;
}

int sched_getparam(pid_t pid, struct sched_param *param) {
    if (pid < 0 || !param) { errno = EINVAL; return -1; }
    if (!names_self(pid)) { errno = EPERM; return -1; }
    param->sched_priority = 0;
    return 0;
}

int sched_get_priority_max(int policy) {
    if (!policy_supported(policy)) { errno = EINVAL; return -1; }
    return 0;
}

int sched_get_priority_min(int policy) {
    if (!policy_supported(policy)) { errno = EINVAL; return -1; }
    return 0;
}

int sched_rr_get_interval(pid_t pid, struct timespec *interval) {
    if (pid < 0 || !interval) { errno = EINVAL; return -1; }
    /* POSIX defines this only for SCHED_RR, and SCHED_RR is not supported here
     * (see <sched.h>), so there is no round-robin interval to report. */
    errno = EINVAL;
    return -1;
}

int sched_yield(void) {
    return nio_map(nio_ctl(CACT_PROCCTL_YIELD, 0));
}
