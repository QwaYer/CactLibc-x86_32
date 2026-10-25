/* pthread host test: the scheduling half of the attribute and the per-thread
 * scheduling calls — all of which can only validate, because the one class this
 * scheduler has is SCHED_OTHER (see <sched.h>).  Creating a thread is not
 * exercised here (it needs the kernel trap); what is under test is the contract
 * those calls expose.
 *
 * The libc's <pthread.h>/<sched.h> cannot be included (their <unistd.h> typedefs
 * a 32-bit pid_t that clashes with the system headers), so the pieces used are
 * mirrored, as in the other units. */

#include <stdio.h>
#include <string.h>

struct sched_param { int sched_priority; };
typedef unsigned int pthread_t;

#define PTHREAD_INHERIT_SCHED   0
#define PTHREAD_EXPLICIT_SCHED  1
#define PTHREAD_SCOPE_SYSTEM    0
#define PTHREAD_SCOPE_PROCESS   1
#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

/* The libc's pthread_attr_t: policy, inheritsched, and the param. */
struct cact_attr { int policy; int inheritsched; struct sched_param param; };

int cact_pthread_attr_init(struct cact_attr *attr);
int cact_pthread_attr_destroy(struct cact_attr *attr);
int cact_pthread_attr_setinheritsched(struct cact_attr *attr, int inheritsched);
int cact_pthread_attr_getinheritsched(const struct cact_attr *attr, int *inheritsched);
int cact_pthread_attr_setschedpolicy(struct cact_attr *attr, int policy);
int cact_pthread_attr_getschedpolicy(const struct cact_attr *attr, int *policy);
int cact_pthread_attr_setschedparam(struct cact_attr *attr, const struct sched_param *param);
int cact_pthread_attr_getschedparam(const struct cact_attr *attr, struct sched_param *param);
int cact_pthread_attr_setscope(struct cact_attr *attr, int scope);
int cact_pthread_attr_getscope(const struct cact_attr *attr, int *scope);
int cact_pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param);
int cact_pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param);
int cact_pthread_setschedprio(pthread_t thread, int prio);
int cact_pthread_create(pthread_t *thread, const struct cact_attr *attr,
                        void *(*start_routine)(void *), void *arg);

#define EINVAL 22
#define ESRCH  3

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void *noop(void *a) { (void)a; return 0; }

