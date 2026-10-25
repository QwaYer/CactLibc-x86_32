/* strftime host test: src/strftime.c against glibc's.
 *
 * The same struct tm is formatted by both, so every specifier is compared —
 * including %Z/%z, which now come from the TZ engine, and %s, which goes
 * through mktime.  The tm is built by glibc's localtime_r, i.e. it is a *local*
 * time, which is the only case POSIX defines %z/%Z for. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

size_t cact_strftime(char *s, size_t max, const char *fmt, const void *tm);

static int checks, fails;

static void one(const char *lbl, const char *fmt, const struct tm *tm) {
    char a[256], b[256];
    size_t ra = cact_strftime(a, sizeof a, fmt, tm);
    size_t rb = strftime(b, sizeof b, fmt, tm);
    checks++;
    if (ra != rb || strcmp(a, b)) {
        fails++;
        printf("FAIL %-20s %-14s got(%zu)=\"%s\" want(%zu)=\"%s\"\n", lbl, fmt, ra, a, rb, b);
    }
}

int main(void) {
    static const char *const fmts[] = {
        "%Z", "%z", "%s", "%F %T %z", "%F %T %Z",
        "%c", "%a %b %e %H:%M:%S %Y", "%Y-%m-%d %H:%M:%S",
        "%j %U %W %V %G", "%p %I %r", "%D %x %X %T", "%u %w %y %C",
        "%^c", "%#c", "%^r", "%#r", "%^X", "%^F", "%^D", "%30c", "%^30c", "%#^c",
    };
    static const char *const zones[] = {
        "UTC", "UTC-5", "EST5EDT,M3.2.0,M11.1.0", "CET-1CEST,M3.5.0,M10.5.0/3",
        "AEST-10AEDT,M10.1.0,M4.1.0/3", "XXX-2:30",
    };
    static const long times[] = {
        0, -1, 86400, 946684800, 1600000000, 1690000000, 1700000000, 1710000000,
    };

    for (size_t i = 0; i < sizeof zones / sizeof zones[0]; i++) {
        setenv("TZ", zones[i], 1);
        tzset();
        for (size_t j = 0; j < sizeof times / sizeof times[0]; j++) {
            struct tm tm;
            localtime_r(&times[j], &tm);
            for (size_t k = 0; k < sizeof fmts / sizeof fmts[0]; k++)
                one(zones[i], fmts[k], &tm);
        }
    }

    printf("strftime: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
