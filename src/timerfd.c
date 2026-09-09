#include <timerfd.h>
#include "nodeio.h"
#include "ioctl_abi.h"
#include "errno.h"
#include "string.h"
#include <time.h>
#include <stdint.h>

int timerfd_create(int clockid, int flags) {
    cact_timerfd_create_arg_t a;
    a.clockid = clockid;
    a.flags   = (uint32_t)flags &
                (CACT_TFD_NONBLOCK | CACT_TFD_CLOEXEC);
    int r = nio_dev_cmd("timerfd", CACT_TIMERFDCTL_CREATE, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

static uint32_t _timespec_to_ms(const struct timespec *ts) {
    if (!ts) return 0;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0) return 0;
    return (uint32_t)(ts->tv_sec * 1000u + ts->tv_nsec / 1000000u);
}

static void _ms_to_timespec(struct timespec *ts, uint32_t ms) {
    if (!ts) return;
    ts->tv_sec  = (time_t)(ms / 1000u);
    ts->tv_nsec = (long)(ms % 1000u) * 1000000L;
}

int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value) {
    if (!new_value) { errno = EINVAL; return -1; }

    cact_timerfd_spec_t a;
    memset(&a, 0, sizeof(a));

    /* The kernel clock is a 10 ms monotonic tick; translate an absolute
     * deadline into a relative one here so the kernel only deals with ms. */
    if (flags & TFD_TIMER_ABSTIME) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long sec  = new_value->it_value.tv_sec - now.tv_sec;
        long nsec = new_value->it_value.tv_nsec - now.tv_nsec;
        long ms = sec * 1000L + nsec / 1000000L;
        if (ms < 1) ms = 1;
        a.it_value_ms = (uint32_t)ms;
    } else {
        a.it_value_ms = _timespec_to_ms(&new_value->it_value);
    }
    a.it_interval_ms = _timespec_to_ms(&new_value->it_interval);
    a.flags = 0;   /* always relative to the kernel */

    int r = nio_ioctl(fd, CACT_TIMERFD_SETTIME, &a);
    if (r < 0) { errno = -r; return -1; }

    if (old_value) {
        _ms_to_timespec(&old_value->it_value, a.old_value_ms);
        _ms_to_timespec(&old_value->it_interval, a.old_interval_ms);
    }
    return 0;
}

int timerfd_gettime(int fd, struct itimerspec *curr_value) {
    if (!curr_value) { errno = EINVAL; return -1; }

    cact_timerfd_spec_t a;
    memset(&a, 0, sizeof(a));

    int r = nio_ioctl(fd, CACT_TIMERFD_GETTIME, &a);
    if (r < 0) { errno = -r; return -1; }

    _ms_to_timespec(&curr_value->it_value, a.it_value_ms);
    _ms_to_timespec(&curr_value->it_interval, a.it_interval_ms);
    return 0;
}
