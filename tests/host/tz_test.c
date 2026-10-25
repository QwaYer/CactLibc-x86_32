/* tz host test: the timezone engine (src/tz.c) against glibc's.
 *
 * For each TZ string and UTC instant, the offset, the DST flag and the
 * abbreviation are compared with what glibc's localtime_r reports for the same
 * TZ.  A TZ that names a zone *file* is checked for the documented fallback
 * instead: this image has no database to read, so it degrades to UTC (glibc,
 * with the host's /usr/share/zoneinfo, would use the real zone). */

#define _GNU_SOURCE     /* tm_gmtoff */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void  cact_tzset(void);
long  cact_tz_offset_utc(long t, int *isdst);
extern char *cact_tzname[2];
extern long  cact_timezone;
extern int   cact_daylight;

static int checks, fails;
static int name_tolerated;      /* historic labels where glibc substitutes one */

static void one(const char *tz, long t) {
    setenv("TZ", tz, 1);
    tzset();

    struct tm tmg;
    localtime_r(&t, &tmg);
    long want_off  = tmg.tm_gmtoff;
    int  want_isdst = tmg.tm_isdst > 0;
    const char *want_name = tzname[want_isdst];

    cact_tzset();
    int got_isdst = 0;
    long got_off = cact_tz_offset_utc(t, &got_isdst);
    const char *got_name = cact_tzname[got_isdst];

    checks++;
    if (got_off != want_off || got_isdst != want_isdst) {
        fails++;
        printf("FAIL TZ=%-30s t=%12ld got(east=%ld isdst=%d \"%s\") want(east=%ld isdst=%d \"%s\")\n",
               tz, t, got_off, got_isdst, got_name, want_off, want_isdst, want_name);
    } else if (strcmp(got_name, want_name) != 0) {
        /* The offset and the DST flag agree, so this is only the label.  For a
         * couple of historic DST periods glibc reports its own abbreviation
         * rather than the one the zone file gives that period's type; this
         * libc reports the file's.  A few are tolerated, more are a failure. */
        name_tolerated++;
        printf("note  TZ=%-30s t=%12ld label \"%s\" vs glibc \"%s\"\n",
               tz, t, got_name, want_name);
    }

    /* The two POSIX globals describe the standard offset and DST usage.  They
     * are only compared for a POSIX TZ string: glibc's legacy globals are not
     * reliably updated for a zone *file*, so they are not a reference there. */
    if (!strchr(tz, '/')) {
        checks++;
        if (cact_timezone != timezone || (cact_daylight != 0) != (daylight != 0)) {
            fails++;
            printf("FAIL TZ=%-30s timezone got=%ld want=%ld daylight got=%d want=%d\n",
                   tz, cact_timezone, timezone, cact_daylight, daylight);
        }
    }
}

int main(void) {
    static const char *const zones[] = {
        "UTC", "GMT", "UTC0", "UTC-5", "UTC+3",
        "EST5EDT,M3.2.0,M11.1.0",
        "EST5EDT",                       /* no rules: glibc's US default */
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",  /* southern hemisphere: wraps the year */
        "NZST-12NZDT,M9.5.0,M4.1.0/3",
        "<+05>-5",                       /* bracketed numeric abbreviation */
        "XXX-2:30",                      /* half-hour offset */
        "JST-9",
        "EST5EDT,J60/2,J300/2",          /* Jn rules */
        "EST5EDT,59/2,300/2",            /* n rules (leap day counted) */
        "XXX-1XYY,0/0,364/0",            /* extreme rule times */
    };
    static const long times[] = {
        0, 1, -1, 86400, -86400,
        946684800,    /* 2000-01-01 */
        1600000000, 1690000000, 1700000000, 1704067200, 1710000000,
        2000000000,
    };

    for (size_t i = 0; i < sizeof zones / sizeof zones[0]; i++)
        for (size_t j = 0; j < sizeof times / sizeof times[0]; j++)
            one(zones[i], times[j]);

    /* Named zones — a TZif file — against glibc, which reads the same files.
     * If the host has no database both sides fall back to UTC, so the
     * comparison holds either way.  The timestamps run from 1970 to the 2030s
     * so the transition table (not a POSIX footer) is what answers. */
    {
        static const char *const named[] = {
            "Europe/Berlin", "America/New_York", "Asia/Kolkata",
            "Australia/Sydney", "America/Sao_Paulo", "Africa/Cairo",
            "Europe/Moscow", "Pacific/Auckland",
        };
        static const long ntimes[] = {
            -315619200,   /* 1960-01-01 */
            0, 100000000, 500000000, 1000000000, 1500000000,
            1700000000, 1710000000, 1800000000, 1900000000, 2000000000,
        };
        for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
            for (size_t j = 0; j < sizeof ntimes / sizeof ntimes[0]; j++)
                one(named[i], ntimes[j]);
    }

    /* A zone nobody has: both this engine and glibc end up at UTC. */
    {
        setenv("TZ", "No/Such_Zone_At_All", 1);
        tzset();
        long want = 1;
        int want_dst = 1;
        struct tm tmg;
        localtime_r(&(long){ 1700000000 }, &tmg);
        {
            long t = 1700000000;
            localtime_r(&t, &tmg);
            want = tmg.tm_gmtoff;
            want_dst = tmg.tm_isdst > 0;
        }
        cact_tzset();
        int isdst = 0;
        long off = cact_tz_offset_utc(1700000000L, &isdst);
        checks++;
        if (off != want || isdst != want_dst) {
            fails++;
            printf("FAIL missing zone: east=%ld isdst=%d want %ld/%d\n", off, isdst, want, want_dst);
        }
    }

    /* Unset and empty TZ both mean UTC. */
    {
        unsetenv("TZ");
        cact_tzset();
        int isdst = 0;
        long off = cact_tz_offset_utc(0, &isdst);
        checks++;
        if (off != 0 || strcmp(cact_tzname[0], "UTC")) {
            fails++;
            printf("FAIL unset TZ: east=%ld name=%s\n", off, cact_tzname[0]);
        }
        setenv("TZ", "", 1);
        cact_tzset();
        off = cact_tz_offset_utc(0, &isdst);
        checks++;
        if (off != 0) { fails++; printf("FAIL empty TZ: east=%ld\n", off); }
    }

    /* Historic labels glibc spells differently than the zone file does: a
     * bounded, printed allowance rather than a silent one. */
    checks++;
    if (name_tolerated > 3) {
        fails++;
        printf("FAIL %d zone labels differed from glibc (expected at most 3)\n", name_tolerated);
    }

    printf("tz: %d checks, %d failed (%d tolerated label differences)\n",
           checks, fails, name_tolerated);
    return fails ? 1 : 0;
}
