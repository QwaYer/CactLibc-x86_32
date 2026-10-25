/* sched host test: the shipped src/sched.c.
 *
 * CactOS has one policy, SCHED_OTHER, so most of this is the refusal contract
 * (EINVAL for a policy or a priority it cannot honour, EPERM for another pid);
 * sched_yield() is checked through the shim's nio_ctl recorder.  The libc's own
 * headers cannot be included here (their <unistd.h> typedefs a 32-bit pid_t that
 * clashes with the system headers), so the contract is mirrored — and that
 * mirrored prototype is exactly what is under test. */

#include <stdio.h>
#include <string.h>
#include <time.h>                /* glibc's: struct timespec */
#include <unistd.h>              /* glibc's: getpid */

struct sched_param { int sched_priority; };

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

int cact_sched_setscheduler(pid_t pid, int policy, const struct sched_param *p);
int cact_sched_getscheduler(pid_t pid);
int cact_sched_setparam(pid_t pid, const struct sched_param *p);
int cact_sched_getparam(pid_t pid, struct sched_param *p);
int cact_sched_get_priority_max(int policy);
int cact_sched_get_priority_min(int policy);
int cact_sched_rr_get_interval(pid_t pid, struct timespec *interval);
int cact_sched_yield(void);

int *cact_host_errno_ptr(void);
extern int           cact_host_ctl_calls;
extern unsigned long cact_host_ctl_cmd;

#define CACT_PROCCTL_YIELD 0x3219

#define EPERM  1
#define EINVAL 22

#define E (*cact_host_errno_ptr())

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

int main(void) {
    struct sched_param sp0, sp1, out;
    pid_t me = getpid();

    /* the one supported policy */
    CK(cact_sched_getscheduler(0) == SCHED_OTHER, "getscheduler(0)");
    CK(cact_sched_getscheduler(me) == SCHED_OTHER, "getscheduler(self)");

    sp0.sched_priority = 0;
    sp1.sched_priority = 1;

    CK(cact_sched_setscheduler(0, SCHED_OTHER, &sp0) == 0, "setscheduler OTHER");
    CK(cact_sched_setscheduler(me, SCHED_OTHER, &sp0) == 0, "setscheduler OTHER self");

    /* an unsupported policy, and a priority only SCHED_OTHER forbids */
    CK(cact_sched_setscheduler(0, SCHED_FIFO, &sp1) == -1 && E == EINVAL,
       "SCHED_FIFO accepted (%d)", E);
    CK(cact_sched_setscheduler(0, SCHED_RR, &sp1) == -1 && E == EINVAL,
       "SCHED_RR accepted (%d)", E);
    CK(cact_sched_setscheduler(0, 99, &sp0) == -1 && E == EINVAL,
       "a bogus policy accepted (%d)", E);
    CK(cact_sched_setscheduler(0, SCHED_OTHER, &sp1) == -1 && E == EINVAL,
       "a non-zero SCHED_OTHER priority accepted (%d)", E);
    CK(cact_sched_setscheduler(0, SCHED_OTHER, NULL) == -1 && E == EINVAL,
       "a NULL param accepted (%d)", E);
    CK(cact_sched_setscheduler(-1, SCHED_OTHER, &sp0) == -1 && E == EINVAL,
       "a negative pid accepted (%d)", E);

    /* another process needs a privilege this libc does not model */
    CK(cact_sched_setscheduler(me + 1, SCHED_OTHER, &sp0) == -1 && E == EPERM,
       "another pid accepted (%d)", E);
    CK(cact_sched_getscheduler(me + 1) == -1 && E == EPERM,
       "getscheduler(another) accepted (%d)", E);
    CK(cact_sched_getscheduler(-1) == -1 && E == EINVAL,
       "getscheduler(-1) accepted (%d)", E);

    /* the priority range is the single SCHED_OTHER value */
    CK(cact_sched_get_priority_max(SCHED_OTHER) == 0, "priority_max(OTHER)");
    CK(cact_sched_get_priority_min(SCHED_OTHER) == 0, "priority_min(OTHER)");
    CK(cact_sched_get_priority_max(SCHED_FIFO) == -1 && E == EINVAL,
       "priority_max(FIFO) accepted (%d)", E);
    CK(cact_sched_get_priority_min(SCHED_RR) == -1 && E == EINVAL,
       "priority_min(RR) accepted (%d)", E);
    CK(cact_sched_get_priority_max(7) == -1 && E == EINVAL,
       "priority_max(bogus) accepted (%d)", E);

    CK(cact_sched_setparam(0, &sp0) == 0, "setparam 0");
    CK(cact_sched_setparam(0, &sp1) == -1 && E == EINVAL, "setparam prio 1 (%d)", E);
    CK(cact_sched_setparam(0, NULL) == -1 && E == EINVAL, "setparam NULL (%d)", E);

    memset(&out, 0xff, sizeof out);
    CK(cact_sched_getparam(0, &out) == 0 && out.sched_priority == 0,
       "getparam prio %d", out.sched_priority);
    CK(cact_sched_getparam(me + 1, &out) == -1 && E == EPERM, "getparam other (%d)", E);
    CK(cact_sched_getparam(0, NULL) == -1 && E == EINVAL, "getparam NULL (%d)", E);

    /* sched_yield is the one call that reaches the kernel */
    {
        int before = cact_host_ctl_calls;
        cact_host_ctl_cmd = 0;
        CK(cact_sched_yield() == 0, "sched_yield failed");
        CK(cact_host_ctl_calls == before + 1 && cact_host_ctl_cmd == CACT_PROCCTL_YIELD,
           "sched_yield sent cmd %#lx", cact_host_ctl_cmd);
    }

    /* sched_rr_get_interval exists but POSIX defines it only for SCHED_RR,
     * which is not supported, so it reports EINVAL. */
    {
        struct timespec ts;
        CK(cact_sched_rr_get_interval(0, &ts) == -1 && E == EINVAL,
           "rr_get_interval accepted SCHED_OTHER (%d)", E);
        CK(cact_sched_rr_get_interval(0, NULL) == -1 && E == EINVAL,
           "rr_get_interval(NULL) accepted (%d)", E);
        CK(cact_sched_rr_get_interval(-1, &ts) == -1 && E == EINVAL,
           "rr_get_interval(-1) accepted (%d)", E);
    }

    printf("sched: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
