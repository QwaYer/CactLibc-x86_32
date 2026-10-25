#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "nodeio.h"
#include "errno.h"
#include "locale.h"
#include <stdarg.h>
#include <stdint.h>

#define F_EOF 1
#define F_ERR 2


void kprint(const char *s) {
    int fd = nio_open("/dev/console", O_WRONLY);
    if (fd < 0) return;
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = nio_write(fd, s, n);
        if (w <= 0) break;
        s += w;
        n -= (size_t)w;
    }
    nio_close(fd);
}

int putchar(int c) {
    char buf = (char)c;
    write(STDOUT_FILENO, &buf, 1);
    return c;
}

int puts(const char *str) {
    int len = strlen(str);
    write(STDOUT_FILENO, str, len);
    char nl = '\n';
    write(STDOUT_FILENO, &nl, 1);
    return 0;
}

int rename(const char *oldpath, const char *newpath) {
    char obase[128];
    int  fd = nio_open_parent(oldpath, obase, sizeof(obase));
    if (fd < 0) return -1;

    const char *slash = 0;
    for (const char *s = newpath; *s; s++)
        if (*s == '/') slash = s;
    const char *b = slash ? slash + 1 : newpath;
    char nbase[128];
    size_t n = 0;
    while (b[n] && n + 1 < sizeof(nbase)) { nbase[n] = b[n]; n++; }
    nbase[n] = '\0';
    if (!nbase[0]) { nio_close(fd); errno = EINVAL; return -1; }

    /* Destination directory: everything before the last slash, or "." if none. */
    char ndir[512];
    if (slash) {
        size_t d = (size_t)(slash - newpath);
        if (d == 0) d = 1;                     /* "/x" -> "/" */
        if (d > sizeof(ndir) - 1) d = sizeof(ndir) - 1;
        for (size_t i = 0; i < d; i++) ndir[i] = newpath[i];
        ndir[d] = '\0';
    } else {
        ndir[0] = '.'; ndir[1] = '\0';
    }

    /* RENAMEAT handles both same- and cross-directory moves atomically. */
    cact_renameat_arg_t a;
    a.oldname = obase;
    a.newdir  = ndir;
    a.newname = nbase;
    int r = nio_ioctl(fd, CACT_DIRCTL_RENAMEAT, &a);
    nio_close(fd);
    return nio_map(r);
}


FILE __stdio_stdin  = { .fd = 0, .flags = 0, .ungotten = -1, .buf = {0} };
FILE __stdio_stdout = { .fd = 1, .flags = 0, .ungotten = -1, .buf = {0} };
FILE __stdio_stderr = { .fd = 2, .flags = 0, .ungotten = -1, .buf = {0} };

FILE *fopen(const char *pathname, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r') flags = O_RDONLY;
    if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    else if (mode[0] == 'w' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;

    int fd = open(pathname, flags);
    if (fd < 0) return 0;

    FILE *f = malloc(sizeof(FILE));
    if (!f) { close(fd); return 0; }
    f->fd = fd;
    f->flags = 0;
    f->ungotten = -1;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    FILE *f = malloc(sizeof(FILE));
    if (!f) return 0;
    f->fd = fd;
    f->flags = 0;
    f->ungotten = -1;
    return f;
}

int fclose(FILE *stream) {
    if (stream == stdin || stream == stdout || stream == stderr)
        return 0;
    int ret = close(stream->fd);
    free(stream);
    return ret;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    if (total == 0) return 0;
    ssize_t r = read(stream->fd, ptr, total);
    if (r < 0) { stream->flags |= F_ERR; return 0; }
    if (r == 0) { stream->flags |= F_EOF; return 0; }
    return (r / size);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    if (total == 0) return 0;
    ssize_t w = write(stream->fd, ptr, total);
    if (w < 0) { stream->flags |= F_ERR; return 0; }
    return (w / size);
}

int fseek(FILE *stream, long offset, int whence) {
    off_t r = lseek(stream->fd, offset, whence);
    return (r < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = '\0';
    return s;
}

int feof(FILE *stream) {
    return (stream->flags & F_EOF) != 0;
}

int ferror(FILE *stream) {
    return (stream->flags & F_ERR) != 0;
}

void clearerr(FILE *stream) {
    stream->flags &= ~(F_EOF | F_ERR);
}

int fgetc(FILE *stream) {
    if (stream->ungotten >= 0) {
        int c = stream->ungotten;
        stream->ungotten = -1;
        return c;
    }
    unsigned char c;
    ssize_t r = read(stream->fd, &c, 1);
    if (r == 0) { stream->flags |= F_EOF; return EOF; }
    if (r < 0)  { stream->flags |= F_ERR; return EOF; }
    return c;
}

int ungetc(int c, FILE *stream) {
    stream->ungotten = c;
    return c;
}

int fputc(int c, FILE *stream) {
    unsigned char uc = (unsigned char)c;
    ssize_t w = write(stream->fd, &uc, 1);
    if (w != 1) { stream->flags |= F_ERR; return EOF; }
    return c;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    ssize_t w = write(stream->fd, s, len);
    if (w < 0) { stream->flags |= F_ERR; return EOF; }
    return 0;
}

int remove(const char *pathname) {
    return unlink(pathname);
}

int fileno(FILE *stream) {
    return stream->fd;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n) return -1;
    if (!*lineptr) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t len = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (len == 0) return -1;
            break;
        }
        if (len + 1 >= *n) {
            size_t nn = *n * 2;
            char *np = realloc(*lineptr, nn);
            if (!np) return -1;
            *lineptr = np;
            *n = nn;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}

FILE *tmpfile(void) {
    char t[] = "/tmp/tmpXXXXXX";
    int fd = mkstemp(t);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w+");
    if (!f) close(fd);
    return f;
}

void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    char *e = strerror(errno);
    write(2, e, strlen(e));
    write(2, "\n", 1);
}


