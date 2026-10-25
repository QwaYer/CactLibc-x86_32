#include "time.h"
#include "tz.h"
#include "ctype.h"
#include "string.h"

/* strftime()/strptime() for the C locale.
 *
 * There is no locale support; the names are the fixed English ones.  The zone
 * comes from TZ (src/tz.c): %Z reports the zone's abbreviation and %z its
 * offset for the struct tm at hand. */

static const char *const wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const wday_abbr[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const mon_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char *const mon_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* output buffer */
struct sbuf {
    char  *s;
    size_t max;
    size_t n;
    int    overflow;
};

static void sb_putc(struct sbuf *b, int c) {
    if (b->n + 1 < b->max) b->s[b->n] = (char)c;
    else b->overflow = 1;
    b->n++;
}

/* padding/letter-case modifiers */
#define M_NONE   0x01
#define M_SPACE  0x02
#define M_ZERO   0x04
#define M_UPPER  0x08
#define M_SWAP   0x10

static void str_out(struct sbuf *b, const char *s, int mods, int width) {
    size_t len = strlen(s);
    for (size_t i = len; i < (size_t)width; i++) sb_putc(b, ' ');

    int upper = (mods & M_UPPER) != 0;
    int lower = 0;
    if (mods & M_SWAP) {
        /* glibc's '#', measured: mixed case becomes upper, an all-upper string
         * becomes lower, an all-lower string is left alone. */
        int has_upper = 0, has_lower = 0;
        for (size_t i = 0; i < len; i++) {
            if (isupper((unsigned char)s[i])) has_upper = 1;
            else if (islower((unsigned char)s[i])) has_lower = 1;
        }
        if (has_upper && has_lower) upper = 1;
        else if (has_upper) lower = 1;
    }

    for (size_t i = 0; i < len; i++) {
        int c = (unsigned char)s[i];
        if (upper) c = toupper(c);
        else if (lower) c = tolower(c);
        sb_putc(b, c);
    }
}

static void num_out(struct sbuf *b, long v, int defw, char defpad, int mods, int width) {
    char tmp[24];
    int n = 0;
    unsigned long u = (v < 0) ? -(unsigned long)v : (unsigned long)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);

    char pad = defpad;
    if (mods & M_NONE) pad = 0;                 /* %- : no padding at all */
    else if (mods & M_SPACE) pad = ' ';
    else if (mods & M_ZERO) pad = '0';

    int w = width ? width : defw;
    int len = n + (v < 0 ? 1 : 0);
    if (v < 0) sb_putc(b, '-');
    if (pad) for (int i = len; i < w; i++) sb_putc(b, pad);
    for (int i = n - 1; i >= 0; i--) sb_putc(b, tmp[i]);
}

static int days_in_year(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
}

/* Weekday of January 1st, with Monday = 1 ... Sunday = 7. */
static int jan1_wd(int y) {
    long yy = y - 1;
    long days = 365L * yy + yy / 4 - yy / 100 + yy / 400
              - (365L * 1969 + 1969 / 4 - 1969 / 100 + 1969 / 400);
    return (int)(((days % 7) + 3 + 7) % 7) + 1;      /* 1970-01-01 was a Thursday */
}

static int iso_weeks_in_year(int y) {
    int wd = jan1_wd(y);
    return (wd == 4 || (wd == 3 && days_in_year(y) == 366)) ? 53 : 52;
}

/* ISO 8601 week number and its (possibly different) year. */
static void iso_week(const struct tm *tm, int *week, int *year) {
    int y = tm->tm_year + 1900;
    int doy = tm->tm_yday + 1;
    int wd = (tm->tm_wday == 0) ? 7 : tm->tm_wday;   /* Monday = 1 ... Sunday = 7 */
    int w = (doy - wd + 10) / 7;

    if (w < 1) { y--; w = iso_weeks_in_year(y); }
    else if (w > iso_weeks_in_year(y)) { w = 1; y++; }

    *week = w;
    *year = y;
}

