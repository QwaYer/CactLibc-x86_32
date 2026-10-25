#ifndef _MQUEUE_H
#define _MQUEUE_H

#include <stddef.h>
#include "unistd.h"     /* ssize_t, mode_t, size_t */
#include "time.h"       /* struct timespec */
#include "signal.h"     /* struct sigevent */
#include "fcntl.h"      /* O_*, F_SETFD */

/* POSIX message queues — mq_open() and the family — on the Cact kernel.
 *
 * The kernel mounts a queue filesystem at /dev/mqueue (Cact/fs/mqueue): its
 * files *are* the queues, so mq_open() is an open-or-create of
 * "/dev/mqueue/<name>" and mq_unlink() removes that name.  A message carries a
 * priority in both directions, so mq_send()/mq_receive() are ioctls on the
 * descriptor rather than write()/read().  A queue unlinked while still open
 * stays alive until the last close, as POSIX requires.
 *
 * Blocking is the kernel's cooperative wait: mq_receive() on an empty queue
 * (and mq_send() on a full one) sleeps until a message arrives or space frees,
 * broken with EINTR when a catchable signal is pending.  O_NONBLOCK on the
 * descriptor (from mq_open() or fcntl()) turns each into an immediate EAGAIN,
 * and the mq_timed*() forms return ETIMEDOUT.
 *
 * Limits (matching the kernel's ioctl_abi.h): a name is "/" plus one component
 * of at most MQ_NAME_MAX characters, a queue holds at most MQ_MAXMSG_LIMIT
 * messages of at most MQ_MSGSIZE_LIMIT bytes, and priorities are
 * 0..MQ_PRIO_MAX-1.  All queues' messages share one kernel byte budget. */

typedef int mqd_t;

struct mq_attr {
    long mq_flags;      /* O_NONBLOCK (the only flag setattr can change) */
    long mq_maxmsg;     /* queue depth                                     */
    long mq_msgsize;    /* maximum message size in bytes                   */
    long mq_curmsgs;    /* messages currently queued (output only)         */
    long __reserved[4]; /* glibc's spare words, kept for ABI familiarity   */
};

#define MQ_PRIO_MAX        32
#define MQ_MAXMSG_LIMIT    256
#define MQ_MSGSIZE_LIMIT   8192
#define MQ_NAME_MAX        127
/* What mq_open() uses when `attr` is NULL (the Linux defaults). */
#define MQ_DEFAULT_MAXMSG  10
#define MQ_DEFAULT_MSGSIZE 8192

/* Opens (or, with O_CREAT, creates) the queue `name`.  With O_CREAT, two more
 * arguments follow — mode_t mode and struct mq_attr *attr (attr may be NULL) —
 * exactly as POSIX specifies.  The descriptor has FD_CLOEXEC set. */
mqd_t mq_open(const char *name, int oflag, ...);

/* Closes the descriptor.  The queue survives if another descriptor or its name
 * still refers to it. */
int   mq_close(mqd_t mqdes);

/* Removes the name.  A queue still open stays alive until the last close. */
int   mq_unlink(const char *name);

/* Sends `msg_len` bytes at priority `msg_prio`.  Returns 0, or -1 with errno:
 * EMSGSIZE if longer than the queue's mq_msgsize, EAGAIN when full and
 * non-blocking, EINVAL for a bad priority, ETIMEDOUT on the timed form. */
int   mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio);

/* Receives the highest-priority message into a buffer of at least mq_msgsize
 * bytes; *msg_prio (if not NULL) gets its priority.  Returns the message length
 * or -1: EMSGSIZE if the buffer is too small, EAGAIN when empty and
 * non-blocking, ETIMEDOUT on the timed form. */
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio);

/* As above, waiting no longer than the absolute CLOCK_REALTIME `abs_timeout`
 * (NULL means wait forever). */
int     mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                     unsigned msg_prio, const struct timespec *abs_timeout);
ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                        unsigned *msg_prio, const struct timespec *abs_timeout);

int   mq_getattr(mqd_t mqdes, struct mq_attr *attr);

/* Sets the descriptor's flags (only O_NONBLOCK in practice).  `oldattr`, when
 * not NULL, receives the previous attributes.  POSIX forbids changing
 * mq_maxmsg/mq_msgsize, so they are ignored. */
int   mq_setattr(mqd_t mqdes, const struct mq_attr *newattr,
                 struct mq_attr *oldattr);

/* Registers (sevp with SIGEV_SIGNAL) or cancels (sevp NULL or SIGEV_NONE)
 * delivery of a signal when a message arrives on a queue the process left
 * empty.  Returns -1/EINVAL for SIGEV_THREAD, -1/EBUSY if already registered. */
int   mq_notify(mqd_t mqdes, const struct sigevent *sevp);

#endif /* _MQUEUE_H */
