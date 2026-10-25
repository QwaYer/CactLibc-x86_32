/* semaphore host test: src/semaphore.c + src/futex.c against a fake kernel.
 *
 * The real test of `pshared` is what the libc asks the kernel for: a waiter in
 * another process is only reachable if the wait is keyed by the word's physical
 * page (CACT_FUTEX_WAIT_SHARED) rather than by (this address space, address).
 * The shim plays that kernel — it records the request, and can play "another
 * process posted while we slept", which is what lets a blocking wait be
 * exercised end to end here.  The kernel's own page-table walk and the real
 * cross-process rendezvous are not reachable from a host test (they need the
 * kernel and two address spaces); they are covered by the kernel build and the
 * on-target boot check.
 *
 * What is checked below: the userspace state machine (decrement never goes
 * below zero, EAGAIN, getvalue, the error paths), which futex operation each
 * call picks for each mode, the -EAGAIN retry a lost race causes, and
 * sem_timedwait's timeout arithmetic in both directions.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>                              /* the system's: clock_gettime */
#include "../../include/semaphore.h"           /* our sem_t */

/* The unit's entry points, renamed (see rename_semaphore.h). */
int  cact_sem_init(sem_t *sem, int pshared, unsigned int value);
int  cact_sem_destroy(sem_t *sem);
int  cact_sem_wait(sem_t *sem);
int  cact_sem_trywait(sem_t *sem);
int  cact_sem_timedwait(sem_t *sem, const struct timespec *abstime);
int  cact_sem_post(sem_t *sem);
int  cact_sem_getvalue(sem_t *sem, int *sval);
int *cact_host_errno_ptr(void);                /* the libc's own errno slot */

/* The fake kernel (tests/host/shim.c). */
extern int          cact_host_ctl_fd_on;
extern int          cact_host_ioctl_calls;
extern int          cact_host_futex_op;
extern unsigned int cact_host_futex_uaddr;
extern int          cact_host_futex_val;
extern int          cact_host_futex_timeout;
extern int          cact_host_futex_waits;
extern void        *cact_host_futex_word;
extern int          cact_host_futex_post;
extern int          cact_host_futex_eagain_first;
extern int          cact_host_futex_wait_ret;

/* The op codes, mirrored from include/ioctl_abi.h: this mirror is the contract
 * the libc has to honour. */
#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_WAIT_SHARED 2
#define FUTEX_WAKE_SHARED 3

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static int *errp(void) { return cact_host_errno_ptr(); }

static void reset(void) {
    cact_host_ioctl_calls = 0;
    cact_host_futex_op = -1;
    cact_host_futex_uaddr = 0;
    cact_host_futex_val = 0;
    cact_host_futex_timeout = 0;
    cact_host_futex_waits = 0;
    cact_host_futex_post = 0;
    cact_host_futex_eagain_first = 0;
    cact_host_futex_wait_ret = -4;             /* -EINTR */
    *errp() = 0;
}

/* One semaphore per mode, each with the shim pointed at its word. */
static sem_t priv, shared;

static void arm(sem_t *s, int pshared, int value) {
    reset();
    CK(cact_sem_init(s, pshared, (unsigned)value) == 0, "sem_init(%d,%d) failed", pshared, value);
    CK(s->count == value, "sem_init left count %d, want %d", s->count, value);
    CK(s->pshared == (pshared ? 1 : 0), "sem_init left pshared %d", s->pshared);
    cact_host_futex_word = (void *)&s->count;  /* what the fake kernel posts to */
}

