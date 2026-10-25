#include "semaphore.h"
#include "futex.h"
#include "errno.h"
#include "time.h"

/* Counting semaphore on a futex word.  The kernel's FUTEX_WAIT is keyed by
 * (address space, uaddr), so the same algorithm works for any sem_t in this
 * process's memory.  A semaphore in memory another process maps too (pshared)
 * uses the kernel's *shared* futex instead, keyed by the word's physical page:
 * the mode has to be stored in the semaphore itself, because the other process
 * never called sem_init() and still has to pick the same key. */

static int sem_try_decr(sem_t *sem) {
    int c = __atomic_load_n((int *)&sem->count, __ATOMIC_ACQUIRE);
    while (c > 0) {
        int expected = c;
        if (__atomic_compare_exchange_n((int *)&sem->count, &expected, c - 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        c = expected;               /* lost the race: retry with the new value */
    }
    return -1;                      /* the value was 0 */
}

/* Sleep on the word until someone posts.  `timeout_ms` <= 0 waits forever. */
static int sem_futex_wait(sem_t *sem, int timeout_ms) {
    volatile int *word = &sem->count;
    if (sem->pshared) return cact_futex_wait_shared(word, 0, timeout_ms);
    return cact_futex_wait(word, 0, timeout_ms);
}

static void sem_futex_wake(sem_t *sem) {
    volatile int *word = &sem->count;
    if (sem->pshared) cact_futex_wake_shared(word, 1);
    else              cact_futex_wake(word, 1);
}

int sem_init(sem_t *sem, int pshared, unsigned int value) {
    if (!sem) { errno = EINVAL; return -1; }
    sem->count   = (int)value;
    sem->pshared = pshared ? 1 : 0;
    return 0;
}

int sem_destroy(sem_t *sem) {
    if (!sem) { errno = EINVAL; return -1; }
    return 0;
}

int sem_wait(sem_t *sem) {
    if (!sem) { errno = EINVAL; return -1; }

    for (;;) {
        if (sem_try_decr(sem) == 0) return 0;

        /* The value is 0: sleep until a post changes the word.  A post that
         * lands between the decrement attempt and the wait makes FUTEX_WAIT
         * return -EAGAIN, and the loop then just decrements it. */
        int r = sem_futex_wait(sem, -1);
        if (r == -EINTR) { errno = EINTR; return -1; }
        if (r < 0 && r != -EAGAIN) { errno = EINVAL; return -1; }
    }
}

int sem_trywait(sem_t *sem) {
    if (!sem) { errno = EINVAL; return -1; }
    if (sem_try_decr(sem) == 0) return 0;
    errno = EAGAIN;
    return -1;
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime) {
    if (!sem || !abstime) { errno = EINVAL; return -1; }

    for (;;) {
        if (sem_try_decr(sem) == 0) return 0;

        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) { errno = EINVAL; return -1; }

        long ms = (abstime->tv_sec - now.tv_sec) * 1000L +
                  (abstime->tv_nsec - now.tv_nsec) / 1000000L;
        if (ms <= 0) { errno = ETIMEDOUT; return -1; }
        if (ms > 0x7fffffffL) ms = 0x7fffffffL;

        int r = sem_futex_wait(sem, (int)ms);
        if (r == -ETIMEDOUT) { errno = ETIMEDOUT; return -1; }
        if (r == -EINTR) { errno = EINTR; return -1; }
        if (r < 0 && r != -EAGAIN) { errno = EINVAL; return -1; }
    }
}

int sem_post(sem_t *sem) {
    if (!sem) { errno = EINVAL; return -1; }
    __atomic_fetch_add((int *)&sem->count, 1, __ATOMIC_RELEASE);
    sem_futex_wake(sem);
    return 0;
}

int sem_getvalue(sem_t *sem, int *sval) {
    if (!sem || !sval) { errno = EINVAL; return -1; }
    *sval = __atomic_load_n((int *)&sem->count, __ATOMIC_ACQUIRE);
    return 0;
}
