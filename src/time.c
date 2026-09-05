#include "time.h"
#include "nodeio.h"
#include "syscall.h"
#include "errno.h"
#include <stdint.h>

/* Монотонное время с загрузки (см. /proc/time в ядре). */

static int _read_time(cact_time_t *t) {
    int r = nio_read_file("/proc/time", t, sizeof(*t));
    return (r == (int)sizeof(*t)) ? 0 : -1;
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return 0;
    cact_time_t t;
    if (_read_time(&t) < 0) return -1;
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
    if (_read_time(&t) < 0) return -1;
    tp->tv_sec  = (long)t.sec;
    tp->tv_nsec = (long)t.usec * 1000;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000) {
        errno = EINVAL;
        return -1;
    }
    unsigned int ms = (unsigned int)(req->tv_sec * 1000) +
                      (unsigned int)((req->tv_nsec + 999999) / 1000000);
    if (ms == 0) ms = 1;
    __syscall3(SYS_POLL, 0, 0, ms);   /* poll(NULL,0,ms) — блокирующий sleep */
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}

static struct tm __gmt;
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
    int days = (int)(t / 86400);
    int rem  = (int)(t % 86400);
    r->tm_sec = rem % 60; rem /= 60;
    r->tm_min = rem % 60; rem /= 60;
    r->tm_hour = rem;
    r->tm_wday = (days + 4) % 7;
    int y, m;
    for (y = 1970;; y++) {
        int d = _isleap(y) ? 366 : 365;
        if (days < d) break;
        days -= d;
    }
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (m = 0; m < 12; m++) {
        int d = mdays[m];
        if (m == 1 && _isleap(y)) d = 29;
        if (days < d) break;
        days -= d;
    }
    r->tm_year = y - 1900;
    r->tm_mon  = m;
    r->tm_mday = days + 1;
    r->tm_yday = (int)(t / 86400);
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

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    return gmtime_r(timep, result);
}

struct tm *localtime(const time_t *timep) {
    return gmtime(timep);
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon;
    int d = tm->tm_mday;
    if (m < 0 || m > 11 || d < 1 || d > 31 || y < 1970) return (time_t)-1;

    long days = 0;
    for (int yy = 1970; yy < y; yy++) days += _isleap(yy) ? 366 : 365;
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (int mm = 0; mm < m; mm++) {
        days += mdays[mm];
        if (mm == 1 && _isleap(y)) days++;
    }
    days += d - 1;

    time_t t = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;

    tm->tm_wday = (int)((days + 4) % 7);
    int yday = d - 1;
    for (int mm = 0; mm < m; mm++) {
        yday += mdays[mm];
        if (mm == 1 && _isleap(y)) yday++;
    }
    tm->tm_yday = yday;
    tm->tm_isdst = 0;
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
