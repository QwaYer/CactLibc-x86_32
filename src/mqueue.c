/*
 * POSIX message queues — mq_open() and the rest, over the kernel's queue
 * filesystem at /dev/mqueue.
 *
 * The kernel mounts a queue filesystem there (Cact/fs/mqueue): a queue is a
 * file whose name is the mq name, so mq_open() is an open-or-create and
 * mq_unlink() is an unlink().  Because a message carries a priority in both
 * directions, sending and receiving are ioctls (CACT_MQCTL_SEND/RECEIVE)
 * rather than read()/write().
 *
 * mq_open() asks for the object with O_CREAT|O_EXCL first.  Its success means
 * this call created the queue, which is exactly when the limits from the `attr`
 * argument must be applied (CACT_MQCTL_INIT); a failure falls back to an
 * ordinary open unless the caller asked for O_EXCL.  The descriptor it returns
 * has FD_CLOEXEC set, as POSIX requires.
 *
 * This file is deliberately thin: names, paths, flag/errno translation.  The
 * queue itself — priority order, blocking, the timed deadline — is the kernel's
 * (verified there); the host unit under tests/host checks everything assembled
 * here.
 */

#include "mqueue.h"
#include "ioctl_abi.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "time.h"

#include <stdarg.h>

#define MQ_DIR       "/dev/mqueue"
#define MQ_PATH_MAX  (sizeof(MQ_DIR) + MQ_NAME_MAX + 1)

/* POSIX: the name is "/" followed by a non-empty component with no further
 * '/'.  Anything else ("name", "/", "/a/b") is EINVAL. */
static int mq_name_ok(const char *name) {
    if (!name || name[0] != '/') return 0;
    if (!name[1]) return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] == '/') return 0;
    return 1;
}

static int mq_name_len(const char *name) {
    int n = 0;
    while (name[1 + n]) n++;
    return n;
}

/* "/name" -> "/dev/mqueue/name" (name already validated). */
static void mq_path(char *out, const char *name) {
    int i = 0;
    for (; MQ_DIR[i]; i++) out[i] = MQ_DIR[i];
    out[i++] = '/';
    for (int j = 1; name[j]; j++) out[i++] = name[j];
    out[i] = '\0';
}

/* Convert POSIX's absolute CLOCK_REALTIME timeout to the kernel's relative
 * milliseconds (< 0 = wait forever).  *expired is set when the deadline has
 * already passed, so the caller can turn the kernel's EAGAIN into ETIMEDOUT. */
static int mq_timeout_ms(const struct timespec *abs, int *expired) {
    struct timespec now;
    long long ns;

    *expired = 0;
    if (!abs) return -1;
    if (abs->tv_sec < 0 || abs->tv_nsec < 0 || abs->tv_nsec >= 1000000000L) {
        *expired = 1;
        return 0;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;

    ns = ((long long)abs->tv_sec - (long long)now.tv_sec) * 1000000000LL
       + ((long long)abs->tv_nsec - (long long)now.tv_nsec);
    if (ns <= 0) { *expired = 1; return 0; }
    if (ns > 2147483647000000LL) return 2147483647;      /* clamp to INT_MAX ms */
    return (int)((ns + 999999LL) / 1000000LL);           /* round up: never early */
}

mqd_t mq_open(const char *name, int oflag, ...) {
    mode_t mode = 0;
    struct mq_attr *attr = 0;

    if (!mq_name_ok(name)) { errno = EINVAL; return -1; }
    if (mq_name_len(name) > MQ_NAME_MAX) { errno = ENAMETOOLONG; return -1; }

    /* O_ACCMODE is both bits; neither is a valid access mode on its own. */
    int acc = oflag & O_ACCMODE;
    if (acc == O_ACCMODE) { errno = EINVAL; return -1; }

    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, unsigned);
        attr = va_arg(ap, struct mq_attr *);
        va_end(ap);
    }
    (void)mode;      /* POSIX requires the argument; the kernel derives the mode */

    char path[MQ_PATH_MAX];
    mq_path(path, name);

    /* A queue uses only the access mode and O_NONBLOCK out of the open flags. */
    int open_flags = (oflag & O_ACCMODE) | (oflag & O_NONBLOCK);

    int fd;
    int created = 0;
    if (oflag & O_CREAT) {
        fd = open(path, open_flags | O_CREAT | O_EXCL);
        if (fd >= 0) {
            created = 1;
        } else if (oflag & O_EXCL) {
            /* SYS_OPEN reports every failure as a bare -1 (no errno channel);
             * with O_EXCL the only reason to get here is that it exists. */
            errno = EEXIST;
            return -1;
        } else {
            fd = open(path, open_flags);
            if (fd < 0) { errno = ENOENT; return -1; }
        }
    } else {
        fd = open(path, open_flags);
        if (fd < 0) { errno = ENOENT; return -1; }
    }

    if (created) {
        cact_mq_attr_t a;
        a.mq_flags   = 0;
        a.mq_maxmsg  = MQ_DEFAULT_MAXMSG;
        a.mq_msgsize = MQ_DEFAULT_MSGSIZE;
        a.mq_curmsgs = 0;
        for (int i = 0; i < 4; i++) a.reserved[i] = 0;
        if (attr) {
            a.mq_maxmsg  = (int32_t)attr->mq_maxmsg;
            a.mq_msgsize = (int32_t)attr->mq_msgsize;
        }
        if (a.mq_maxmsg <= 0 || a.mq_maxmsg > MQ_MAXMSG_LIMIT ||
            a.mq_msgsize <= 0 || a.mq_msgsize > MQ_MSGSIZE_LIMIT) {
            close(fd);
            errno = EINVAL;
            return -1;
        }
        int r = ioctl(fd, CACT_MQCTL_INIT, &a);
        if (r < 0) { close(fd); errno = -r; return -1; }
    }

    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int mq_close(mqd_t mqdes) {
    return close(mqdes);
}