static int _fmt_uint(unsigned long v, unsigned int base, int upper, char *out) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    char tmp[40];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (upper ? up : lo)[v % base]; v /= base; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

static int _fmt_u64(unsigned long long v, unsigned int base, int upper, char *out) {
    unsigned int hi = (unsigned int)(v >> 32);
    unsigned int lo = (unsigned int)(v & 0xffffffffu);
    static const char lo_d[] = "0123456789abcdef";
    static const char up_d[] = "0123456789ABCDEF";
    const char *dig = upper ? up_d : lo_d;

    if (base == 16) {
        char rev[24];
        int n = 0;
        if (lo == 0) rev[n++] = '0';
        while (lo) { rev[n++] = dig[lo % 16]; lo /= 16; }
        if (hi) {
            while (n < 8) rev[n++] = '0';
            while (hi) { rev[n++] = dig[hi % 16]; hi /= 16; }
        }
        for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
        out[n] = '\0';
        return n;
    }

    /* base 10: 64/32-bit division by unsigned int — a single divl, no libgcc */
    char rev[40];
    int n = 0;
    unsigned int d = 10u;
    if (v == 0) rev[n++] = '0';
    while (v) {
        rev[n++] = '0' + (unsigned int)(v % d);
        v /= d;
    }
    for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
    out[n] = '\0';
    return n;
}


/* ── float formatting ─────────────────────────────────────────────────────
 * Neither %f/%e/%g nor an explicit precision (.N) used to be understood: the
 * parser stopped at '.', so `%.1f` was printed as `%` + `.1f` and a percent
 * sign in the text.  This adds both, without touching the integer paths.
 *
 * Floats are rendered from the value itself: sign, integer part, then a
 * fraction scaled to 10^prec and rounded.  That is exact while the parts fit
 * in 64 bits (~18 significant digits); beyond that %f falls back to the
 * scientific form, which values that large would not fit the buffer for
 * anyway. */
static double _pow10_i(int n) {
    double r = 1.0;
    while (n-- > 0) r *= 10.0;
    return r;
}

/* Same in long double: the 64-bit mantissa holds 10^prec and the product with
   the fractional part exactly up to 18 digits, so rounding matches exact
   decimal printing rather than the accumulated error of double. */
static long double _pow10_ld(int n) {
    long double r = 1.0L;
    while (n-- > 0) r *= 10.0L;
    return r;
}

