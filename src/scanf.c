#include "stdio.h"
#include "ctype.h"
#include "stdlib.h"
#include "errno.h"
#include <stdarg.h>
#include <stdint.h>

typedef struct {
    FILE        *f;
    const char  *p;
    int          from_file;
} _src_t;

static int _s_get(_src_t *s) {
    if (s->from_file) return fgetc(s->f);
    if (s->p && *s->p) return (unsigned char)*s->p++;
    return EOF;
}

static void _s_unget(_src_t *s, int c) {
    if (c == EOF) return;
    if (s->from_file) { ungetc(c, s->f); return; }
    if (s->p) s->p--;
}

static void _skip_ws(_src_t *s) {
    int c;
    while ((c = _s_get(s)) != EOF && isspace(c)) {}
    if (c != EOF) _s_unget(s, c);
}

static int _vscan(_src_t *s, const char *format, va_list ap) {
    int count = 0;

    for (; *format; format++) {
        if (isspace((unsigned char)*format)) {
            _skip_ws(s);
            continue;
        }
        if (*format != '%') {
            int c = _s_get(s);
            if (c != (unsigned char)*format) {
                if (c != EOF) _s_unget(s, c);
                break;
            }
            continue;
        }

        format++;
        int suppress = 0, width = 0;
        if (*format == '*') { suppress = 1; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        char conv = *format;

        if (conv == '%') {
            int c = _s_get(s);
            if (c != '%') { if (c != EOF) _s_unget(s, c); break; }
            continue;
        }

        if (conv == 'c') {
            int n = width ? width : 1;
            char *dst = suppress ? 0 : va_arg(ap, char *);
            int got = 0;
            while (got < n) {
                int c = _s_get(s);
                if (c == EOF) break;
                if (dst) dst[got] = (char)c;
                got++;
            }
            if (got == 0) break;
            if (!suppress) count++;
            continue;
        }

        _skip_ws(s);

        if (conv == 's') {
            int max = width ? width : 0x7fffffff;
            char *dst = suppress ? 0 : va_arg(ap, char *);
            int got = 0, c;
            while (got < max && (c = _s_get(s)) != EOF && !isspace(c)) {
                if (dst) dst[got] = (char)c;
                got++;
            }
            if (got == 0) break;
            if (dst) dst[got] = '\0';
            if (!suppress) count++;
            continue;
        }

        unsigned int base;
        int allow_sign;
        switch (conv) {
        case 'd': case 'i': base = 10; allow_sign = 1; break;
        case 'u':           base = 10; allow_sign = 0; break;
        case 'o':           base = 8;  allow_sign = 0; break;
        case 'x': case 'X': base = 16; allow_sign = 0; break;
        case 'f': case 'e': case 'g': {
            char tok[128];
            int n = 0, c;
            int max = width ? width : (int)sizeof(tok) - 1;
            while (n < max && (c = _s_get(s)) != EOF && !isspace(c)) tok[n++] = (char)c;
            if (n == 0) break;
            tok[n] = '\0';
            if (!suppress) {
                double *d = va_arg(ap, double *);
                *d = strtod(tok, 0);
                count++;
            }
            continue;
        }
        default:
            return count;
        }

        int c = _s_get(s);
        int neg = 0;
        if (allow_sign && (c == '-' || c == '+')) {
            neg = (c == '-');
            c = _s_get(s);
        }
        int dv = -1;
        if (c >= '0' && c <= '9') dv = c - '0';
        else if (c >= 'a' && c <= 'f') dv = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') dv = c - 'A' + 10;
        if (c == EOF || dv < 0 || dv >= (int)base) {
            if (c != EOF) _s_unget(s, c);
            break;
        }
        unsigned long long val = (unsigned long long)dv;
        int n = 1;
        int max = width ? width : 0x7fffffff;
        while (n < max) {
            c = _s_get(s);
            dv = -1;
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (c >= 'a' && c <= 'f') dv = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') dv = c - 'A' + 10;
            if (c == EOF || dv < 0 || dv >= (int)base) {
                if (c != EOF) _s_unget(s, c);
                break;
            }
            val = val * (unsigned long long)base + (unsigned long long)dv;
            n++;
        }
        if (!suppress) {
            if (conv == 'd' || conv == 'i') {
                long long sv = neg ? -(long long)val : (long long)val;
                if (conv == 'i') {
                    /* keep simple: treated as decimal */
                }
                *va_arg(ap, int *) = (int)sv;
            } else {
                *va_arg(ap, unsigned int *) = (unsigned int)val;
            }
            count++;
        }
    }
    return count;
}

int vfscanf(FILE *stream, const char *format, va_list ap) {
    _src_t s;
    s.from_file = 1;
    s.f = stream;
    s.p = 0;
    return _vscan(&s, format, ap);
}

int vsscanf(const char *str, const char *format, va_list ap) {
    _src_t s;
    s.from_file = 0;
    s.f = 0;
    s.p = str;
    return _vscan(&s, format, ap);
}

int fscanf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vfscanf(stream, format, ap);
    va_end(ap);
    return r;
}

int scanf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vfscanf(stdin, format, ap);
    va_end(ap);
    return r;
}

int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vsscanf(str, format, ap);
    va_end(ap);
    return r;
}
