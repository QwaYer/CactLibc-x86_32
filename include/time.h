#ifndef _TIME_H
#define _TIME_H

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct itimerspec {
    struct timespec it_interval;   /* period between expirations         */
    struct timespec it_value;      /* time until first expiration        */
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define TIMER_ABSTIME   1

time_t time(time_t *t);
double difftime(time_t time1, time_t time0);
struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime(const time_t *timep);
char *ctime_r(const time_t *timep, char *buf);
int gettimeofday(struct timeval *tv, void *tz);
int clock_gettime(int clkid, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);

#endif
