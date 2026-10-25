#include "time.h"
#include "tz.h"
#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
#include <stdint.h>

/* Monotonic time since boot (see /proc/time in the kernel). */

static int _read_time(cact_time_t *t) {
    int r = nio_read_file("/proc/time", t, sizeof(*t));
    return (r == (int)sizeof(*t)) ? 0 : -1;
}

/* Civil time from the RTC (see /proc/wallclock in the kernel).  On a kernel
 * without /proc/wallclock we fall back to monotonic time — no worse than
 * before. */
static int _read_wallclock(cact_time_t *t) {
    int r = nio_read_file("/proc/wallclock", t, sizeof(*t));
    if (r == (int)sizeof(*t)) return 0;
    return _read_time(t);
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return 0;
    cact_time_t t;
    if (_read_wallclock(&t) < 0) return -1;
    tv->tv_sec  = (long)t.sec;
    tv->tv_usec = (long)t.usec;
    return 0;
}

int clock_gettime(int clkid, struct timespec *tp) {
    if (clkid != CLOCK_REALTIME && clkid != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    cact_time_t t;
    int r = (clkid == CLOCK_MONOTONIC) ? _read_time(&t) : _read_wallclock(&t);
    if (r < 0) return -1;
    tp->tv_sec  = (long)t.sec;
    tp->tv_nsec = (long)t.usec * 1000;
    return 0;
}

int clock_getres(int clkid, struct timespec *res) {
    if (clkid != CLOCK_REALTIME && clkid != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    if (res) { res->tv_sec = 0; res->tv_nsec = 1000000; }   /* the kernel's 1 ms timer */
    return 0;
}

int clock_settime(int clkid, const struct timespec *tp) {
    if (clkid != CLOCK_REALTIME) {          /* there is no settable monotonic clock */
        errno = EINVAL;
        return -1;
    }
    if (!tp || tp->tv_sec < 0 || tp->tv_nsec < 0 || tp->tv_nsec >= 1000000000) {
        errno = EINVAL;
        return -1;
    }
    cact_time_t t;
    t.sec  = (uint32_t)tp->tv_sec;
    t.usec = (uint32_t)(tp->tv_nsec / 1000);
    int r = nio_dev_cmd("sys", CACT_SYSCTL_SETWALLCLOCK, &t);
    return nio_map(r);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000) {
        errno = EINVAL;
        return -1;
    }
    unsigned int ms = (unsigned int)(req->tv_sec * 1000) +
                      (unsigned int)((req->tv_nsec + 999999) / 1000000);
    if (ms == 0) ms = 1;
    __syscall3(SYS_POLL, 0, 0, ms);   /* poll(NULL,0,ms) — blocking sleep */
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

/* Like nanosleep(), but like POSIX also: it names the clock to sleep against,
 * may take an absolute deadline (TIMER_ABSTIME), and — unlike every other call
 * here — returns the error number itself instead of setting errno. */
int clock_nanosleep(int clkid, int flags, const struct timespec *req,
                    struct timespec *rem) {
    if (clkid != CLOCK_REALTIME && clkid != CLOCK_MONOTONIC) return EINVAL;
    if (flags & ~TIMER_ABSTIME) return EINVAL;
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000)
        return EINVAL;

    if (flags & TIMER_ABSTIME) {
        struct timespec now, sleep_for;
        long long deadline, now_ns, rel;
        if (clock_gettime(clkid, &now) != 0) return errno;
        deadline = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
        now_ns   = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
        rel      = deadline - now_ns;
        if (rel <= 0) return 0;                    /* already due */
        sleep_for.tv_sec  = (long)(rel / 1000000000LL);
        sleep_for.tv_nsec = (long)(rel % 1000000000LL);
        if (nanosleep(&sleep_for, 0) != 0) return errno;
        return 0;                                  /* rem is unused for an absolute wait */
    }

    if (nanosleep(req, rem) != 0) return errno;
    return 0;
}

static struct tm __gmt;
static struct tm __local;
static char __asctime_buf[32];
static char __ctime_buf[32];

time_t time(time_t *t) {
    struct timeval tv;
    if (gettimeofday(&tv, 0) < 0) return (time_t)-1;
    if (t) *t = tv.tv_sec;
    return tv.tv_sec;
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

static int _isleap(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static void _fill_gmtime(time_t t, struct tm *r) {
    /* Floor division, so times before 1970 do not come out with a negative
     * second / minute / hour. */
    long days = (long)t / 86400;
    long rem  = (long)t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }

    r->tm_sec  = (int)(rem % 60); rem /= 60;
    r->tm_min  = (int)(rem % 60); rem /= 60;
    r->tm_hour = (int)rem;

    {
        long w = (days + 4) % 7;                 /* 1970-01-01 was a Thursday */
        if (w < 0) w += 7;
        r->tm_wday = (int)w;
    }

    long y;
    if (days >= 0) {
        for (y = 1970;; y++) {
            long d = _isleap(y) ? 366 : 365;
            if (days < d) break;
            days -= d;
        }
    } else {
        for (y = 1969;; y--) {
            days += _isleap(y) ? 366 : 365;      /* step into year y */
            if (days >= 0) break;
        }
    }
    r->tm_year = (int)(y - 1900);
    r->tm_yday = (int)days;                      /* day of year, 0-based */

    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m;
    for (m = 0; m < 12; m++) {
        long d = mdays[m];
        if (m == 1 && _isleap(y)) d = 29;
        if (days < d) break;
        days -= d;
    }
    r->tm_mon  = m;
    r->tm_mday = (int)days + 1;
    r->tm_isdst = 0;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (!timep || !result) return 0;
    _fill_gmtime(*timep, result);
    return result;
}

struct tm *gmtime(const time_t *timep) {
    return gmtime_r(timep, &__gmt);
}

/* Local time is civil time plus the zone's offset at that instant; the zone
 * comes from TZ (src/tz.c). */
struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (!timep || !result) return 0;
    int isdst = 0;
    long east = cact_tz_offset_utc((long)*timep, &isdst);
    time_t shifted = (time_t)((long)*timep + east);
    if (!gmtime_r(&shifted, result)) return 0;
    result->tm_isdst = isdst;
    return result;
}

struct tm *localtime(const time_t *timep) {
    return localtime_r(timep, &__local);
}

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian), for any year. */
static long _days_from_civil(long y, long m, long d) {
    y -= (m <= 2) ? 1 : 0;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp  = (unsigned)((m + 9) % 12);
    unsigned doy = (153u * mp + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon;
    int d = tm->tm_mday;
    if (m < 0 || m > 11 || d < 1 || d > 31 || y < 1) return (time_t)-1;

    time_t t = (time_t)cact_tz_utc_from_tm(tm);

    long days = _days_from_civil(y, m + 1, d);
    tm->tm_wday = (int)(((days + 4) % 7 + 7) % 7);
    tm->tm_yday = (int)(days - _days_from_civil(y, 1, 1));

    int isdst = 0;
    cact_tz_offset_utc((long)t, &isdst);
    tm->tm_isdst = isdst;
    return t;
}

static void _app_str(char *buf, int *p, const char *s) {
    while (*s) buf[(*p)++] = *s++;
}

static void _app_int(char *buf, int *p, int v, int width) {
    char tmp[16];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + v % 10; v /= 10; }
    while (n < width) { buf[(*p)++] = '0'; width--; }
    while (n > 0) buf[(*p)++] = tmp[--n];
}

char *asctime_r(const struct tm *tm, char *buf) {
    static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    int p = 0;
    _app_str(buf, &p, wd[tm->tm_wday & 7]);
    buf[p++] = ' ';
    _app_str(buf, &p, mo[tm->tm_mon]);
    buf[p++] = ' ';
    _app_int(buf, &p, tm->tm_mday, 2);
    buf[p++] = ' ';
    _app_int(buf, &p, tm->tm_hour, 2);
    buf[p++] = ':';
    _app_int(buf, &p, tm->tm_min, 2);
    buf[p++] = ':';
    _app_int(buf, &p, tm->tm_sec, 2);
    buf[p++] = ' ';
    _app_int(buf, &p, tm->tm_year + 1900, 4);
    buf[p++] = '\n';
    buf[p] = '\0';
    return buf;
}

char *asctime(const struct tm *tm) {
    return asctime_r(tm, __asctime_buf);
}

char *ctime_r(const time_t *timep, char *buf) {
    struct tm tmv;
    if (!gmtime_r(timep, &tmv)) return 0;
    return asctime_r(&tmv, buf);
}

char *ctime(const time_t *timep) {
    return ctime_r(timep, __ctime_buf);
}
