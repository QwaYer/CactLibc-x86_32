#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include "time.h"

/* POSIX unnamed semaphores on the Cact kernel's futex.
 *
 * `count` is the futex word: sem_post() increments it and wakes one waiter;
 * sem_wait() decrements it and, at zero, futex-waits on the word itself.  That
 * is a real blocking semaphore — it never counts below zero and it reports
 * EINTR when a signal interrupts the wait.  (It used to be a set of macros
 * doing `--*s`/`++*s`, which silently ran negative and never blocked.)
 *
 * A semaphore in memory shared with another process (`pshared != 0`, e.g. a
 * shmget/shmat segment) waits on the *shared* futex instead — keyed by the
 * word's physical page, so both processes rendezvous on it.  `pshared` is kept
 * in the semaphore itself, because the process that did not call sem_init()
 * still has to know which key to use; that means the whole segment must be
 * initialised by sem_init() before it is shared, and it must stay mapped while
 * anyone is waiting on it (see CACT_FUTEX_WAIT_SHARED in ioctl_abi.h). */

typedef struct {
    volatile int count;
    int          pshared;   /* 1 = lives in memory shared with other processes */
} sem_t;

/* Initialises `sem` to `value`.  `pshared != 0` marks it as living in shared
 * memory: the futex is then keyed by the word's physical page, so another
 * process mapping the same page waits on the same word.  Returns 0 or -1
 * (errno set). */
int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);

/* Blocks while the value is 0.  Returns 0, or -1 with errno == EINTR when a
 * signal arrives first. */
int sem_wait(sem_t *sem);

/* Non-blocking decrement: 0, or -1 with errno == EAGAIN when the value is 0. */
int sem_trywait(sem_t *sem);

/* Decrement, waiting at most until `abstime` (CLOCK_REALTIME).  Returns 0, or
 * -1 with errno == ETIMEDOUT/EINTR. */
int sem_timedwait(sem_t *sem, const struct timespec *abstime);

/* Increments and wakes one waiter.  Returns 0 (never fails for a valid sem). */
int sem_post(sem_t *sem);

/* Stores the current value in *sval.  Returns 0. */
int sem_getvalue(sem_t *sem, int *sval);

#endif /* _SEMAPHORE_H */