static int fmt_run(struct sbuf *b, const char *format, const struct tm *tm, int depth);

/* The composites (%c %F %r %x …) expand into a scratch buffer so the letter-case
 * modifier can be applied to the whole result, as glibc does.  Measured against
 * glibc: `^` uppercases the whole composite, `#` (swap) is *dropped* rather than
 * applied to the whole string (`%#c` == `%c`), and a width pads with spaces
 * regardless of the '-' flag. */
static int comp_out(struct sbuf *b, const char *fmt, const struct tm *tm,
                    int depth, int mods, int width) {
    char tmp[128];
    struct sbuf t;
    t.s = tmp; t.max = sizeof tmp; t.n = 0; t.overflow = 0;
    if (!fmt_run(&t, fmt, tm, depth)) return 0;
    if (t.overflow) return 0;
    tmp[t.n] = '\0';
    str_out(b, tmp, mods & ~M_SWAP, width);
    return 1;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || max == 0 || !format || !tm) return 0;
    struct sbuf b;
    b.s = s;
    b.max = max;
    b.n = 0;
    b.overflow = 0;
    if (!fmt_run(&b, format, tm, 0)) return 0;
    if (b.n >= b.max) return 0;
    b.s[b.n] = '\0';
    return b.n;
}

static int fmt_run(struct sbuf *b, const char *format, const struct tm *tm, int depth) {
    if (depth > 4) return 0;
    int y = tm->tm_year + 1900;

    for (const char *p = format; *p; p++) {
        if (*p != '%') { sb_putc(b, (unsigned char)*p); continue; }
        p++;

        int mods = 0;
        for (;;) {
            if (*p == '-') { mods = M_NONE; p++; }
            else if (*p == '_') { mods = M_SPACE; p++; }
            else if (*p == '0') { mods = M_ZERO; p++; }
            else if (*p == '^') { mods |= M_UPPER; p++; }
            else if (*p == '#') { mods |= M_SWAP; p++; }
            else break;
        }
        int width = 0;
        while (isdigit((unsigned char)*p)) { width = width * 10 + (*p - '0'); p++; }

        int h12 = tm->tm_hour % 12;
        if (h12 == 0) h12 = 12;

        switch (*p) {
        case 'a': str_out(b, wday_abbr[tm->tm_wday % 7], mods, width); break;
        case 'A': str_out(b, wday_full[tm->tm_wday % 7], mods, width); break;
        case 'b': case 'h': str_out(b, mon_abbr[tm->tm_mon % 12], mods, width); break;
        case 'B': str_out(b, mon_full[tm->tm_mon % 12], mods, width); break;
        case 'c': if (!comp_out(b, "%a %b %e %H:%M:%S %Y", tm, depth + 1, mods, width)) return 0; break;
        case 'C': num_out(b, y / 100, 2, '0', mods, width); break;
        case 'd': num_out(b, tm->tm_mday, 2, '0', mods, width); break;
        case 'D': if (!comp_out(b, "%m/%d/%y", tm, depth + 1, mods, width)) return 0; break;
        case 'e': num_out(b, tm->tm_mday, 2, ' ', mods, width); break;
        case 'F': if (!comp_out(b, "%Y-%m-%d", tm, depth + 1, mods, width)) return 0; break;
        case 'g': { int w, iy; (void)w; iso_week(tm, &w, &iy);
                    num_out(b, iy % 100, 2, '0', mods, width); break; }
        case 'G': { int w, iy; (void)w; iso_week(tm, &w, &iy);
                    num_out(b, iy, 0, '0', mods, width); break; }
        case 'H': num_out(b, tm->tm_hour, 2, '0', mods, width); break;
        case 'I': num_out(b, h12, 2, '0', mods, width); break;
        case 'j': num_out(b, tm->tm_yday + 1, 3, '0', mods, width); break;
        case 'k': num_out(b, tm->tm_hour, 2, ' ', mods, width); break;
        case 'l': num_out(b, h12, 2, ' ', mods, width); break;
        case 'm': num_out(b, tm->tm_mon + 1, 2, '0', mods, width); break;
        case 'M': num_out(b, tm->tm_min, 2, '0', mods, width); break;
        case 'n': sb_putc(b, '\n'); break;
        case 'p': str_out(b, tm->tm_hour < 12 ? "AM" : "PM", mods, width); break;
        case 'P': str_out(b, tm->tm_hour < 12 ? "am" : "pm", mods, width); break;
        case 'r': if (!comp_out(b, "%I:%M:%S %p", tm, depth + 1, mods, width)) return 0; break;
        case 'R': if (!comp_out(b, "%H:%M", tm, depth + 1, mods, width)) return 0; break;
        case 's': { struct tm copy = *tm; num_out(b, (long)mktime(&copy), 0, '0', mods, width); break; }
        case 'S': num_out(b, tm->tm_sec, 2, '0', mods, width); break;
        case 't': sb_putc(b, '\t'); break;
        case 'T': case 'X': if (!comp_out(b, "%H:%M:%S", tm, depth + 1, mods, width)) return 0; break;
        case 'u': num_out(b, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0', mods, width); break;
        case 'U': num_out(b, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0', mods, width); break;
        case 'V': { int w, iy; iso_week(tm, &w, &iy); num_out(b, w, 2, '0', mods, width); break; }
        case 'w': num_out(b, tm->tm_wday, 1, '0', mods, width); break;
        case 'W': num_out(b, (tm->tm_yday + 7 - ((tm->tm_wday + 6) % 7)) / 7, 2, '0', mods, width); break;
        case 'x': if (!comp_out(b, "%m/%d/%y", tm, depth + 1, mods, width)) return 0; break;
        case 'y': num_out(b, y % 100, 2, '0', mods, width); break;
        case 'Y': num_out(b, y, 0, '0', mods, width); break;
        case 'z': {
            int isdst = 0;
            long east = cact_tz_offset_for_tm(tm, &isdst);
            char zb[8];
            int k = 0;
            if (east < 0) { zb[k++] = '-'; east = -east; }
            else            zb[k++] = '+';
            long hh = east / 3600, mm = (east % 3600) / 60;
            zb[k++] = (char)('0' + (hh / 10) % 10);
            zb[k++] = (char)('0' + hh % 10);
            zb[k++] = (char)('0' + (mm / 10) % 10);
            zb[k++] = (char)('0' + mm % 10);
            zb[k] = '\0';
            str_out(b, zb, mods, width);
            break;
        }
        case 'Z': {
            int isdst = 0;
            (void)cact_tz_offset_for_tm(tm, &isdst);
            str_out(b, tzname[isdst], mods, width);
            break;
        }
        case '%': sb_putc(b, '%'); break;
        case '\0': return 0;                        /* trailing '%' */
        default: sb_putc(b, '%'); sb_putc(b, (unsigned char)*p); break;
        }
        if (b->overflow) return 0;
    }
    return 1;
}

/* ── strptime ───────────────────────────────────────────────────────────── */

static int ci_match(const char **in, const char *name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)(*in)[i]) != tolower((unsigned char)name[i])) return 0;
    *in += n;
    return 1;
}

