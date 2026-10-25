/* time host test: clock_settime()'s argument validation and the /dev/sys
 * request it builds (the kernel side itself is not reachable from here). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec { long tv_sec; long tv_nsec; };

int cact_clock_settime(int clkid, const struct timespec *tp);
int cact_clock_nanosleep(int clkid, int flags, const struct timespec *req,
                         struct timespec *rem);
int *cact_host_errno_ptr(void);          /* the libc's own errno slot */

/* localtime/mktime, with the tz engine linked in (see tests/units.txt). */
struct cact_tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
struct cact_tm *cact_localtime_r(const long *t, struct cact_tm *r);
long cact_mktime(struct cact_tm *tm);
void cact_tzset(void);
void cact_gmtime_r(const long *t, struct cact_tm *r);

extern int           cact_host_devcmd_calls;
extern char          cact_host_devcmd_dev[16];
extern unsigned long cact_host_devcmd_cmd;
extern unsigned int  cact_host_devcmd_sec;
extern unsigned int  cact_host_devcmd_usec;
extern int           cact_host_devcmd_ret;

#define CACT_SYSCTL_SETWALLCLOCK 0x350A

static int checks, fails;

static void reset(void) {
    cact_host_devcmd_calls = 0;
    cact_host_devcmd_dev[0] = '\0';
    cact_host_devcmd_cmd = 0;
    cact_host_devcmd_sec = 0;
    cact_host_devcmd_usec = 0;
    cact_host_devcmd_ret = 0;
}