int mq_unlink(const char *name) {
    if (!mq_name_ok(name)) { errno = EINVAL; return -1; }
    if (mq_name_len(name) > MQ_NAME_MAX) { errno = ENAMETOOLONG; return -1; }

    char path[MQ_PATH_MAX];
    mq_path(path, name);
    return unlink(path);
}

/* ── send / receive ───────────────────────────────────────────────────────
 * Both share one body: the only difference between the plain and timed forms
 * is where the kernel deadline comes from. */

static int mq_send_common(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                          unsigned msg_prio, const struct timespec *abs) {
    cact_mq_msg_t a;
    int expired = 0, r;

    a.msg_ptr    = (char *)msg_ptr;
    a.msg_len    = (uint32_t)msg_len;
    a.msg_prio   = (uint32_t)msg_prio;
    a.timeout_ms = mq_timeout_ms(abs, &expired);

    r = ioctl(mqdes, CACT_MQCTL_SEND, &a);
    if (r < 0) {
        int e = -r;
        if (expired && e == EAGAIN) e = ETIMEDOUT;
        errno = e;
        return -1;
    }
    return 0;
}

static ssize_t mq_receive_common(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                                 unsigned *msg_prio, const struct timespec *abs) {
    cact_mq_msg_t a;
    int expired = 0, r;

    a.msg_ptr    = msg_ptr;
    a.msg_len    = (uint32_t)msg_len;
    a.msg_prio   = 0;
    a.timeout_ms = mq_timeout_ms(abs, &expired);

    r = ioctl(mqdes, CACT_MQCTL_RECEIVE, &a);
    if (r < 0) {
        int e = -r;
        if (expired && e == EAGAIN) e = ETIMEDOUT;
        errno = e;
        return -1;
    }
    if (msg_prio) *msg_prio = a.msg_prio;
    return (ssize_t)a.msg_len;
}

int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio) {
    return mq_send_common(mqdes, msg_ptr, msg_len, msg_prio, 0);
}

ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio) {
    return mq_receive_common(mqdes, msg_ptr, msg_len, msg_prio, 0);
}

int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                 unsigned msg_prio, const struct timespec *abs_timeout) {
    return mq_send_common(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
}

ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                        unsigned *msg_prio, const struct timespec *abs_timeout) {
    return mq_receive_common(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
}

/* ── attributes ─────────────────────────────────────────────────────────── */

static void mq_attr_from_wire(struct mq_attr *dst, const cact_mq_attr_t *src) {
    dst->mq_flags   = src->mq_flags;
    dst->mq_maxmsg  = src->mq_maxmsg;
    dst->mq_msgsize = src->mq_msgsize;
    dst->mq_curmsgs = src->mq_curmsgs;
    for (int i = 0; i < 4; i++) dst->__reserved[i] = 0;
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr) {
    cact_mq_attr_t a;
    int r;

    if (!attr) { errno = EINVAL; return -1; }
    r = ioctl(mqdes, CACT_MQCTL_GETATTR, &a);
    if (r < 0) { errno = -r; return -1; }
    mq_attr_from_wire(attr, &a);
    return 0;
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr) {
    cact_mq_attr_t a;
    int r;

    if (!newattr) { errno = EINVAL; return -1; }
    a.mq_flags   = (int32_t)newattr->mq_flags;   /* only mq_flags is meaningful */
    a.mq_maxmsg  = 0;
    a.mq_msgsize = 0;
    a.mq_curmsgs = 0;
    for (int i = 0; i < 4; i++) a.reserved[i] = 0;

    r = ioctl(mqdes, CACT_MQCTL_SETATTR, &a);   /* the kernel returns the previous attr */
    if (r < 0) { errno = -r; return -1; }
    if (oldattr) mq_attr_from_wire(oldattr, &a);
    return 0;
}

/* ── notification ───────────────────────────────────────────────────────── */

int mq_notify(mqd_t mqdes, const struct sigevent *sevp) {
    cact_mq_notify_arg_t a;
    int r;

    if (!sevp || sevp->sigev_notify == SIGEV_NONE) {
        a.signo = 0;                                    /* cancel */
    } else if (sevp->sigev_notify == SIGEV_SIGNAL) {
        a.signo = (uint32_t)sevp->sigev_signo;          /* the libc's SIG* mask bit */
    } else {
        errno = EINVAL;                                 /* SIGEV_THREAD unsupported */
        return -1;
    }

    r = ioctl(mqdes, CACT_MQCTL_NOTIFY, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