static int get_num(const char **in, int maxdigits, int *out, int *ndigits) {
    const char *p = *in;
    int v = 0, n = 0;
    while (n < maxdigits && isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; n++; }
    if (n == 0) return 0;
    *in = p;
    *out = v;
    if (ndigits) *ndigits = n;
    return 1;
}

static int skip_ws(const char **in) {
    const char *p = *in;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') p++;
    *in = p;
    return 1;
}

/* A day-of-year (1-based) expanded into month and day for `y`. */
static void yday_to_md(int y, int doy, int *mon, int *mday) {
    static const int md[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int m = 0;
    for (; m < 11; m++) {
        int dm = (m == 1 && days_in_year(y) == 366) ? 29 : md[m];
        if (doy <= dm) break;
        doy -= dm;
    }
    *mon = m;
    *mday = doy;
}

/* With a complete date, the derived fields are filled in as well (glibc does
 * this too, and callers mktime() the result). */
static void finalize_date(struct tm *tm) {
    static const int md[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int doy = tm->tm_mday;
    for (int i = 0; i < m - 1 && i < 12; i++)
        doy += (i == 1 && days_in_year(y) == 366) ? 29 : md[i];

    tm->tm_yday = doy - 1;
    int jan1 = jan1_wd(y);                             /* Monday = 1 ... Sunday = 7 */
    int wd_m1 = ((jan1 - 1 + (doy - 1)) % 7) + 1;
    tm->tm_wday = wd_m1 % 7;                           /* Sunday = 0 */
}

/* Month name (abbreviated or full), or a number with %m. */
char *strptime(const char *s, const char *format, struct tm *tm) {
    const char *in = s;
    int have_year = 0, have_mon = 0, have_mday = 0, have_yday = 0;
    int yday_val = 0;
    int year = tm->tm_year + 1900, mon = tm->tm_mon, mday = tm->tm_mday;
    int hour12 = -1, pm = -1;

    if (!s || !format || !tm) return 0;

    for (const char *f = format; *f; f++) {
        if (isspace((unsigned char)*f)) { skip_ws(&in); continue; }
        if (*f != '%') {
            if (*in != *f) return 0;
            in++;
            continue;
        }
        f++;

        int width = 0;
        while (isdigit((unsigned char)*f)) { width = width * 10 + (*f - '0'); f++; }

        int v = 0, nd = 0;
        switch (*f) {
        case 'a': case 'A': {
            int found = -1;
            for (int i = 0; i < 7; i++) {
                const char *p = in;
                if (ci_match(&p, wday_full[i]) || (p = in, ci_match(&p, wday_abbr[i]))) {
                    found = i;
                    in = p;
                    break;
                }
            }
            if (found < 0) return 0;
            tm->tm_wday = found;
            break;
        }
        case 'b': case 'B': case 'h': {
            int found = -1;
            for (int i = 0; i < 12; i++) {
                const char *p = in;
                if (ci_match(&p, mon_full[i])) { found = i; in = p; break; }
                p = in;
                if (ci_match(&p, mon_abbr[i])) { found = i; in = p; break; }
            }
            if (found < 0) return 0;
            mon = found;
            have_mon = 1;
            break;
        }
        case 'c': {
            char *r = strptime(in, "%a %b %e %H:%M:%S %Y", tm);
            if (!r) return 0;
            in = r;
            have_year = have_mon = have_mday = 1;
            year = tm->tm_year + 1900; mon = tm->tm_mon; mday = tm->tm_mday;
            break;
        }
        case 'C':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            year = v * 100 + (year % 100);
            have_year = 1;
            break;
        case 'd':
            if (!get_num(&in, 2, &mday, &nd)) return 0;
            have_mday = 1;
            break;
        case 'e':
            skip_ws(&in);                        /* %e is space-padded */
            if (!get_num(&in, 2, &mday, &nd)) return 0;
            have_mday = 1;
            break;
        case 'D': {
            char *r = strptime(in, "%m/%d/%y", tm);
            if (!r) return 0;
            in = r;
            have_mon = have_mday = 1;
            year = tm->tm_year + 1900;
            mon = tm->tm_mon;
            mday = tm->tm_mday;
            break;
        }
        case 'F': {
            char *r = strptime(in, "%Y-%m-%d", tm);
            if (!r) return 0;
            in = r;
            have_year = have_mon = have_mday = 1;
            year = tm->tm_year + 1900; mon = tm->tm_mon; mday = tm->tm_mday;
            break;
        }
        case 'H': case 'k':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            tm->tm_hour = v;
            break;
        case 'I': case 'l':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            hour12 = v;
            break;
        case 'j':
            if (!get_num(&in, 3, &v, &nd)) return 0;
            have_yday = 1;
            yday_val = v;
            break;
        case 'm':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            mon = v - 1;
            have_mon = 1;
            break;
        case 'M':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            tm->tm_min = v;
            break;
        case 'n': case 't': skip_ws(&in); break;
        case 'p': case 'P': {
            const char *p = in;
            if (ci_match(&p, "AM")) { pm = 0; in = p; }
            else if (ci_match(&p, "PM")) { pm = 1; in = p; }
            else return 0;
            break;
        }
        case 'r': {
            char *r = strptime(in, "%I:%M:%S %p", tm);
            if (!r) return 0;
            in = r;
            break;
        }
        case 'R': {
            char *r = strptime(in, "%H:%M", tm);
            if (!r) return 0;
            in = r;
            break;
        }
        case 'S':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            tm->tm_sec = v;
            break;
        case 'T': case 'X': {
            char *r = strptime(in, "%H:%M:%S", tm);
            if (!r) return 0;
            in = r;
            break;
        }
        case 'u': case 'w':
            if (!get_num(&in, 1, &v, &nd)) return 0;
            tm->tm_wday = (*f == 'u') ? (v % 7) : v;
            break;
        case 'U': case 'W':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            /* Recorded but not used to derive a date: POSIX leaves the week
             * specifiers as an input hint that only matters with a year. */
            break;
        case 'V':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            break;
        case 'y':
            if (!get_num(&in, 2, &v, &nd)) return 0;
            year = (v <= 68) ? 2000 + v : 1900 + v;
            have_year = 1;
            break;
        case 'Y':
            if (!get_num(&in, 4, &v, &nd)) return 0;
            year = v;
            have_year = 1;
            break;
        case 'z': {
            const char *p = in;
            if (*p == 'Z') { in = p + 1; break; }
            if (*p != '+' && *p != '-') return 0;
            p++;
            int hh = 0, mm = 0, n1 = 0, n2 = 0;
            if (!get_num(&p, 2, &hh, &n1)) return 0;
            if (*p == ':') p++;
            if (!get_num(&p, 2, &mm, &n2)) return 0;
            (void)hh; (void)mm;
            in = p;
            break;
        }
        case 'Z': {
            const char *p = in;
            if (ci_match(&p, "UTC") || ci_match(&p, "GMT") ||
                (p = in, ci_match(&p, "Z"))) { in = p; break; }
            /* Accept any run of letters as a zone name. */
            if (!isalpha((unsigned char)*p)) return 0;
            while (isalpha((unsigned char)*p)) p++;
            in = p;
            break;
        }
        case '%':
            if (*in != '%') return 0;
            in++;
            break;
        default:
            return 0;
        }
    }

    if (hour12 >= 0) {
        if (hour12 < 1 || hour12 > 12) return 0;
        int h = hour12 % 12;
        if (pm == 1) h += 12;
        else if (pm < 0 && hour12 == 12) h = 12;
        tm->tm_hour = h;
    }

    if (have_year) tm->tm_year = year - 1900;
    if (have_mon)  { if (mon < 0 || mon > 11) return 0; tm->tm_mon = mon; }
    if (have_mday) { if (mday < 1 || mday > 31) return 0; tm->tm_mday = mday; }

    /* Derived fields, as glibc computes them: a day-of-year expands into
     * month/day, and any date-ish field fills tm_yday/tm_wday too. */
    if (have_yday && !have_mday) {
        int m2, d2;
        yday_to_md(tm->tm_year + 1900, yday_val, &m2, &d2);
        tm->tm_mon  = m2;
        tm->tm_mday = d2;
        tm->tm_yday = yday_val - 1;
        int jan1 = jan1_wd(tm->tm_year + 1900);
        tm->tm_wday = (((jan1 - 1 + (yday_val - 1)) % 7) + 1) % 7;
    } else if (have_year || have_mon || have_mday) {
        finalize_date(tm);
    }

    return (char *)in;
}
