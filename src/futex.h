/* Internal futex and control-channel helpers shared by pthread.c and
 * semaphore.c.  Not a public header: it lives next to the sources so
 * `#include "futex.h"` resolves without widening the installed include set. */

#ifndef CACT_FUTEX_INTERNAL_H
#define CACT_FUTEX_INTERNAL_H

/* One CACT_PROCCTL_* ioctl on this process's control node.  The fd is opened
 * on first use and kept for the lifetime of the process.  Returns the kernel's
 * return value (a negative errno on failure). */
int cact_ctl(unsigned long cmd, void *arg);

/* FUTEX_WAIT keyed by (address space, uaddr): sleep until the word no longer
 * equals `expected`, or `timeout_ms` elapses (<= 0 waits forever).  Returns 0,
 * or a negative kernel error (-EAGAIN, -ETIMEDOUT, -EINTR). */
int cact_futex_wait(volatile int *uaddr, int expected, int timeout_ms);

/* FUTEX_WAKE: wake up to `count` waiters on uaddr (0x7fffffff = all).
 * Returns the number woken, or a negative kernel error. */
int cact_futex_wake(volatile int *uaddr, int count);

/* The same two operations keyed by the word's PHYSICAL page instead, so a
 * semaphore in memory shared with another process rendezvous across processes
 * (see CACT_FUTEX_WAIT_SHARED in ioctl_abi.h).  The page must stay mapped while
 * a waiter is queued. */
int cact_futex_wait_shared(volatile int *uaddr, int expected, int timeout_ms);
int cact_futex_wake_shared(volatile int *uaddr, int count);

#endif /* CACT_FUTEX_INTERNAL_H */