int main(void) {
    struct timespec ts = { 1700000000, 123456000 };      /* .123456 s */

    /* The request that reaches the kernel: /dev/sys, 0x350A, sec + usec. */
    reset();
    int r = cact_clock_settime(CLOCK_REALTIME, &ts);
    checks++;
    if (r != 0 || cact_host_devcmd_calls != 1 ||
        strcmp(cact_host_devcmd_dev, "sys") ||
        cact_host_devcmd_cmd != CACT_SYSCTL_SETWALLCLOCK ||
        cact_host_devcmd_sec != 1700000000u ||
        cact_host_devcmd_usec != 123456u) {
        fails++;
        printf("FAIL set: r=%d calls=%d dev=%s cmd=%#lx sec=%u usec=%u\n",
               r, cact_host_devcmd_calls, cact_host_devcmd_dev,
               cact_host_devcmd_cmd, cact_host_devcmd_sec, cact_host_devcmd_usec);
    }

    /* A zero nsec must round-trip as usec 0. */
    reset();
    struct timespec ts0 = { 42, 0 };
    cact_clock_settime(CLOCK_REALTIME, &ts0);
    checks++;
    if (cact_host_devcmd_sec != 42u || cact_host_devcmd_usec != 0u) {
        fails++;
        printf("FAIL whole-second: sec=%u usec=%u\n",
               cact_host_devcmd_sec, cact_host_devcmd_usec);
    }

    /* Rejected without a kernel call: the monotonic clock, NULL, bad nsec. */
    struct timespec bad = { 1, 1000000000L };
    *cact_host_errno_ptr() = 0;
    int r1 = cact_clock_settime(CLOCK_MONOTONIC, &ts);
    int e1 = *cact_host_errno_ptr();
    *cact_host_errno_ptr() = 0;
    int r2 = cact_clock_settime(CLOCK_REALTIME, 0);
    int e2 = *cact_host_errno_ptr();
    *cact_host_errno_ptr() = 0;
    int r3 = cact_clock_settime(CLOCK_REALTIME, &bad);
    int e3 = *cact_host_errno_ptr();
    checks++;
    if (r1 != -1 || e1 != EINVAL || r2 != -1 || e2 != EINVAL ||
        r3 != -1 || e3 != EINVAL || cact_host_devcmd_calls != 1) {
        fails++;
        printf("FAIL rejects: r=%d/%d/%d e=%d/%d/%d calls=%d\n",
               r1, r2, r3, e1, e2, e3, cact_host_devcmd_calls);
    }

    /* A kernel failure surfaces as -1 with the mapped errno. */
    reset();
    cact_host_devcmd_ret = -1;                            /* -EPERM */
    int r4 = cact_clock_settime(CLOCK_REALTIME, &ts);
    checks++;
    if (r4 != -1) { fails++; printf("FAIL err propagation: r=%d\n", r4); }

    /* localtime_r against glibc, with TZ varied: same civil fields, same
     * tm_isdst, and mktime() round-trips back to the same instant. */
    {
        static const char *const zones[] = {
            "UTC", "UTC-5", "EST5EDT,M3.2.0,M11.1.0",
            "CET-1CEST,M3.5.0,M10.5.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3",
        };
        static const long times[] = { 0, -1, 86400, 1690000000, 1700000000, 1710000000 };
        for (size_t i = 0; i < sizeof zones / sizeof zones[0]; i++) {
            setenv("TZ", zones[i], 1);
            tzset();                /* glibc's: localtime_r does not re-read TZ */
            cact_tzset();
            for (size_t j = 0; j < sizeof times / sizeof times[0]; j++) {
                struct tm wtm;
                struct cact_tm ctm;
                localtime_r(&times[j], &wtm);
                cact_localtime_r(&times[j], &ctm);
                checks++;
                if (ctm.tm_sec != wtm.tm_sec || ctm.tm_min != wtm.tm_min ||
                    ctm.tm_hour != wtm.tm_hour || ctm.tm_mday != wtm.tm_mday ||
                    ctm.tm_mon != wtm.tm_mon || ctm.tm_year != wtm.tm_year ||
                    ctm.tm_wday != wtm.tm_wday || ctm.tm_yday != wtm.tm_yday ||
                    (ctm.tm_isdst > 0) != (wtm.tm_isdst > 0)) {
                    fails++;
                    printf("FAIL localtime TZ=%s t=%ld got %04d-%02d-%02d %02d:%02d:%02d isdst=%d"
                           " want %04d-%02d-%02d %02d:%02d:%02d isdst=%d\n",
                           zones[i], times[j],
                           ctm.tm_year + 1900, ctm.tm_mon + 1, ctm.tm_mday,
                           ctm.tm_hour, ctm.tm_min, ctm.tm_sec, ctm.tm_isdst,
                           wtm.tm_year + 1900, wtm.tm_mon + 1, wtm.tm_mday,
                           wtm.tm_hour, wtm.tm_min, wtm.tm_sec, wtm.tm_isdst);
                }
                checks++;
                if (times[j] >= 0 && cact_mktime(&ctm) != times[j]) {
                    fails++;
                    printf("FAIL mktime round-trip TZ=%s t=%ld -> %ld\n",
                           zones[i], times[j], cact_mktime(&ctm));
                }
            }
        }
    }

    /* clock_nanosleep(): the validation paths, which run before any syscall (the
     * sleeping itself is the target's).  Note POSIX's oddity, checked here: it
     * *returns* the error number instead of returning -1 and setting errno. */
    {
        struct timespec good = { 0, 0 }, bad = { 0, -1 };
        checks++;
        if (cact_clock_nanosleep(99, 0, &good, 0) != EINVAL) {
            fails++; printf("FAIL clock_nanosleep: a bad clock was not EINVAL\n");
        }
        checks++;
        if (cact_clock_nanosleep(CLOCK_REALTIME, 0x40, &good, 0) != EINVAL) {
            fails++; printf("FAIL clock_nanosleep: an unknown flag was not EINVAL\n");
        }
        checks++;
        if (cact_clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, 0, 0) != EINVAL) {
            fails++; printf("FAIL clock_nanosleep: a NULL request was not EINVAL\n");
        }
        checks++;
        if (cact_clock_nanosleep(CLOCK_MONOTONIC, 0, &bad, 0) != EINVAL) {
            fails++; printf("FAIL clock_nanosleep: a negative tv_nsec was not EINVAL\n");
        }
        *cact_host_errno_ptr() = 0;
        checks++;
        if (cact_clock_nanosleep(99, 0, &good, 0) != EINVAL ||
            *cact_host_errno_ptr() != 0) {
            fails++; printf("FAIL clock_nanosleep: it set errno instead of returning\n");
        }
    }

    printf("time: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
