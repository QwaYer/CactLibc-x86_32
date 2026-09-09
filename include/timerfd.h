#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H

#include <stdint.h>
#include <time.h>
#include <fcntl.h>

/* Linux timerfd(2) API as served by CactOS via /dev/timerfd + ioctls. */

#define TFD_TIMER_ABSTIME 1

#ifndef TFD_CLOEXEC
#define TFD_CLOEXEC O_CLOEXEC
#endif
#ifndef TFD_NONBLOCK
#define TFD_NONBLOCK O_NONBLOCK
#endif

int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *curr_value);

#endif /* _SYS_TIMERFD_H */