/* Decimal digits of v, most significant first.  Returns the count. */
static int _u64_digits(unsigned long long v, char *out) {
    char rev[24];
    int n = 0;
    if (v == 0) rev[n++] = '0';
    while (v) { rev[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
    return n;
}

#define _FMT_MAXPREC 18

/* "123.456" — magnitude of v, no sign.  prec >= 0. */
static int _fmt_fixed(double v, int prec, char *out, int cap) {
    int n = 0;
    if (prec > _FMT_MAXPREC) prec = _FMT_MAXPREC;
    if (v >= 1.0e18) return -1;                 /* does not fit: caller will fall back to %e */
    unsigned long long ip = (unsigned long long)v;
    double fr = v - (double)ip;
    unsigned long long scale = (unsigned long long)_pow10_i(prec);
    long double scaled = (long double)fr * _pow10_ld(prec);
    unsigned long long fd = (unsigned long long)scaled;
    long double rem = scaled - (long double)fd;
    /* rounding as in C: half to even (with prec==0 the last digit of the
       integer part is rounded, not the fractional part) */
    unsigned long long last = (prec == 0) ? (ip & 1ULL) : (fd & 1ULL);
    if (rem > 0.5 || (rem == 0.5 && last)) fd += 1;
    if (fd >= scale) { fd -= scale; ip += 1; }  /* the rounding carried over into the integer part */

    char dig[24];
    int dn = _u64_digits(ip, dig);
    for (int i = 0; i < dn && n < cap - 1; i++) out[n++] = dig[i];

    if (prec > 0) {
        char fdg[24];
        int fn = _u64_digits(fd, fdg);
        if (n < cap - 1) out[n++] = '.';
        for (int i = fn; i < prec && n < cap - 1; i++) out[n++] = '0';   /* leading zeros of the fraction */
        for (int i = 0; i < fn && n < cap - 1; i++) out[n++] = fdg[i];
    }
    out[n] = '\0';
    return n;
}

/* "1.234e+05" — magnitude of v, no sign.  prec >= 0 digits after the point. */
static int _fmt_sci(double v, int prec, int upper, char *out, int cap) {
    if (prec > _FMT_MAXPREC) prec = _FMT_MAXPREC;
    int exp = 0;
    double a = v;
    if (a != 0.0) {
        while (a >= 10.0) { a /= 10.0; exp++; }
        while (a < 1.0)   { a *= 10.0; exp--; }
    }
    unsigned long long full = (unsigned long long)_pow10_i(prec);
    long double scaled = (long double)a * _pow10_ld(prec);
    unsigned long long m = (unsigned long long)scaled;
    long double rem = scaled - (long double)m;
    if (rem > 0.5 || (rem == 0.5 && (m & 1ULL))) m += 1;   /* half to even */
    if (m >= full * 10ULL) { m /= 10ULL; exp++; }        /* 9.99… -> 1.0e+1 */

    char dig[24];
    int dn = _u64_digits(m, dig);
    int n = 0;
    for (int i = dn; i < prec + 1; i++) { if (n < cap - 1) out[n++] = '0'; }  /* align the mantissa */
    for (int i = 0; i < dn && n < cap - 1; i++) out[n++] = dig[i];
    if (prec > 0 && n < cap - 1) {
        /* point after the first digit: shift the tail */
        for (int i = n; i > 1; i--) out[i] = out[i - 1];
        out[1] = '.';
        n++;
    }
    if (n < cap - 1) out[n++] = upper ? 'E' : 'e';
    if (n < cap - 1) out[n++] = (exp < 0) ? '-' : '+';
    int ae = exp < 0 ? -exp : exp;
    if (ae < 10 && n < cap - 1) out[n++] = '0';
    char ed[8];
    int en = _u64_digits((unsigned long long)ae, ed);
    for (int i = 0; i < en && n < cap - 1; i++) out[n++] = ed[i];
    out[n] = '\0';
    return n;
}

/* Remove trailing zeros (and the point) — needed by %g. */
static void _strip_zeros(char *s, int *len) {
    int n = *len, i = 0;
    while (i < n && s[i] != '.' && s[i] != 'e' && s[i] != 'E') i++;
    if (i >= n || s[i] != '.') return;
    int end = i + 1;
    while (end < n && s[end] >= '0' && s[end] <= '9') end++;
    int j = end;
    while (j > i + 1 && s[j - 1] == '0') j--;
    if (j == i + 1) j = i;                       /* point with no fraction — remove it too */
    for (int k = j; k < n - (end - j); k++) s[k] = s[k + (end - j)];
    *len = n - (end - j);
}

/* %f/%e/%g (and uppercase).  The caller handles the sign. */
static int _fmt_double(double v, int prec, int conv, char *out, int cap) {
    int upper = (conv >= 'A' && conv <= 'Z');
    char mode  = (conv == 'e' || conv == 'E') ? 'e'
               : (conv == 'g' || conv == 'G') ? 'g' : 'f';
    int n;

    if (v != v) {                                /* NaN */
        const char *s = upper ? "NAN" : "nan";
        n = 0;
        while (*s && n < cap - 1) out[n++] = *s++;
        out[n] = '\0';
        return n;
    }
    if (v > 1.7976931348623157e308) {            /* +/- inf, sign handled by the caller */
        const char *s = upper ? "INF" : "inf";
        n = 0;
        while (*s && n < cap - 1) out[n++] = *s++;
        out[n] = '\0';
        return n;
    }

    if (mode == 'g') {
        if (prec == 0) prec = 1;
        /* the exponent decides which form is shorter (C rule) */
        int exp = 0;
        double a = v;
        if (a != 0.0) {
            while (a >= 10.0) { a /= 10.0; exp++; }
            while (a < 1.0)   { a *= 10.0; exp--; }
        }
        if (exp < -4 || exp >= prec) n = _fmt_sci(v, prec - 1, upper, out, cap);
        else                         n = _fmt_fixed(v, prec - 1 - exp, out, cap);
        if (n < 0) n = _fmt_sci(v, prec - 1, upper, out, cap);
        _strip_zeros(out, &n);
        return n;
    }

    if (mode == 'e') return _fmt_sci(v, prec, upper, out, cap);
    n = _fmt_fixed(v, prec, out, cap);
    return (n < 0) ? _fmt_sci(v, prec, upper, out, cap) : n;
}

/* ── argument slots: positional (%n$…), width/precision from the list ─────
 * The format is walked twice.  The first walk records, for each argument
 * position, the type its conversion expects; the values are then pulled out
 * of the va_list in position order (position 1 is the first argument after
 * the format, whether the format names it "%1$" or not).  That is what makes
 * "%2$s %1$s" work off a single va_list, and leaves the ordinary
 * non-positional path byte-for-byte as cheap as it was. */

typedef union {
    long long          i;
    unsigned long long u;
    long double        ld;
    void              *p;
} _fmt_slot_t;

enum {
    _K_NONE = 0, _K_INT, _K_UINT, _K_LONG, _K_ULONG,
    _K_LLONG, _K_ULLONG, _K_DBL, _K_LDBL, _K_PTR
};

#define _FMT_MAXARGS 64

/* Length-modifier encoding (dst/sdst/q follow the C99/GNU spellings). */
enum {
    _LEN_NONE = 0, _LEN_H, _LEN_HH, _LEN_L, _LEN_LL, _LEN_LD,
    _LEN_Z, _LEN_T, _LEN_J
};

/* The value kind a conversion consumes.  'm' and '%%' take no argument. */
static int _arg_kind(int conv, int len) {
    switch (conv) {
    case 'd': case 'i':
        if (len == _LEN_LL || len == _LEN_J) return _K_LLONG;
        if (len == _LEN_L  || len == _LEN_Z || len == _LEN_T) return _K_LONG;
        return _K_INT;
    case 'u': case 'o': case 'x': case 'X':
        if (len == _LEN_LL || len == _LEN_J) return _K_ULLONG;
        if (len == _LEN_L  || len == _LEN_Z || len == _LEN_T) return _K_ULONG;
        return _K_UINT;
    case 'c':
        return _K_INT;
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        return (len == _LEN_LD) ? _K_LDBL : _K_DBL;
    case 's': case 'p': case 'n':
        return _K_PTR;
    default:
        return _K_NONE;
    }
}

struct _spec {
    int zero, minus, plus, space, hash, group;
    int width, prec;                /* -1 = absent, -2 = taken from the list */
    int wpos, ppos;                 /* positional index of a '*' argument, else 0 */
    int pos;                        /* positional index of the value, else 0 */
    int len;                        /* _LEN_* */
    int conv;
};

static const char *_scan_uint(const char *f, int *out) {
    int n = 0;
    while (*f >= '0' && *f <= '9') { n = n * 10 + (*f - '0'); f++; }
    *out = n;
    return f;
}

/* Parse one directive; *pf points just past the '%'.  On success stores *s
 * and advances *pf past the conversion letter.  On a malformed directive
 * returns 0 and leaves *pf alone, so the caller can emit the '%' literally. */
static int _parse_spec(const char **pf, struct _spec *s) {
    const char *f = *pf;

    s->zero = s->minus = s->plus = s->space = s->hash = s->group = 0;
    s->width = s->prec = -1;
    s->wpos = s->ppos = s->pos = 0;
    s->len = _LEN_NONE;

    if (*f >= '1' && *f <= '9') {                 /* "%2$…" value index */
        const char *g = _scan_uint(f, &s->pos);
        if (*g == '$') f = g + 1;
        else           s->pos = 0;
    }
    for (;;) {                                    /* flags */
        if      (*f == '0') s->zero  = 1;
        else if (*f == '-') s->minus = 1;
        else if (*f == '+') s->plus  = 1;
        else if (*f == ' ') s->space = 1;
        else if (*f == '#') s->hash  = 1;
        else if (*f == '\'') s->group = 1;
        else break;
        f++;
    }
    if (*f == '*') {                              /* width */
        f++;
        if (*f >= '1' && *f <= '9') {
            const char *g = _scan_uint(f, &s->wpos);
            if (*g == '$') f = g + 1; else s->wpos = 0;
        }
        s->width = -2;
    } else if (*f >= '0' && *f <= '9') {
        f = _scan_uint(f, &s->width);
    }
    if (*f == '.') {                              /* precision */
        f++;
        if (*f == '*') {
            f++;
            if (*f >= '1' && *f <= '9') {
                const char *g = _scan_uint(f, &s->ppos);
                if (*g == '$') f = g + 1; else s->ppos = 0;
            }
            s->prec = -2;
        } else if (*f >= '0' && *f <= '9') {
            f = _scan_uint(f, &s->prec);
        } else {
            s->prec = 0;                          /* "%.-" means "%.0" */
        }
    }
    if (*f == 'h') { f++; if (*f == 'h') { s->len = _LEN_HH; f++; } else s->len = _LEN_H; }
    else if (*f == 'l') { f++; if (*f == 'l') { s->len = _LEN_LL; f++; } else s->len = _LEN_L; }
    else if (*f == 'L') { s->len = _LEN_LD; f++; }
    else if (*f == 'z') { s->len = _LEN_Z;  f++; }
    else if (*f == 't') { s->len = _LEN_T;  f++; }
    else if (*f == 'j') { s->len = _LEN_J;  f++; }
    else if (*f == 'q') { s->len = _LEN_LL; f++; }

    if (*f == '\0') return 0;
    s->conv = (unsigned char)*f;
    *pf = f + 1;
    return 1;
}

/* First walk: fill kind[] per argument position and report the highest one.
 * A position the format never names has no type; like glibc, assume pointer
 * size for it (all that is needed is to advance the list past it) instead of
 * rejecting the call. */
static void _collect_args(const char *format, unsigned char *kind, int *p_max) {
    const char *f = format;
    int argpos = 1, max = 0;

    while (*f) {
        if (*f != '%') { f++; continue; }
        f++;
        struct _spec s;
        if (!_parse_spec(&f, &s)) continue;

        if (s.width == -2) {
            int i = s.wpos ? s.wpos : argpos++;
            if (i > 0 && i < _FMT_MAXARGS) kind[i] = _K_INT;
            if (i > max) max = i;
        }
        if (s.prec == -2) {
            int i = s.ppos ? s.ppos : argpos++;
            if (i > 0 && i < _FMT_MAXARGS) kind[i] = _K_INT;
            if (i > max) max = i;
        }
        if (_arg_kind(s.conv, s.len) != _K_NONE) {
            int i = s.pos ? s.pos : argpos++;
            if (i > 0 && i < _FMT_MAXARGS) kind[i] = (unsigned char)_arg_kind(s.conv, s.len);
            if (i > max) max = i;
        } else if (s.pos > max) {
            max = s.pos;                          /* positions past a %%/%m */
        }
    }

    for (int i = 1; i <= max; i++)
        if (kind[i] == _K_NONE) kind[i] = _K_PTR;
    *p_max = max;
}

static void _fill_args(va_list args, const unsigned char *kind, int max, _fmt_slot_t *slot) {
    va_list ap;
    va_copy(ap, args);
    for (int i = 1; i <= max; i++) {
        switch (kind[i]) {
        case _K_INT:    slot[i].i  = va_arg(ap, int);               break;
        case _K_UINT:   slot[i].u  = va_arg(ap, unsigned int);      break;
        case _K_LONG:   slot[i].i  = va_arg(ap, long);              break;
        case _K_ULONG:  slot[i].u  = va_arg(ap, unsigned long);     break;
        case _K_LLONG:  slot[i].i  = va_arg(ap, long long);         break;
        case _K_ULLONG: slot[i].u  = va_arg(ap, unsigned long long); break;
        case _K_DBL:    slot[i].ld = va_arg(ap, double);            break;
        case _K_LDBL:   slot[i].ld = va_arg(ap, long double);       break;
        case _K_PTR:    slot[i].p  = va_arg(ap, void *);            break;
        default: break;
        }
    }
    va_end(ap);
}

/* Insert the locale's thousands separators into a run of decimal digits
 * (printf's `'` flag).  With the C locale's empty separator this is a plain
 * copy, so the flag is inert exactly as it is in glibc's C locale. */
static int _group_digits(char *out, int cap, const char *dig, int n,
                         const char *sep, const char *grp) {
    int m = (n < cap - 1) ? n : cap - 1;
    if (!sep || !sep[0] || !grp || !grp[0] || (unsigned char)grp[0] == 127) {
        for (int i = 0; i < m; i++) out[i] = dig[i];
        out[m] = '\0';
        return m;
    }
    int glen = (int)strlen(sep);
    int group = (unsigned char)grp[0];
    if (group <= 0) group = 1;

    char rev[80];
    int r = 0, since = 0;
    for (int i = n - 1; i >= 0 && r < (int)sizeof(rev) - glen - 1; i--) {
        if (since == group) {
            for (int j = glen - 1; j >= 0; j--) rev[r++] = sep[j];
            since = 0;
        }
        rev[r++] = dig[i];
        since++;
    }
    m = (r < cap - 1) ? r : cap - 1;
    for (int i = 0; i < m; i++) out[i] = rev[r - 1 - i];
    out[m] = '\0';
    return m;
}

/* ── the one formatter ────────────────────────────────────────────────────
 * The whole printf family is produced here, once.  Only the sink differs:
 *
 *   vfprintf  -> _file_sink_put   : write(2) per buffer flush (it used to be
 *                                   one write per *character*)
 *   vsnprintf -> _buf_sink_put    : copies what fits, counts everything
 *
 * Keeping two near-identical copies of this engine (a FILE one and a buffer
 * one) is how %i and %* went missing from both for so long, so there is now
 * exactly one, writing through `_o_char` into a coalescing staging buffer.
 */

typedef struct {
    void (*put)(void *ctx, const char *s, size_t n);
    void *ctx;
    int   count;                        /* bytes handed to the sink */
    char  stage[128];
    int   n;
} _fmt_out_t;

static void _o_flush(_fmt_out_t *o) {
    if (o->n) {
        o->put(o->ctx, o->stage, (size_t)o->n);
        o->count += o->n;
        o->n = 0;
    }
}

static void _o_char(_fmt_out_t *o, int c) {
    if (o->n == (int)sizeof(o->stage)) _o_flush(o);
    o->stage[o->n++] = (char)c;
}

static int _vfmt_core(_fmt_out_t *o, const char *format, va_list args) {
    unsigned char kind[_FMT_MAXARGS];
    _fmt_slot_t   slot[_FMT_MAXARGS];
    int max = 0;

    memset(kind, 0, sizeof kind);
    _collect_args(format, kind, &max);
    if (max > 0) _fill_args(args, kind, max, slot);

    int argpos = 1;
    const char *f = format;
    while (*f) {
        if (*f != '%') { _o_char(o, *f++); continue; }
        f++;

        struct _spec s;
        if (!_parse_spec(&f, &s)) { _o_char(o, '%'); continue; }

        int zero = s.zero, minus = s.minus, hash = s.hash, group = s.group;
        int plus = s.plus, space = s.space;
        int width = s.width, prec = s.prec;
        if (width == -2) {                        /* from the argument list */
            width = s.wpos ? slot[s.wpos].i : slot[argpos++].i;
            if (width < 0) { minus = 1; width = -width; }
        } else if (width < 0) {
            width = 0;                            /* the width was simply absent */
        }
        if (prec == -2) {
            prec = s.ppos ? slot[s.ppos].i : slot[argpos++].i;
            if (prec < 0) prec = -1;              /* a negative * precision means "none" */
        } else if (prec < 0) {
            prec = -1;
        }

        int conv = s.conv;
        int k = _arg_kind(conv, s.len);
        int vpos = 0;
        if (k != _K_NONE) vpos = s.pos ? s.pos : argpos++;

        char tmp[80];
        int len = 0, neg = 0, sign_len = 0;
        char *sval = 0;
        int cval = 0;

        switch (conv) {
        case 'd': case 'i': {
            long long v = slot[vpos].i;
            if      (s.len == _LEN_HH) v = (signed char)v;
            else if (s.len == _LEN_H)  v = (short)v;
            unsigned long long u;
            if (v < 0) { neg = 1; u = (unsigned long long)(-(v + 1)) + 1ULL; }
            else       { u = (unsigned long long)v; }
            if (s.len == _LEN_LL || s.len == _LEN_J) len = _fmt_u64(u, 10, 0, tmp);
            else                                     len = _fmt_uint((unsigned long)u, 10, 0, tmp);
            break;
        }
        case 'u': {
            unsigned long long v = slot[vpos].u;
            if      (s.len == _LEN_HH) v = (unsigned char)v;
            else if (s.len == _LEN_H)  v = (unsigned short)v;
            if (s.len == _LEN_LL || s.len == _LEN_J) len = _fmt_u64(v, 10, 0, tmp);
            else                                     len = _fmt_uint((unsigned long)v, 10, 0, tmp);
            break;
        }
        case 'o': {
            unsigned long long v = slot[vpos].u;
            if (s.len == _LEN_LL || s.len == _LEN_J) len = _fmt_u64(v, 8, 0, tmp);
            else                                     len = _fmt_uint((unsigned long)v, 8, 0, tmp);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v = slot[vpos].u;
            if (s.len == _LEN_LL || s.len == _LEN_J) len = _fmt_u64(v, 16, conv == 'X', tmp);
            else                                     len = _fmt_uint((unsigned long)v, 16, conv == 'X', tmp);
            break;
        }
        case 'p': {
            void *pv = slot[vpos].p;
            if (!pv) {                            /* glibc prints "(nil)" for NULL */
                tmp[0] = '('; tmp[1] = 'n'; tmp[2] = 'i'; tmp[3] = 'l'; tmp[4] = ')';
                len = 5;
            } else {
                tmp[0] = '0'; tmp[1] = 'x';
                len = _fmt_uint((unsigned long)(uintptr_t)pv, 16, 0, tmp + 2) + 2;
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double dv = (double)slot[vpos].ld;
            if (dv < 0 || (dv == 0 && 1.0 / dv < 0)) { neg = 1; dv = -dv; }
            len = _fmt_double(dv, prec < 0 ? 6 : prec, conv, tmp, (int)sizeof(tmp));
            if (len < 0) len = 0;
            break;
        }
        case 'c':
            cval = slot[vpos].i;
            len = 1;
            break;
        case 's':
            sval = (char *)slot[vpos].p;
            if (!sval) sval = "(null)";
            len = strlen(sval);
            break;
        case 'n': {
            /* No output: the count of bytes emitted so far goes through the
               pointer.  The staged run has not been handed to the sink yet, so
               it counts too. */
            int nc = o->count + o->n;
            void *p = slot[vpos].p;
            if (p) {
                if      (s.len == _LEN_HH) *(signed char *)p = (signed char)nc;
                else if (s.len == _LEN_H)  *(short *)p       = (short)nc;
                else if (s.len == _LEN_LL) *(long long *)p   = (long long)nc;
                else if (s.len == _LEN_L || s.len == _LEN_Z ||
                         s.len == _LEN_T || s.len == _LEN_J) *(long *)p = (long)nc;
                else                       *(int *)p         = nc;
            }
            continue;
        }
        case 'm':                                  /* GNU: strerror(errno) */
            sval = strerror(errno);
            if (!sval) sval = "Unknown error";
            len = (int)strlen(sval);
            break;
        case '%':
            _o_char(o, '%');
            continue;
        default:
            _o_char(o, '%');
            _o_char(o, conv);
            continue;
        }

        /* precision: max characters for strings, min digits for numbers */
        if (conv == 's' || conv == 'm') {
            if (prec >= 0 && len > prec) len = prec;
        } else if (prec >= 0 && (conv == 'd' || conv == 'i' || conv == 'u' ||
                                 conv == 'o' || conv == 'x' || conv == 'X')) {
            zero = 0;                          /* for integers precision cancels the 0 flag */
            if (prec == 0 && len == 1 && tmp[0] == '0') {
                len = 0;                       /* %.0d of zero — nothing (as in C) */
            } else if (prec > len) {
                int z = prec - len;
                if (z > (int)sizeof(tmp) - 2) z = (int)sizeof(tmp) - 2;
                for (int i = len - 1; i >= 0; i--) tmp[i + z] = tmp[i];
                for (int i = 0; i < z; i++) tmp[i] = '0';
                len += z;
            }
        }

        if (conv == 's' || conv == 'c' || conv == 'm') zero = 0;

        /* # on a float keeps the decimal point even at precision 0 ("%.0f" of
           5 is "5", but "%#.0f" is "5."); inf/nan carry no digits and are left
           alone, as glibc prints them. */
        if (hash && (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                     conv == 'g' || conv == 'G') && len > 0) {
            int has_point = 0, has_digit = 0;
            for (int i = 0; i < len; i++) {
                if (tmp[i] == '.') has_point = 1;
                if (tmp[i] >= '0' && tmp[i] <= '9') has_digit = 1;
            }
            if (has_digit && !has_point && len < (int)sizeof(tmp) - 1) {
                int at = len;                     /* append, or sit before the exponent */
                for (int i = 0; i < len; i++)
                    if (tmp[i] == 'e' || tmp[i] == 'E') { at = i; break; }
                for (int i = len; i > at; i--) tmp[i] = tmp[i - 1];
                tmp[at] = '.';
                len++;
            }
        }

        /* ' : decimal grouping.  The C locale defines no separator, so this is
           inert until a locale supplies one (see _group_digits). */
        if (group && (conv == 'd' || conv == 'i' || conv == 'u')) {
            struct lconv *lc = localeconv();
            if (lc && lc->thousands_sep && lc->thousands_sep[0]) {
                char g[80];
                len = _group_digits(g, (int)sizeof(g), tmp, len,
                                    lc->thousands_sep, lc->grouping);
                for (int i = 0; i < len; i++) tmp[i] = g[i];
            }
        }

        /* # : for %o the leading zero is a digit, for %x/%X it is a prefix, and only
           for a nonzero value (as in C).  Width is counted together with the prefix. */
        char pfx[3] = {0};
        int pfx_len = 0;
        if (hash && conv == 'o' && len >= 0) {
            int nonzero = 0;
            for (int i = 0; i < len; i++) if (tmp[i] != '0') { nonzero = 1; break; }
            if (len == 0) { tmp[0] = '0'; len = 1; }
            else if (nonzero && tmp[0] != '0' && len < (int)sizeof(tmp) - 1) {
                for (int i = len - 1; i >= 0; i--) tmp[i + 1] = tmp[i];
                tmp[0] = '0';
                len++;
            }
        } else if (hash && (conv == 'x' || conv == 'X')) {
            int nonzero = 0;
            for (int i = 0; i < len; i++) if (tmp[i] != '0') { nonzero = 1; break; }
            if (nonzero) {
                pfx[0] = '0';
                pfx[1] = (conv == 'X') ? 'X' : 'x';
                pfx_len = 2;
            }
        }

        /* + and space force a sign on the conversions that can be negative. */
        char sgn = 0;
        if (conv == 'd' || conv == 'i' || conv == 'f' || conv == 'F' ||
            conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') {
            if      (neg)   { sgn = '-'; sign_len = 1; }
            else if (plus)  { sgn = '+'; sign_len = 1; }
            else if (space) { sgn = ' '; sign_len = 1; }
        }

        int pad = width - (len + sign_len + pfx_len);
        if (pad < 0) pad = 0;

        if (minus) {
            if (sgn) _o_char(o, sgn);
            for (int i = 0; i < pfx_len; i++) _o_char(o, pfx[i]);
        } else if (zero) {
            if (sgn) _o_char(o, sgn);
            for (int i = 0; i < pfx_len; i++) _o_char(o, pfx[i]);
            for (int i = 0; i < pad; i++) _o_char(o, '0');
        } else {
            for (int i = 0; i < pad; i++) _o_char(o, ' ');
            if (sgn) _o_char(o, sgn);
            for (int i = 0; i < pfx_len; i++) _o_char(o, pfx[i]);
        }

        if (conv == 'c') {
            _o_char(o, cval);
        } else if (conv == 's' || conv == 'm') {
            for (int i = 0; i < len; i++) _o_char(o, sval[i]);
        } else {
            for (int i = 0; i < len; i++) _o_char(o, tmp[i]);
        }
        if (minus) {
            for (int i = 0; i < pad; i++) _o_char(o, ' ');
        }
    }
    _o_flush(o);
    return o->count;
}

/* FILE sink: hand the staged run to write(2), retrying a short write. */
static void _file_sink_put(void *ctx, const char *s, size_t n) {
    FILE *f = (FILE *)ctx;
    while (n > 0) {
        ssize_t w = write(f->fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    _fmt_out_t o;
    o.put   = _file_sink_put;
    o.ctx   = stream;
    o.count = 0;
    o.n     = 0;
    return _vfmt_core(&o, format, args);
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stream, format, args);
    va_end(args);
    return ret;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stdout, format, args);
    va_end(args);
    return ret;
}

/* Buffer sink: copy what fits, keep counting past the end, so vsnprintf's
 * return value is the length the call *would* have produced (as C requires). */
typedef struct {
    char  *dst;
    size_t size;
    size_t pos;
} _buf_ctx_t;

static void _buf_sink_put(void *ctx, const char *s, size_t n) {
    _buf_ctx_t *b = (_buf_ctx_t *)ctx;
    for (size_t i = 0; i < n; i++) {
        if (b->size > 0 && b->pos < b->size) b->dst[b->pos] = s[i];
        b->pos++;
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    _buf_ctx_t b;
    _fmt_out_t o;
    b.dst   = str;
    b.size  = size;
    b.pos   = 0;
    o.put   = _buf_sink_put;
    o.ctx   = &b;
    o.count = 0;
    o.n     = 0;
    _vfmt_core(&o, format, ap);
    if (size > 0) str[b.pos < size ? b.pos : size - 1] = '\0';
    return (int)b.pos;
}


int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int sprintf(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, (size_t)-1, format, args);
    va_end(args);
    return ret;
}

/* ── printf to a fresh buffer, and printf to a descriptor ───────────────── */

int vasprintf(char **strp, const char *format, va_list ap) {
    if (!strp) { errno = EINVAL; return -1; }

    /* The length first (a size-0 call only counts), then one real pass. */
    va_list probe;
    va_copy(probe, ap);
    int need = vsnprintf(0, 0, format, probe);
    va_end(probe);
    if (need < 0) { *strp = 0; return -1; }

    char *buf = (char *)malloc((size_t)need + 1);
    if (!buf) { *strp = 0; errno = ENOMEM; return -1; }

    int got = vsnprintf(buf, (size_t)need + 1, format, ap);
    if (got < 0) { free(buf); *strp = 0; return -1; }
    *strp = buf;
    return got;
}

int asprintf(char **strp, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vasprintf(strp, format, args);
    va_end(args);
    return ret;
}

int vdprintf(int fd, const char *format, va_list ap) {
    /* The formatter only ever reads stream->fd, so a stack FILE is a complete
     * sink for a descriptor. */
    FILE f;
    f.fd       = fd;
    f.flags    = 0;
    f.ungotten = 0;
    f.buf[0]   = 0;
    return vfprintf(&f, format, ap);
}

int dprintf(int fd, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vdprintf(fd, format, args);
    va_end(args);
    return ret;
}

float strtof(const char *str, char **endptr) {
    return (float)strtod(str, endptr);
}

#define POPEN_MAX 8
static FILE *_pop_f[POPEN_MAX];
static pid_t _pop_pid[POPEN_MAX];

FILE *popen(const char *command, const char *type) {
    if (!command || !type || (type[0] != 'r' && type[0] != 'w') || type[1] == '+')
        return 0;
    if (access("/usr/bin/cactsole", X_OK) != 0) {
        errno = ENOENT;
        return 0;
    }
    int fds[2];
    if (pipe(fds) != 0) return 0;
    int wr = (type[0] == 'w');

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        if (wr) dup2(fds[0], 0);
        else    dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        char *argv[] = { "cactsole", "-c", (char *)command, 0 };
        execve("/usr/bin/cactsole", argv, environ);
        _exit(127);
    }

    int keep = wr ? fds[1] : fds[0];
    close(wr ? fds[0] : fds[1]);

    FILE *f = fdopen(keep, wr ? "w" : "r");
    if (!f) {
        close(keep);
        waitpid(pid, 0, 0);
        return 0;
    }
    for (int i = 0; i < POPEN_MAX; i++) {
        if (_pop_pid[i] == 0) {
            _pop_f[i] = f;
            _pop_pid[i] = pid;
            break;
        }
    }
    return f;
}

int pclose(FILE *stream) {
    pid_t pid = 0;
    for (int i = 0; i < POPEN_MAX; i++) {
        if (_pop_f[i] == stream) {
            pid = _pop_pid[i];
            _pop_f[i] = 0;
            _pop_pid[i] = 0;
            break;
        }
    }
    int st = fclose(stream);
    if (pid) {
        int s = 0;
        waitpid(pid, &s, 0);
        return s;
    }
    return st;
}

/* CactOS: no dynamic memory FILEs. open_memstream() is only used by debug
 * logging; returning NULL makes callers bail out. */
FILE *open_memstream(char **ptr, size_t *sizeloc) {
    (void)ptr;
    (void)sizeloc;
    return 0;
}