int main(void) {
    struct cact_attr attr;
    struct sched_param sp, out;
    int v = -1;

    /* Defaults: POSIX starts a thread with INHERIT_SCHED, so the policy/param
     * below are inert until EXPLICIT_SCHED is asked for. */
    CK(cact_pthread_attr_init(&attr) == 0, "attr_init");
    CK(cact_pthread_attr_getinheritsched(&attr, &v) == 0 && v == PTHREAD_INHERIT_SCHED,
       "default inheritsched is %d", v);
    CK(cact_pthread_attr_getschedpolicy(&attr, &v) == 0 && v == SCHED_OTHER,
       "default policy is %d", v);
    memset(&out, 0xff, sizeof out);
    CK(cact_pthread_attr_getschedparam(&attr, &out) == 0 && out.sched_priority == 0,
       "default priority is %d", out.sched_priority);
    CK(cact_pthread_attr_getscope(&attr, &v) == 0 && v == PTHREAD_SCOPE_SYSTEM,
       "default scope is %d", v);

    /* Round trips. */
    sp.sched_priority = 0;
    CK(cact_pthread_attr_setschedparam(&attr, &sp) == 0, "attr_setschedparam");
    sp.sched_priority = 3;
    CK(cact_pthread_attr_setschedparam(&attr, &sp) == 0, "attr_setschedparam(3)");
    memset(&out, 0, sizeof out);
    CK(cact_pthread_attr_getschedparam(&attr, &out) == 0 && out.sched_priority == 3,
       "attr param round trip (%d)", out.sched_priority);
    CK(cact_pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0, "attr_setschedpolicy(FIFO)");
    CK(cact_pthread_attr_getschedpolicy(&attr, &v) == 0 && v == SCHED_FIFO,
       "attr policy round trip (%d)", v);

    CK(cact_pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == 0, "explicit");
    CK(cact_pthread_attr_setinheritsched(&attr, 7) == EINVAL, "a bogus inheritsched");
    CK(cact_pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM) == 0, "scope system");
    CK(cact_pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS) == EINVAL,
       "PTHREAD_SCOPE_PROCESS was accepted");

    CK(cact_pthread_attr_init(NULL) == EINVAL, "attr_init(NULL)");
    CK(cact_pthread_attr_setschedparam(&attr, NULL) == EINVAL, "setschedparam(NULL)");
    CK(cact_pthread_attr_getschedpolicy(NULL, &v) == EINVAL, "getpolicy(NULL)");
    CK(cact_pthread_attr_destroy(&attr) == 0, "attr_destroy");

    /* pthread_create() refuses a class it cannot honour — but only once the
     * attribute is actually in force (EXPLICIT_SCHED); with the POSIX default,
     * INHERIT_SCHED, the policy/param are inert.  Only the paths that return
     * before any thread is set up are callable here: creating a thread is the
     * kernel's trap, which a host test has none of. */
    {
        pthread_t t = 0;
        struct cact_attr a;
        struct sched_param p1 = { 1 };

        cact_pthread_attr_init(&a);
        a.policy = SCHED_FIFO; a.param = p1;
        a.inheritsched = PTHREAD_EXPLICIT_SCHED;
        CK(cact_pthread_create(&t, &a, noop, 0) == EINVAL,
           "an unhourable policy under EXPLICIT_SCHED was accepted");
        a.policy = SCHED_OTHER;
        CK(cact_pthread_create(&t, &a, noop, 0) == EINVAL,
           "a non-zero priority under EXPLICIT_SCHED was accepted");
        CK(cact_pthread_create(&t, &a, 0, 0) == EINVAL, "a NULL start routine");
    }

    /* The per-thread calls: thread 0 means "the calling thread" here, and any
     * id the registry does not know is ESRCH. */
    sp.sched_priority = 0;
    CK(cact_pthread_setschedparam(0, SCHED_OTHER, &sp) == 0, "setschedparam(self)");
    CK(cact_pthread_setschedparam(0, SCHED_FIFO, &sp) == EINVAL, "setschedparam(FIFO)");
    sp.sched_priority = 1;
    CK(cact_pthread_setschedparam(0, SCHED_OTHER, &sp) == EINVAL, "setschedparam(prio 1)");
    sp.sched_priority = 0;
    CK(cact_pthread_setschedparam(0, SCHED_OTHER, NULL) == EINVAL, "setschedparam(NULL)");

    memset(&out, 0xff, sizeof out);
    v = -1;
    CK(cact_pthread_getschedparam(0, &v, &out) == 0 &&
       v == SCHED_OTHER && out.sched_priority == 0,
       "getschedparam(self) gave policy %d priority %d", v, out.sched_priority);
    CK(cact_pthread_getschedparam(0, 0, &out) == EINVAL, "getschedparam(NULL policy)");

    CK(cact_pthread_setschedprio(0, 0) == 0, "setschedprio(0)");
    CK(cact_pthread_setschedprio(0, 5) == EINVAL, "setschedprio(5)");

    /* An unknown thread id. */
    CK(cact_pthread_setschedparam((pthread_t)0x7ffffffeu, SCHED_OTHER, &sp) == ESRCH,
       "setschedparam of an unknown thread");
    CK(cact_pthread_getschedparam((pthread_t)0x7ffffffeu, &v, &out) == ESRCH,
       "getschedparam of an unknown thread");
    CK(cact_pthread_setschedprio((pthread_t)0x7ffffffeu, 0) == ESRCH,
       "setschedprio of an unknown thread");

    printf("pthread: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
