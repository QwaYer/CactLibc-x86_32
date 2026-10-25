/*
 * Timezone engine: POSIX TZ strings, and TZif zone files.
 *
 *   TZ = std offset [dst [offset] [,start[/time],end[/time]]]
 *
 * The offset is written the way POSIX and TZif both do — positive west of
 * Greenwich — so `EST5` is UTC-5 and the engine stores the negated value, east
 * of UTC, which is what localtime() adds.
 *
 * A rule is `Jn` (day n of a 365-day year, February 29 never counted), `n`
 * (0-based day of the year, leap day counted) or `Mm.w.d` (week w of month m,
 * day d; w = 5 means "the last").  A DST name with no rules gets glibc's
 * TZDEFRULES, M3.2.0/M11.1.0 at 02:00.
 *
 * When TZ names a zone file instead (`:`-prefixed, or containing a '/' and no
 * comma), the file is read from /usr/share/zoneinfo and its transition table is
 * used; if there is no such file the zone is UTC, as glibc does.  A CactOS
 * image ships no zoneinfo tree, so this path is only taken when one is staged —
 * but the code is the same the host test exercises against the host's own
 * database.
 */

#include "tz.h"
#include "time.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"

/* ── public state (POSIX) ───────────────────────────────────────────────── */

char *tzname[2] = { (char *)"UTC", (char *)"UTC" };
long timezone = 0;                      /* seconds WEST of UTC, standard time */
int  daylight = 0;                      /* nonzero when DST is ever used */

/* ── the parsed zone ────────────────────────────────────────────────────── */

struct zone {
    long east;                          /* seconds east of UTC */
    int  isdst;
    char abbr[12];
};

/* Rule kinds. */
enum { RULE_NONE = 0, RULE_JULIAN, RULE_DAY, RULE_MONTH };

struct rule {
    int  kind;
    int  n, m, w, d;
    long time;                          /* seconds after local midnight */
};

#define TZ_MAXTYPES 64
#define TZ_MAXTRANS 512
#define TZ_FILE_MAX 8192
#define TZ_PATH_MAX 256

static struct {
    int        valid;
    char       key[128];                /* TZ value this state was parsed from */
    struct zone st, dt;
    int        has_dst;
    struct rule start, end;

    /* A zone file (TZif), when TZ named one of those. */
    int        named;
    int        footer;                  /* its POSIX TZ string was parsed */
    int        ntypes, ntrans;
    int        first_std;               /* the type for times before the first */
    int        std_idx, dst_idx;        /* the types the public names come from */
    struct zone types[TZ_MAXTYPES];
    long       trans[TZ_MAXTRANS];
    unsigned char ttype[TZ_MAXTRANS];
} tz;

static char tz_abbrbuf[2][12];

