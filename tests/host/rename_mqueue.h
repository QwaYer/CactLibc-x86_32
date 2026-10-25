/* Host-test renames for src/mqueue.c.
 *
 * mqueue.c is a thin layer over the libc's own open()/close()/unlink()/fcntl()/
 * ioctl()/clock_gettime(), so this unit builds it against recording stubs for
 * those six instead of a kernel.  That lets the test check exactly what the
 * layer assembled — the /dev/mqueue path, the O_CREAT|O_EXCL creation dance,
 * the CACT_MQCTL_* ioctls with their arguments, the FD_CLOEXEC, and the errno
 * it derives — which is the whole of its behaviour.  The kernel half (the
 * mounted queue filesystem, real blocking, real priorities) needs a boot and is
 * exercised there.
 *
 * The mq_* entry points are renamed too: glibc has all of them. */
#ifndef CACT_HOST_RENAME_MQUEUE_H
#define CACT_HOST_RENAME_MQUEUE_H

#undef errno
#define __errno_location cact_host_errno_location

#define mq_open         cact_mq_open
#define mq_close        cact_mq_close
#define mq_unlink       cact_mq_unlink
#define mq_send         cact_mq_send
#define mq_receive      cact_mq_receive
#define mq_timedsend    cact_mq_timedsend
#define mq_timedreceive cact_mq_timedreceive
#define mq_getattr      cact_mq_getattr
#define mq_setattr      cact_mq_setattr
#define mq_notify       cact_mq_notify

/* The six entry points mqueue.c is built on, redirected to the recording stubs
 * in tests/host/mqueue_test.c. */
#define open          cact_mq_host_open
#define close         cact_mq_host_close
#define unlink        cact_mq_host_unlink
#define fcntl         cact_mq_host_fcntl
#define ioctl         cact_mq_host_ioctl
#define clock_gettime cact_mq_host_clock_gettime

#endif
