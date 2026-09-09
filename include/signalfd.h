#ifndef _SYS_SIGNALFD_H
#define _SYS_SIGNALFD_H

#include <stdint.h>
#include <fcntl.h>
#include <signal.h>

/* Linux signalfd(2) API as served by CactOS via /dev/signalfd + ioctls. */

#ifndef SFD_CLOEXEC
#define SFD_CLOEXEC O_CLOEXEC
#endif
#ifndef SFD_NONBLOCK
#define SFD_NONBLOCK O_NONBLOCK
#endif

/* Wire layout produced by the kernel signalfd node (128 bytes total).
 * ssi_signo is the kernel signal bit index (0..12). */
struct signalfd_siginfo {
    uint32_t ssi_signo;       /* 0   kernel signal bit index */
    int32_t  ssi_errno;       /* 4   */
    int32_t  ssi_code;        /* 8   */
    uint32_t ssi_pid;         /* 12  */
    uint32_t ssi_uid;         /* 16  */
    int32_t  ssi_fd;          /* 20  */
    uint32_t ssi_tid;         /* 24  */
    uint32_t ssi_band;        /* 28  */
    uint32_t ssi_overrun;     /* 32  */
    uint32_t ssi_trapno;      /* 36  */
    int32_t  ssi_status;      /* 40  */
    int32_t  ssi_int;         /* 44  */
    uint64_t ssi_ptr;         /* 48  */
    uint64_t ssi_utime;       /* 56  */
    uint64_t ssi_stime;       /* 64  */
    uint64_t ssi_addr;        /* 72  */
    uint16_t ssi_addr_lsb;    /* 80  */
    uint16_t __pad2;          /* 82  */
    int32_t  ssi_syscall;     /* 84  */
    uint64_t ssi_rt_timerid;  /* 88  */
    uint64_t ssi_sigev_value; /* 96  */
    int32_t  ssi_syscall_arch;/* 104 */
    uint8_t  __pad[20];       /* 108 */
};                            /* total 128 */

/* fd >= 0: update an existing signalfd's mask. fd == -1: create a new one. */
int signalfd(int fd, const sigset_t *mask, int flags);

#endif /* _SYS_SIGNALFD_H */