/* ── civil calendar ─────────────────────────────────────────────────────── */

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian). */
static long days_from_civil(long y, long m, long d) {
    y -= (m <= 2) ? 1 : 0;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp  = (unsigned)((m + 9) % 12);
    unsigned doy = (153u * mp + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

static int is_leap(long y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static long days_in_month(long y, int m) {      /* m: 1..12 */
    static const int md[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && is_leap(y)) return 29;
    return md[m - 1];
}

/* Sunday = 0. */
static int weekday_of(long days) {
    long w = (days + 4) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

/* Civil year containing the day number `days` (days since the epoch). */
static long year_of_days(long days) {
    long y = 1970 + days / 365;
    while (days_from_civil(y + 1, 1, 1) <= days) y++;
    while (days_from_civil(y, 1, 1) > days) y--;
    return y;
}

/* 0-based day of the year on which a rule fires in year `y`. */
static int rule_doy(const struct rule *r, long y) {
    if (r->kind == RULE_JULIAN) {
        /* Jn counts a 365-day year, so Feb 29 is never one of the days. */
        int n = r->n < 1 ? 1 : (r->n > 365 ? 365 : r->n);
        int mm = 1, dd = n;
        for (mm = 1; mm < 12; mm++) {
            long dm = days_in_month(1, mm);     /* year 1: never a leap year */
            if (dd <= (int)dm) break;
            dd -= (int)dm;
        }
        long doy = 0;
        for (int i = 1; i < mm; i++) doy += days_in_month(y, i);
        return (int)(doy + dd - 1);
    }

    if (r->kind == RULE_DAY) {
        int n = r->n < 0 ? 0 : (r->n > 365 ? 365 : r->n);
        return n;
    }

    /* Mm.w.d */
    int m = r->m < 1 ? 1 : (r->m > 12 ? 12 : r->m);
    int w = r->w < 1 ? 1 : (r->w > 5 ? 5 : r->w);
    int d = r->d < 0 ? 0 : (r->d > 6 ? 6 : r->d);

    long first = 0;                             /* 0-based doy of the 1st */
    for (int i = 1; i < m; i++) first += days_in_month(y, i);

    int wd = weekday_of(days_from_civil(y, 1, 1) + first);
    long day = first + ((d - wd + 7) % 7) + (long)(w - 1) * 7;
    while (day >= first + days_in_month(y, m)) day -= 7;   /* w == 5: the last */
    return (int)day;
}

/* UTC instant at which a transition fires in year `y`.  The rule's time of day
 * is wall clock in the zone in effect *before* the change. */
static long transition_utc(const struct rule *r, long y, int to_dst) {
    long base = (days_from_civil(y, 1, 1) + rule_doy(r, y)) * 86400L + r->time;
    return base - (to_dst ? tz.dt.east : tz.st.east);
}

/* Is DST in effect at UTC instant `t`, given the transitions of year `y`? */
static int dst_active(long t, long y) {
    long s = transition_utc(&tz.start, y, 1);
    long e = transition_utc(&tz.end,   y, 0);
    if (s <= e) return (t >= s && t < e);       /* northern hemisphere */
    return (t >= s || t < e);                   /* southern: wraps the year */
}

/* ── the POSIX TZ string parser ─────────────────────────────────────────── */

/* A zone name: an alpha run, or `<...>` for a numeric abbreviation. */
static int parse_name(const char **pp, char *out, size_t cap) {
    const char *p = *pp;
    size_t n = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>' && n + 1 < cap) out[n++] = *p++;
        if (*p != '>') return 0;
        p++;
    } else {
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
    }
    if (n == 0) return 0;
    out[n] = '\0';
    *pp = p;
    return 1;
}

/* `[+|-]hh[:mm[:ss]]`, POSIX-positive west of Greenwich.  *present is cleared
 * when the string has no offset at all. */
static long parse_offset(const char **pp, int *present) {
    const char *p = *pp;
    int neg = 0;
    long secs = 0;

    *present = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if (*p < '0' || *p > '9') {
        *pp = neg ? p - 1 : p;                  /* not an offset after all */
        return 0;
    }
    *present = 1;
    while (*p >= '0' && *p <= '9') { secs = secs * 10 + (*p - '0'); p++; }
    secs *= 3600;
    if (*p == ':') {
        long mm = 0;
        p++;
        while (*p >= '0' && *p <= '9') { mm = mm * 10 + (*p - '0'); p++; }
        secs += mm * 60;
        if (*p == ':') {
            long ss = 0;
            p++;
            while (*p >= '0' && *p <= '9') { ss = ss * 10 + (*p - '0'); p++; }
            secs += ss;
        }
    }
    *pp = p;
    return neg ? -secs : secs;
}

static void parse_rule(const char **pp, struct rule *r, long default_time) {
    const char *p = *pp;

    r->kind = RULE_NONE;
    r->n = r->m = r->w = r->d = 0;
    r->time = default_time;

    if (*p == 'J') {
        p++;
        r->kind = RULE_JULIAN;
        while (*p >= '0' && *p <= '9') { r->n = r->n * 10 + (*p - '0'); p++; }
    } else if (*p == 'M') {
        p++;
        r->kind = RULE_MONTH;
        while (*p >= '0' && *p <= '9') { r->m = r->m * 10 + (*p - '0'); p++; }
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { r->w = r->w * 10 + (*p - '0'); p++; } }
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { r->d = r->d * 10 + (*p - '0'); p++; } }
    } else {
        r->kind = RULE_DAY;
        while (*p >= '0' && *p <= '9') { r->n = r->n * 10 + (*p - '0'); p++; }
    }

    if (*p == '/') {
        int present;
        p++;
        r->time = parse_offset(&p, &present);
        if (!present) r->time = default_time;
    }
    *pp = p;
}

static void set_utc_default(void) {
    tz.valid   = 1;
    tz.has_dst = 0;
    tz.st.east = 0;
    tz.st.isdst = 0;
    strcpy(tz.st.abbr, "UTC");
    strcpy(tz.dt.abbr, "UTC");
    tz.dt.east = 0;
    tz.dt.isdst = 1;
    tz.start.kind = RULE_NONE;
    tz.end.kind = RULE_NONE;
}

/* The POSIX defaults glibc uses for "dst with no rules". */
static void default_us_rules(void) {
    tz.start.kind = RULE_MONTH; tz.start.m = 3;  tz.start.w = 2; tz.start.d = 0;
    tz.start.time = 2 * 3600; tz.start.n = 0;
    tz.end.kind   = RULE_MONTH; tz.end.m   = 11; tz.end.w   = 1; tz.end.d   = 0;
    tz.end.time   = 2 * 3600; tz.end.n = 0;
}

static void parse_posix(const char *spec) {
    const char *p = spec;
    int present;

    tz.valid = 1;
    tz.has_dst = 0;
    tz.st.east = 0;
    tz.st.isdst = 0;
    if (!parse_name(&p, tz.st.abbr, sizeof tz.st.abbr)) { set_utc_default(); return; }

    tz.st.east = -parse_offset(&p, &present);   /* TZ offset is west-positive */

    if (parse_name(&p, tz.dt.abbr, sizeof tz.dt.abbr)) {
        tz.has_dst = 1;
        tz.dt.isdst = 1;
        {
            long o = parse_offset(&p, &present);
            tz.dt.east = present ? -o : tz.st.east + 3600;
        }
        if (*p == ',') {
            p++;
            parse_rule(&p, &tz.start, 2 * 3600);
            if (*p == ',') p++;
            parse_rule(&p, &tz.end, 2 * 3600);
        } else {
            default_us_rules();
        }
    } else {
        tz.dt.east = 0;
    }
}

/* ── TZ state ───────────────────────────────────────────────────────────── */

/* ── a named zone: a TZif file ──────────────────────────────────────────── */

static unsigned long rd_be32(const unsigned char *p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* The file, or -1.  The whole thing is read: zone files are small. */
static int tz_read_whole(const char *path, unsigned char *buf, long cap) {
    int fd = open(path, O_RDONLY);
    long n = 0;

    if (fd < 0) return -1;
    for (;;) {
        ssize_t r;
        if (n >= cap) break;
        r = read(fd, buf + n, (size_t)(cap - n));
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (r == 0) break;
        n += (long)r;
    }
    close(fd);
    return (int)n;
}

/* Where the type in effect at `t` sits in the table (the binary search). */
static int tz_type_at(long t) {
    int lo = 0, hi = tz.ntrans - 1;
    if (tz.ntrans <= 0 || t < tz.trans[0]) return tz.first_std;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (tz.trans[mid] <= t) lo = mid;
        else hi = mid - 1;
    }
    return tz.ttype[lo];
}

/* The type in effect at `t`, preferring the most recent entry with the given
 * DST-ness (what tzname[] has to name: the zone's current pair). */
static int tz_recent_type(int want_dst) {
    int i;
    for (i = tz.ntrans - 1; i >= 0; i--) {
        int idx = tz.ttype[i];
        if (idx >= 0 && idx < tz.ntypes && tz.types[idx].isdst == want_dst)
            return idx;
    }
    return -1;
}

/* Parse /usr/share/zoneinfo/<name>.  Returns 1 when the table is loaded. */
static int tz_read_zone(const char *spec)
{
    char path[TZ_PATH_MAX];
    unsigned char buf[TZ_FILE_MAX];
    int n, version;
    long off, cnt_ut, cnt_std, cnt_leap, cnt_time, cnt_type, cnt_char, ent;
    long j;
    const unsigned char *p;
    const char *name = (spec[0] == ':') ? spec + 1 : spec;

    if (name[0] == '/') {
        size_t i = 0;
        while (name[i] && i + 1 < sizeof path) { path[i] = name[i]; i++; }
        path[i] = '\0';
    } else {
        static const char prefix[] = "/usr/share/zoneinfo/";
        size_t i = strlen(prefix);
        const char *q = name;
        memcpy(path, prefix, i);
        while (*q && i + 1 < sizeof path) path[i++] = *q++;
        path[i] = '\0';
    }

    n = tz_read_whole(path, buf, (long)sizeof buf);
    if (n < 44 || memcmp(buf, "TZif", 4) != 0) return 0;
    version = buf[4];

    off = 20;
    cnt_ut    = (long)rd_be32(buf + off);
    cnt_std   = (long)rd_be32(buf + off + 4);
    cnt_leap  = (long)rd_be32(buf + off + 8);
    cnt_time  = (long)rd_be32(buf + off + 12);
    cnt_type  = (long)rd_be32(buf + off + 16);
    cnt_char  = (long)rd_be32(buf + off + 20);
    ent = 4;
    off = 44;

    if (version >= '2') {
        /* Skip the 32-bit block and use the 64-bit one after the second
         * header: its transitions reach past 2038. */
        long skip = cnt_time * 5 + cnt_type * 6 + cnt_char + cnt_leap * 8
                  + cnt_std + cnt_ut;
        off = 44 + skip;
        if (off + 44 > n || memcmp(buf + off, "TZif", 4) != 0) return 0;
        off += 20;
        cnt_ut    = (long)rd_be32(buf + off);
        cnt_std   = (long)rd_be32(buf + off + 4);
        cnt_leap  = (long)rd_be32(buf + off + 8);
        cnt_time  = (long)rd_be32(buf + off + 12);
        cnt_type  = (long)rd_be32(buf + off + 16);
        cnt_char  = (long)rd_be32(buf + off + 20);
        off += 24;
        ent = 8;
    }

    if (cnt_type <= 0 || cnt_type > TZ_MAXTYPES || cnt_time < 0) return 0;
    if (off + cnt_time * (ent + 1) + cnt_type * 6 + cnt_char > n) return 0;

    /* Transitions, with their type indices.  The 64-bit block reaches back
     * before 1901, where a time is outside a 32-bit time_t: those entries are
     * skipped (a real query can never be earlier) rather than ending the table,
     * and every other entry is kept. */
    {
        const unsigned char *idx = buf + off + cnt_time * ent;
        tz.ntrans = 0;
        for (j = 0; j < cnt_time && tz.ntrans < TZ_MAXTRANS; j++) {
            const unsigned char *tp = buf + off + j * ent;
            long long sv;
            if (ent == 8) {
                unsigned long long v = 0;
                int k;
                for (k = 0; k < 8; k++) v = (v << 8) | tp[k];
                sv = (long long)v;
            } else {
                sv = (long long)(int)(unsigned int)rd_be32(tp);
            }
            if (sv > 0x7fffffffLL || sv < -0x7fffffffLL - 1) continue;
            tz.trans[tz.ntrans] = (long)sv;
            tz.ttype[tz.ntrans] = idx[j];
            tz.ntrans++;
        }
    }
    /* the types and their abbreviations */
    {
        const unsigned char *ti = buf + off + cnt_time * (ent + 1);
        const unsigned char *cp = ti + cnt_type * 6;
        int k;

        tz.ntypes = (int)cnt_type;
        for (k = 0; k < tz.ntypes; k++) {
            int abbrind = ti[k * 6 + 5];
            int a = 0;
            tz.types[k].east  = (long)(int)(unsigned int)rd_be32(ti + k * 6);
            tz.types[k].isdst = ti[k * 6 + 4] ? 1 : 0;
            while (a < (int)sizeof tz.types[k].abbr - 1 && abbrind + a < (int)cnt_char) {
                char c = (char)cp[abbrind + a];
                if (!c) break;
                tz.types[k].abbr[a++] = c;
            }
            tz.types[k].abbr[a] = '\0';
        }
    }

    tz.first_std = 0;
    for (j = 0; j < tz.ntypes; j++)
        if (!tz.types[j].isdst) { tz.first_std = (int)j; break; }

    /* A v2 file ends with a POSIX TZ string giving the rules after the last
     * transition — and the "slim" files most distributions now ship have an
     * empty transition table, so that footer is the only rule there is.  It is
     * exactly what the POSIX parser above takes, so hand it over. */
    if (version >= '2') {
        long d_end = off + cnt_time * (ent + 1) + cnt_type * 6 + cnt_char
                   + cnt_leap * (ent + 4) + cnt_std + cnt_ut;
        if (d_end + 1 < n && buf[d_end] == '\n') {
            char spec[128];
            long q = d_end + 1;
            size_t k = 0;
            while (q < n && buf[q] != '\n' && k + 1 < sizeof spec)
                spec[k++] = (char)buf[q++];
            spec[k] = '\0';
            if (k > 0) {
                parse_posix(spec);
                tz.footer = 1;
            }
        }
    }

    /* The names the zone is known by: the most recent standard type and the
     * most recent DST type, which is what tzname[0]/[1] report. */
    tz.std_idx = tz_recent_type(0);
    if (tz.std_idx < 0) tz.std_idx = tz.first_std;
    tz.dst_idx = tz_recent_type(1);
    tz.named = 1;
    return 1;
}

static void refresh_public_names(void) {
    if (tz.named) {
        /* The names come from the footer's rules when there is one (they are
         * the zone's rules today), the table otherwise; `daylight` comes from
         * the file either way, because it says whether the zone has ever used
         * DST — Moscow's current rules have none, but its history does. */
        if (tz.footer) {
            strcpy(tz_abbrbuf[0], tz.st.abbr);
            strcpy(tz_abbrbuf[1], tz.has_dst ? tz.dt.abbr : tz.st.abbr);
            timezone = -tz.st.east;
        } else {
            const struct zone *s = &tz.types[tz.std_idx];
            const struct zone *d = (tz.dst_idx >= 0) ? &tz.types[tz.dst_idx] : s;
            strcpy(tz_abbrbuf[0], s->abbr);
            strcpy(tz_abbrbuf[1], (tz.dst_idx >= 0) ? d->abbr : s->abbr);
            timezone = -s->east;
        }
        tzname[0] = tz_abbrbuf[0];
        tzname[1] = tz_abbrbuf[1];
        /* Whether DST is in use *now*: the footer's rules when there are any
         * (Moscow and Kolkata have none today although their history does),
         * the table otherwise. */
        daylight  = (tz.footer ? tz.has_dst : (tz.dst_idx >= 0)) ? 1 : 0;
        return;
    }
    strcpy(tz_abbrbuf[0], tz.st.abbr);
    strcpy(tz_abbrbuf[1], tz.has_dst ? tz.dt.abbr : tz.st.abbr);
    tzname[0]  = tz_abbrbuf[0];
    tzname[1]  = tz_abbrbuf[1];
    timezone   = -tz.st.east;                   /* POSIX: west-positive */
    daylight   = tz.has_dst ? 1 : 0;
}

void tzset(void) {
    const char *env = getenv("TZ");
    const char *spec = env ? env : "UTC";

    if (spec[0] == '\0') spec = "UTC";

    if (tz.valid && strcmp(tz.key, spec) == 0) return;   /* unchanged */

    size_t n = strlen(spec);
    if (n >= sizeof tz.key) n = sizeof tz.key - 1;
    memcpy(tz.key, spec, n);
    tz.key[n] = '\0';

    /* A zone-file name: ':'-prefixed, or containing a '/' — but a POSIX spec
       also uses '/' in a rule time (`M10.5.0/3`), and that always comes with a
       comma, so require the absence of one.  With no such file the zone is UTC,
       which is what glibc does too. */
    tz.named = 0;
    tz.footer = 0;
    if (spec[0] == ':' || (strchr(spec, '/') != 0 && strchr(spec, ',') == 0)) {
        if (!tz_read_zone(spec)) parse_posix("UTC0");
    } else {
        parse_posix(spec);
    }
    refresh_public_names();
}

/* ── lookups ────────────────────────────────────────────────────────────── */

static void lookup(long t, long *east, int *isdst, const char **abbr) {
    /* A zone file's transition table answers for the times it covers; past the
     * last transition — and always, for a "slim" file with no table at all —
     * the footer's POSIX rules do. */
    if (tz.named && tz.ntrans > 0 &&
        (t <= tz.trans[tz.ntrans - 1] || !tz.footer)) {
        int idx = tz_type_at(t);
        if (idx < 0 || idx >= tz.ntypes) idx = 0;
        *east  = tz.types[idx].east;
        *isdst = tz.types[idx].isdst;
        *abbr  = tz.types[idx].abbr;
        return;
    }
    if (!tz.has_dst) {
        *east = tz.st.east;
        *isdst = 0;
        *abbr = tz.st.abbr;
        return;
    }
    {
        long days = t + tz.st.east;
        /* floor-divide into a day number, so pre-epoch times behave */
        long day = days / 86400;
        if (days < 0 && days % 86400) day--;
        long y = year_of_days(day);

        if (dst_active(t, y)) {
            *east = tz.dt.east; *isdst = 1; *abbr = tz.dt.abbr;
        } else {
            *east = tz.st.east; *isdst = 0; *abbr = tz.st.abbr;
        }
    }
}

long cact_tz_offset_utc(long t, int *isdst) {
    long east;
    const char *abbr;
    tzset();
    lookup(t, &east, isdst, &abbr);
    return east;
}

long cact_tz_offset_for_tm(const struct tm *tm, int *isdst) {
    tzset();
    if (tz.has_dst) {
        if (tm->tm_isdst > 0) { *isdst = 1; return tz.dt.east; }
        if (tm->tm_isdst == 0) { *isdst = 0; return tz.st.east; }
    }
    *isdst = tm->tm_isdst > 0 ? 1 : 0;
    return *isdst ? tz.dt.east : tz.st.east;
}

/* The local fields as if they were UTC: mktime's starting point. */
static long civil_seconds(const struct tm *tm) {
    return days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday) * 86400L
         + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
}

long cact_tz_utc_from_tm(const struct tm *tm) {
    long u = civil_seconds(tm);

    tzset();
    if (!tz.has_dst) return u - tz.st.east;

    if (tm->tm_isdst > 0) return u - tz.dt.east;
    if (tm->tm_isdst == 0) return u - tz.st.east;

    /* Unknown: settle on the offset that is self-consistent.  Two passes are
       enough — the first guess can only be wrong across a transition. */
    {
        long t = u - tz.st.east;
        for (int i = 0; i < 3; i++) {
            long east;
            int isdst;
            const char *abbr;
            lookup(t, &east, &isdst, &abbr);
            long next = u - east;
            if (next == t) break;
            t = next;
        }
        return t;
    }
}