int main(void) {
    cact_host_ctl_fd_on = 1;                   /* the fd path src/futex.c uses */

    /* ── argument checks ─────────────────────────────────────────────────── */
    reset();
    CK(cact_sem_init(NULL, 0, 1) == -1 && *errp() == EINVAL, "sem_init(NULL)");
    CK(cact_sem_destroy(NULL) == -1 && *errp() == EINVAL, "sem_destroy(NULL)");
    CK(cact_sem_wait(NULL) == -1 && *errp() == EINVAL, "sem_wait(NULL)");
    CK(cact_sem_trywait(NULL) == -1 && *errp() == EINVAL, "sem_trywait(NULL)");
    CK(cact_sem_post(NULL) == -1 && *errp() == EINVAL, "sem_post(NULL)");
    CK(cact_sem_getvalue(NULL, &(int){0}) == -1 && *errp() == EINVAL, "sem_getvalue(NULL)");
    CK(cact_sem_getvalue(&priv, NULL) == -1 && *errp() == EINVAL, "sem_getvalue(sem,NULL)");
    CK(cact_sem_timedwait(&priv, NULL) == -1 && *errp() == EINVAL, "sem_timedwait(NULL time)");

    /* ── the userspace state machine ─────────────────────────────────────── */
    arm(&priv, 0, 3);
    {
        int v = -1;
        CK(cact_sem_getvalue(&priv, &v) == 0 && v == 3, "getvalue = %d, want 3", v);
        /* A wait with a value available never reaches the kernel. */
        CK(cact_sem_wait(&priv) == 0 && priv.count == 2, "wait did not decrement");
        CK(cact_host_ioctl_calls == 0, "a wait on a non-zero semaphore called the kernel");
        CK(cact_sem_trywait(&priv) == 0 && priv.count == 1, "trywait did not decrement");
        CK(cact_sem_trywait(&priv) == 0 && priv.count == 0, "trywait did not decrement");
        /* ... and one with no value left reports EAGAIN without blocking. */
        *errp() = 0;
        CK(cact_sem_trywait(&priv) == -1 && *errp() == EAGAIN, "trywait at zero");
        CK(priv.count == 0, "trywait at zero changed the count");
        CK(cact_host_ioctl_calls == 0, "trywait called the kernel");
        /* post only increments and wakes: the value is never negative. */
        CK(cact_sem_post(&priv) == 0 && priv.count == 1, "post did not increment");
        CK(cact_host_ioctl_calls == 1 && cact_host_futex_op == FUTEX_WAKE &&
           cact_host_futex_val == 1, "private post asked for op %d (calls %d)",
           cact_host_futex_op, cact_host_ioctl_calls);
        CK(cact_host_futex_uaddr == (unsigned int)(uintptr_t)&priv.count,
           "post woke the wrong word: %#x", cact_host_futex_uaddr);
        CK(cact_sem_destroy(&priv) == 0, "sem_destroy failed");
    }

    /* ── which key the kernel is asked for, per mode ─────────────────────── */
    arm(&shared, 1, 0);                        /* pshared: the physical-page key */
    cact_host_futex_post = 1;                  /* the other process posts first */
    CK(cact_sem_wait(&shared) == 0, "a shared blocking wait did not succeed");
    CK(cact_host_futex_op == FUTEX_WAIT_SHARED,
       "a pshared wait asked for op %d, want WAIT_SHARED", cact_host_futex_op);
    CK(cact_host_futex_val == 0, "the wait's expected value was %d", cact_host_futex_val);
    CK(cact_host_futex_uaddr == (unsigned int)(uintptr_t)&shared.count,
       "the wait named the wrong word: %#x", cact_host_futex_uaddr);
    CK(shared.count == 0, "the post's increment was not consumed (count %d)", shared.count);
    CK(cact_host_futex_waits == 1, "the shared wait slept %d times", cact_host_futex_waits);

    reset();
    CK(cact_sem_post(&shared) == 0 && shared.count == 1, "shared post did not increment");
    CK(cact_host_futex_op == FUTEX_WAKE_SHARED,
       "a pshared post asked for op %d, want WAKE_SHARED", cact_host_futex_op);

    arm(&priv, 0, 0);                          /* the same, private: not shared */
    cact_host_futex_post = 1;
    CK(cact_sem_wait(&priv) == 0, "a private blocking wait did not succeed");
    CK(cact_host_futex_op == FUTEX_WAIT,
       "an unshared wait asked for op %d, want WAIT", cact_host_futex_op);

    /* The racy interleaving the loop exists for: a post lands between the
     * decrement attempt and the sleep, so the first wait reports -EAGAIN and
     * the loop has to retry rather than return. */
    arm(&shared, 1, 0);
    cact_host_futex_post = 1;
    cact_host_futex_eagain_first = 1;
    CK(cact_sem_wait(&shared) == 0, "a wait whose first sleep was EAGAIN failed");
    CK(cact_host_futex_waits == 2, "the EAGAIN retry did not happen (%d waits)",
       cact_host_futex_waits);
    CK(shared.count == 0, "the EAGAIN retry lost the post (count %d)", shared.count);

    /* ── errors out of the wait ──────────────────────────────────────────── */
    arm(&shared, 1, 0);
    CK(cact_sem_wait(&shared) == -1 && *errp() == EINTR, "an interrupted wait");
    CK(cact_host_futex_op == FUTEX_WAIT_SHARED, "the interrupted wait used op %d",
       cact_host_futex_op);

    arm(&shared, 1, 0);
    cact_host_futex_wait_ret = -12;             /* -ENOMEM out of the wait table */
    CK(cact_sem_wait(&shared) == -1 && *errp() == EINVAL,
       "a kernel error other than EAGAIN/EINTR");

    /* ── sem_timedwait ───────────────────────────────────────────────────── */
    arm(&shared, 1, 0);
    {
        struct timespec now, past = {0, 0};
        CK(clock_gettime(CLOCK_REALTIME, &now) == 0, "clock_gettime failed");
        past.tv_sec = now.tv_sec - 1;
        CK(cact_sem_timedwait(&shared, &past) == -1 && *errp() == ETIMEDOUT,
           "a deadline in the past");
        CK(cact_host_ioctl_calls == 0, "an expired deadline still slept");
    }

    arm(&shared, 1, 0);
    cact_host_futex_post = 1;
    {
        struct timespec now, soon;
        CK(clock_gettime(CLOCK_REALTIME, &now) == 0, "clock_gettime failed");
        soon.tv_sec  = now.tv_sec + 1;
        soon.tv_nsec = now.tv_nsec;
        CK(cact_sem_timedwait(&shared, &soon) == 0, "a timed wait that was posted");
        CK(cact_host_futex_op == FUTEX_WAIT_SHARED, "the timed wait used op %d",
           cact_host_futex_op);
        /* ~1000 ms to the deadline, never more (the clock can only have moved
         * forward since the deadline was computed). */
        CK(cact_host_futex_timeout > 900 && cact_host_futex_timeout <= 1000,
           "the timed wait slept for %d ms, want ~1000", cact_host_futex_timeout);
    }

    arm(&shared, 1, 0);
    cact_host_futex_wait_ret = -110;            /* the kernel says -ETIMEDOUT */
    {
        struct timespec now, soon;
        CK(clock_gettime(CLOCK_REALTIME, &now) == 0, "clock_gettime failed");
        soon.tv_sec  = now.tv_sec + 1;
        soon.tv_nsec = now.tv_nsec;
        CK(cact_sem_timedwait(&shared, &soon) == -1 && *errp() == ETIMEDOUT,
           "a wait the kernel timed out");
    }

    printf("semaphore: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
