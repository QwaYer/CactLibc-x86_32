/*
 * wordexp: the shell's expansions applied to a string, in-process.
 *
 *   tilde          ~ and ~/ -> $HOME
 *   parameter      $NAME, ${NAME}, $?, $$
 *   command        $(...) and `...` — run through the CactOS shell (popen)
 *   field split    unquoted expansion output is split on IFS whitespace
 *   pathname       * ? [ ... ] through glob(); a non-matching word is kept
 *   quote removal  '...' literal, "..." with \ $ ` active, \ escapes
 *
 * Arithmetic $((...)) is evaluated by a small recursive-descent evaluator (see
 * _eval_arith), which also evaluates the offset/length of a ${name:off:len}
 * substring.
 */

#include "wordexp.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "errno.h"
#include "glob.h"
#include "fnmatch.h"
#include "stdio.h"

/* ── small dynamic buffer ───────────────────────────────────────────────── */

typedef struct { char *p; size_t len, cap; } sbuf;

static int sb_reserve(sbuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t nc = b->cap ? b->cap * 2 : 64;
    while (nc < b->len + extra + 1) nc *= 2;
    char *np = realloc(b->p, nc);
    if (!np) return -1;
    b->p = np; b->cap = nc;
    return 0;
}

static int sb_putc(sbuf *b, char c) {
    if (sb_reserve(b, 1)) return -1;
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
    return 0;
}

static int sb_write(sbuf *b, const char *s, size_t n) {
    if (sb_reserve(b, n)) return -1;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}

static void sb_free(sbuf *b) { free(b->p); b->p = 0; b->len = b->cap = 0; }

/* ── growable word vector ───────────────────────────────────────────────── */

typedef struct { char **v; size_t n, cap; } wlist;

static int wl_reserve(wlist *w, size_t extra) {
    if (w->n + extra + 1 <= w->cap) return 0;
    size_t nc = w->cap ? w->cap * 2 : 16;
    while (nc < w->n + extra + 1) nc *= 2;
    char **nv = realloc(w->v, nc * sizeof(char *));
    if (!nv) return -1;
    w->v = nv; w->cap = nc;
    return 0;
}

/* Push a copy of s. */
static int wl_push_str(wlist *w, const char *s) {
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) return -1;
    memcpy(c, s, n);
    if (wl_reserve(w, 1)) { free(c); return -1; }
    w->v[w->n++] = c;
    return 0;
}

/* Take ownership of b's buffer (b is emptied).  An empty buffer still yields
 * a word, so "" and '' produce an empty field. */
static int wl_take(wlist *w, sbuf *b) {
    if (!b->p) {
        char *e = malloc(1);
        if (!e) return -1;
        e[0] = '\0';
        if (wl_reserve(w, 1)) { free(e); return -1; }
        w->v[w->n++] = e;
        return 0;
    }
    if (wl_reserve(w, 1)) return -1;
    w->v[w->n++] = b->p;
    b->p = 0; b->len = b->cap = 0;
    return 0;
}

static void wl_free(wlist *w) {
    for (size_t i = 0; i < w->n; i++) free(w->v[i]);
    free(w->v);
    w->v = 0; w->n = w->cap = 0;
}

/* ── character classes ──────────────────────────────────────────────────── */

static int ifs_ws(int c)   { return c == ' ' || c == '\t' || c == '\n'; }
static int name_st(int c)  { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int name_ch(int c)  { return name_st(c) || (c >= '0' && c <= '9'); }
static int glob_meta(int c){ return c == '*' || c == '?' || c == '['; }

/* ── one field ──────────────────────────────────────────────────────────── */

typedef struct {
    sbuf field;
    int  started;               /* a field is in progress (so "" is a word) */
    int  meta;                  /* holds an unquoted glob metacharacter */
} fld;

static int fld_putc(fld *f, char c) { f->started = 1; return sb_putc(&f->field, c); }

static int fld_write(fld *f, const char *s, size_t n) {
    if (n) f->started = 1;
    return sb_write(&f->field, s, n);
}

/* Append an expansion's text.  When split is set (unquoted expansion) the
 * text is field-split on IFS whitespace, exactly as the shell would. */
static int fld_expand_text(fld *f, wlist *out, const char *s, int split) {
    if (!split) { return fld_write(f, s, strlen(s)); }
    for (; *s; s++) {
        if (ifs_ws((unsigned char)*s)) {
            if (f->started) {
                if (wl_take(out, &f->field)) return -1;
                f->started = 0; f->meta = 0;
            }
        } else if (fld_putc(f, *s)) {
            return -1;
        }
    }
    return 0;
}

/* ── arithmetic expansion: $(( expr )) ────────────────────────────────────
 * A recursive-descent evaluator over `long long`, in C's own precedence order
 * (`|| && | ^ & == != < <= > >= << >> + - * / %`, unary `+ - ! ~`, parentheses).
 * A bare name or `$name` is a variable and its value is read as a decimal
 * number (0 when unset or not a number).  Note POSIX requires the variables to
 * be expanded here; glibc's own wordexp reports WRDE_SYNTAX for them, which
 * this deliberately does not copy. */

struct arith { const char *p; int err; };

static void _a_skip(struct arith *a) {
    while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n') a->p++;
}

static int _is_hex(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static long long _hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static long long _a_bin(struct arith *a, int lv);

static long long _a_prim(struct arith *a) {
    _a_skip(a);
    if (*a->p == '(') {
        long long v;
        a->p++;
        v = _a_bin(a, 0);
        _a_skip(a);
        if (*a->p != ')') { a->err = 1; return 0; }
        a->p++;
        return v;
    }
    if (name_st((unsigned char)*a->p) || *a->p == '$') {
        char name[128];
        size_t n = 0;
        const char *v;
        long long r = 0;
        if (*a->p == '$') { a->p++; if (*a->p == '{') a->p++; }
        while (name_ch((unsigned char)*a->p) && n < sizeof name - 1) name[n++] = *a->p++;
        name[n] = '\0';
        if (*a->p == '}') a->p++;
        v = getenv(name);
        if (v) {
            int neg = 0;
            if (*v == '-') { neg = 1; v++; }
            else if (*v == '+') v++;
            while (*v >= '0' && *v <= '9') { r = r * 10 + (*v - '0'); v++; }
            if (neg) r = -r;
        }
        return r;
    }
    if (*a->p >= '0' && *a->p <= '9') {
        long long v = 0;
        if (a->p[0] == '0' && (a->p[1] == 'x' || a->p[1] == 'X')) {
            a->p += 2;
            while (_is_hex((unsigned char)*a->p)) { v = v * 16 + _hexval((unsigned char)*a->p); a->p++; }
        } else if (a->p[0] == '0' && a->p[1] >= '0' && a->p[1] <= '7') {
            a->p++;
            while (*a->p >= '0' && *a->p <= '7') { v = v * 8 + (*a->p - '0'); a->p++; }
        } else {
            while (*a->p >= '0' && *a->p <= '9') { v = v * 10 + (*a->p - '0'); a->p++; }
        }
        return v;
    }
    a->err = 1;
    return 0;
}

static long long _a_unary(struct arith *a) {
    _a_skip(a);
    if (*a->p == '-') { a->p++; return -_a_unary(a); }
    if (*a->p == '+') { a->p++; return  _a_unary(a); }
    if (*a->p == '!') { a->p++; return !_a_unary(a); }
    if (*a->p == '~') { a->p++; return ~_a_unary(a); }
    return _a_prim(a);
}

static long long _a_bin(struct arith *a, int lv) {
    long long v = (lv == 9) ? _a_unary(a) : _a_bin(a, lv + 1);

    for (;;) {
        int c1, c2;
        long long r;
        if (a->err) return v;
        _a_skip(a);
        c1 = (unsigned char)a->p[0];
        c2 = (unsigned char)a->p[1];

        switch (lv) {
        case 0: if (!(c1 == '|' && c2 == '|')) return v; a->p += 2; r = _a_bin(a, 1); v = (v || r); break;
        case 1: if (!(c1 == '&' && c2 == '&')) return v; a->p += 2; r = _a_bin(a, 2); v = (v && r); break;
        case 2: if (c1 != '|' || c2 == '|') return v; a->p++; r = _a_bin(a, 3); v |= r; break;
        case 3: if (c1 != '^') return v; a->p++; r = _a_bin(a, 4); v ^= r; break;
        case 4: if (c1 != '&' || c2 == '&') return v; a->p++; r = _a_bin(a, 5); v &= r; break;
        case 5:
            if (c1 == '=' && c2 == '=')      { a->p += 2; r = _a_bin(a, 6); v = (v == r); }
            else if (c1 == '!' && c2 == '=') { a->p += 2; r = _a_bin(a, 6); v = (v != r); }
            else return v;
            break;
        case 6:
            if (c1 == '<' && c2 == '=')      { a->p += 2; r = _a_bin(a, 7); v = (v <= r); }
            else if (c1 == '>' && c2 == '=') { a->p += 2; r = _a_bin(a, 7); v = (v >= r); }
            else if (c1 == '<' && c2 != '<') { a->p++;    r = _a_bin(a, 7); v = (v < r); }
            else if (c1 == '>' && c2 != '>') { a->p++;    r = _a_bin(a, 7); v = (v > r); }
            else return v;
            break;
        case 7:
            if (c1 == '<' && c2 == '<')      { a->p += 2; r = _a_bin(a, 8); v = v << (r & 63); }
            else if (c1 == '>' && c2 == '>') { a->p += 2; r = _a_bin(a, 8); v = v >> (r & 63); }
            else return v;
            break;
        case 8:
            if (c1 == '+')      { a->p++; r = _a_bin(a, 9); v += r; }
            else if (c1 == '-') { a->p++; r = _a_bin(a, 9); v -= r; }
            else return v;
            break;
        default:
            if (c1 == '*') { a->p++; r = _a_unary(a); v *= r; }
            else if (c1 == '/' || c1 == '%') {
                long long d;
                int op = c1;
                a->p++;
                d = _a_unary(a);
                if (d == 0) { a->err = 1; return 0; }
                v = (op == '/') ? v / d : v % d;
            }
            else return v;
            break;
        }
    }
}

/* Evaluate a `$(( … ))` body; 0 and *err set on anything malformed. */
static long long _eval_arith(const char *body, int *err) {
    struct arith a;
    long long v;
    a.p = body;
    a.err = 0;
    v = _a_bin(&a, 0);
    _a_skip(&a);
    if (a.err || *a.p) { *err = 1; return 0; }
    *err = 0;
    return v;
}

/* Read one expansion value for $NAME / ${NAME} / ${NAME<op>word}.  *pp points
 * at the '$'.  Returns 0 on success, -2 for a command substitution, -1 for out
 * of memory, or a WRDE_* code.
 *
 * The braced forms are the POSIX parameter expansions: `:-` / `-` / `:=` / `=`
 * / `:+` / `+` / `?`, the length `${#NAME}`, the pattern removals `#`, `##`,
 * `%`, `%%` (matched with fnmatch, shortest or longest), and the substring
 * `${NAME:off}` / `${NAME:off:len}` (off/len are arithmetic expressions). */
static int read_var(const char **pp, sbuf *val, int flags) {
    const char *p = *pp + 1;
    char name[128];
    size_t n = 0;
    int braced = 0, is_len = 0;
    const char *v;

    if (*p == '(') {
        if (p[1] == '(') {                            /* $(( expr )) */
            char body[256];
            char num[24];
            size_t k = 0;
            int depth = 0, err = 0, i = 0, j = 0;
            long long r;
            int neg;
            unsigned long long u;
            char rev[24];
            const char *q = p + 2;

            for (; *q; q++) {
                if (*q == '(') depth++;
                else if (*q == ')') {
                    if (depth == 0) break;
                    depth--;
                }
                if (k + 1 < sizeof body) body[k++] = *q;
            }
            if (*q != ')' || q[1] != ')') return WRDE_SYNTAX;
            body[k] = '\0';

            r = _eval_arith(body, &err);
            if (err) return WRDE_SYNTAX;

            neg = (r < 0);
            u = neg ? (unsigned long long)(-(r + 1)) + 1ULL : (unsigned long long)r;
            if (u == 0) rev[j++] = '0';
            while (u) { rev[j++] = (char)('0' + (int)(u % 10)); u /= 10; }
            if (neg) num[i++] = '-';
            while (j) num[i++] = rev[--j];
            num[i] = '\0';

            if (sb_write(val, num, (size_t)i)) return -1;
            *pp = q + 2;
            return 0;
        }
        *pp = p;                                      /* hand the '(' to extract_cmd */
        return -2;
    }
    if (*p == '{') { p++; braced = 1; }

    if (braced && *p == '#') { is_len = 1; p++; }

    if (*p == '?' || *p == '$' || *p == '!') {
        if (*p == '$') {                              /* $$ — the pid */
            char num[16];
            int x = (int)getpid(), i = 0;
            if (x == 0) num[i++] = '0';
            while (x > 0 && i < (int)sizeof num) { num[i++] = (char)('0' + x % 10); x /= 10; }
            for (int a = 0, b = i - 1; a < b; a++, b--) { char t = num[a]; num[a] = num[b]; num[b] = t; }
            num[i] = '\0';
            if (sb_write(val, num, (size_t)i)) return -1;
        }
        if (braced) { p++; if (*p != '}') return WRDE_SYNTAX; p++; }
        else p++;
        *pp = p;
        return 0;                                     /* $? $! : empty */
    }

    while (name_ch((unsigned char)*p) && n < sizeof name - 1) name[n++] = *p++;
    name[n] = '\0';
    if (n == 0 && !braced) {                          /* a lone '$' is literal */
        if (sb_putc(val, '$')) return -1;
        *pp = p;
        return 0;
    }

    v = getenv(name);

    if (!braced) {
        if (!v) {
            if (flags & WRDE_UNDEF) return WRDE_BADVAL;
            v = "";
        }
        if (sb_write(val, v, strlen(v))) return -1;
        *pp = p;
        return 0;
    }

    if (is_len) {                                     /* ${#NAME} */
        char num[16];
        size_t len = v ? strlen(v) : 0, i = 0;
        if (len == 0) num[i++] = '0';
        while (len > 0 && i < sizeof num) { num[i++] = (char)('0' + len % 10); len /= 10; }
        for (size_t a = 0, b = i - 1; a < b; a++, b--) { char t = num[a]; num[a] = num[b]; num[b] = t; }
        if (*p != '}') return WRDE_SYNTAX;
        if (sb_write(val, num, i)) return -1;
        *pp = p + 1;
        return 0;
    }

    if (*p == '}') {                                  /* ${NAME} */
        if (!v) {
            if (flags & WRDE_UNDEF) return WRDE_BADVAL;
            v = "";
        }
        if (sb_write(val, v, strlen(v))) return -1;
        *pp = p + 1;
        return 0;
    }

    /* ${NAME<op>word} */
    {
        char op = *p++;
        int colon = 0, longest = 0;
        char word[256];
        size_t w = 0;
        int set  = (v != 0);
        int nonnull = set && v[0] != '\0';

        if (op == ':') {
            colon = 1;
            if (*p == '-' || *p == '=' || *p == '+' || *p == '?') {
                op = *p++;
            } else {
                /* ${NAME:offset[:length]} — the POSIX substring expansion.  The
                 * offset and length are arithmetic expressions, evaluated by the
                 * very evaluator $(( )) uses (POSIX requires this; glibc's own
                 * wordexp rejects it with WRDE_SYNTAX, which this deliberately
                 * does not copy).  A negative offset counts from the end of the
                 * value, a negative length is a syntax error. */
                char ebuf[256];
                long long off, len = -1;
                size_t e, slen, start, count;
                const char *s;
                int err = 0, depth;

                if (!v) { if (flags & WRDE_UNDEF) return WRDE_BADVAL; v = ""; }
                s = v;
                slen = strlen(s);

                for (e = 0, depth = 0; *p && *p != '}'; p++) {
                    if (*p == '(') depth++;
                    else if (*p == ')') { if (depth == 0) return WRDE_SYNTAX; depth--; }
                    else if (*p == ':' && depth == 0) break;
                    if (e + 1 >= sizeof ebuf) return WRDE_SYNTAX;
                    ebuf[e++] = *p;
                }
                if (depth != 0) return WRDE_SYNTAX;
                ebuf[e] = '\0';
                off = _eval_arith(ebuf, &err);
                if (err) return WRDE_SYNTAX;

                if (*p == ':') {                        /* an explicit length */
                    p++;
                    for (e = 0, depth = 0; *p && *p != '}'; p++) {
                        if (*p == '(') depth++;
                        else if (*p == ')') { if (depth == 0) return WRDE_SYNTAX; depth--; }
                        if (e + 1 >= sizeof ebuf) return WRDE_SYNTAX;
                        ebuf[e++] = *p;
                    }
                    if (depth != 0) return WRDE_SYNTAX;
                    ebuf[e] = '\0';
                    len = _eval_arith(ebuf, &err);
                    if (err || len < 0) return WRDE_SYNTAX;
                }
                if (*p != '}') return WRDE_SYNTAX;

                if (off < 0) {
                    long long st = (long long)slen + off;
                    start = (st < 0) ? 0 : ((size_t)st > slen ? slen : (size_t)st);
                } else {
                    start = ((unsigned long long)off > slen) ? slen : (size_t)off;
                }
                count = slen - start;
                if (len >= 0 && (long long)count > len) count = (size_t)len;
                if (sb_write(val, s + start, count)) return -1;
                *pp = p + 1;
                return 0;
            }
        }
        if ((op == '#' || op == '%') && *p == op) { longest = 1; p++; }

        while (*p && *p != '}' && w + 1 < sizeof word) word[w++] = *p++;
        word[w] = '\0';
        if (*p != '}') return WRDE_SYNTAX;

        switch (op) {
        case '-': case '=':
            if (colon ? !nonnull : !set) {
                if (op == '=') setenv(name, word, 1);   /* POSIX assigns */
                if (sb_write(val, word, w)) return -1;
            } else {
                if (sb_write(val, v, strlen(v))) return -1;
            }
            break;
        case '+':
            if (colon ? nonnull : set) {
                if (sb_write(val, word, w)) return -1;
            }
            break;
        case '?':
            if (colon ? !nonnull : !set) return WRDE_BADVAL;
            if (sb_write(val, v, strlen(v))) return -1;
            break;
        case '#': case '%': {
            char tmp[256];
            const char *s = v ? v : "";
            size_t slen = strlen(s), k;
            int have = 0;
            size_t cut = 0;                             /* chars removed */

            if (slen + 1 > sizeof tmp) return WRDE_SYNTAX;
            for (k = 0; k <= slen; k++) {
                /* k = the length of the part matched at that end */
                size_t m = (op == '#') ? k : slen - k;
                memcpy(tmp, (op == '#') ? s : s + m, (op == '#') ? k : slen - m);
                tmp[(op == '#') ? k : slen - m] = '\0';
                if (fnmatch(word, tmp, 0) == 0) {
                    if (!have || (longest ? k > cut : k < cut)) { cut = k; have = 1; }
                    if (!longest) { have = 1; cut = k; break; }
                }
            }
            if (!have) {
                if (sb_write(val, s, slen)) return -1;
            } else if (op == '#') {
                if (sb_write(val, s + cut, slen - cut)) return -1;
            } else {
                if (sb_write(val, s, slen - cut)) return -1;
            }
            break;
        }
        default:
            return WRDE_SYNTAX;
        }
        *pp = p + 1;
        return 0;
    }
}

/* Run one command and return its output. */
static int run_cmd(const char *cmd, sbuf *out, int flags) {
    if (flags & WRDE_NOCMD) return WRDE_CMDSUB;
    FILE *f = popen(cmd, "r");
    if (!f) return WRDE_CMDSUB;
    char buf[512];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0)
        if (sb_write(out, buf, r)) { pclose(f); return WRDE_NOSPACE; }
    pclose(f);
    /* POSIX: the substitution's trailing newlines are removed before the
       result is used, quoted or not. */
    while (out->len > 0 && out->p[out->len - 1] == '\n') out->p[--out->len] = '\0';
    return 0;
}

/* Extract $( ... ) with balanced parentheses, or ` ... `.  *pp at the opener.
 * On success *cmd is a malloc'd command string and *pp is past the closer. */
static int extract_cmd(const char **pp, char **cmd) {
    const char *p = *pp;
    const char *start;
    int depth = 0;

    if (*p == '`') {
        p++;
        start = p;
        while (*p && *p != '`') p++;
        if (*p != '`') return WRDE_SYNTAX;
        *cmd = malloc((size_t)(p - start) + 1);
        if (!*cmd) return WRDE_NOSPACE;
        memcpy(*cmd, start, (size_t)(p - start));
        (*cmd)[p - start] = '\0';
        *pp = p + 1;
        return 0;
    }

    p++;                                  /* past '(' */
    start = p;
    for (; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth == 0) break; depth--; }
    }
    if (*p != ')') return WRDE_SYNTAX;
    *cmd = malloc((size_t)(p - start) + 1);
    if (!*cmd) return WRDE_NOSPACE;
    memcpy(*cmd, start, (size_t)(p - start));
    (*cmd)[p - start] = '\0';
    *pp = p + 1;
    return 0;
}

/* Flush the current field, applying pathname expansion if it holds a wildcard. */
static int fld_flush(fld *f, wlist *out) {
    if (!f->started) { sb_free(&f->field); return 0; }

    if (f->meta && f->field.p) {
        glob_t g;
        g.gl_pathc = 0; g.gl_pathv = 0; g.gl_offs = 0;
        int gr = glob(f->field.p, GLOB_NOCHECK | GLOB_TILDE, 0, &g);
        if (gr == GLOB_NOSPACE) { sb_free(&f->field); return WRDE_NOSPACE; }
        if (gr == 0) {
            for (size_t i = 0; i < g.gl_pathc; i++)
                if (wl_push_str(out, g.gl_pathv[i])) { globfree(&g); sb_free(&f->field); return WRDE_NOSPACE; }
            globfree(&g);
            sb_free(&f->field);
            f->started = 0; f->meta = 0;
            return 0;
        }
        /* any other glob error: fall through and keep the literal word */
    }

    if (wl_take(out, &f->field)) { sb_free(&f->field); return WRDE_NOSPACE; }
    f->started = 0; f->meta = 0;
    return 0;
}

static int expand(const char *in, wlist *out, int flags) {
    fld f;
    f.field.p = 0; f.field.len = f.field.cap = 0;
    f.started = 0; f.meta = 0;

    const char *p = in;
    sbuf val;

    while (*p) {
        int c = (unsigned char)*p;

        if (ifs_ws(c)) { if (fld_flush(&f, out)) return WRDE_NOSPACE; p++; continue; }

        if (c == '\'') {
            f.started = 1;                            /* '' is an empty word */
            p++;
            while (*p && *p != '\'') { if (fld_putc(&f, *p)) return WRDE_NOSPACE; p++; }
            if (*p != '\'') return WRDE_SYNTAX;
            p++;
            continue;
        }

        if (c == '"') {
            f.started = 1;                            /* "" is an empty word */
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    if (!*p) return WRDE_SYNTAX;
                    if (*p == '$' || *p == '`' || *p == '"' || *p == '\\' || *p == '\n')
                        { if (fld_putc(&f, *p)) return WRDE_NOSPACE; p++; }
                    else { if (fld_putc(&f, '\\')) return WRDE_NOSPACE; }
                } else if (*p == '$') {
                    val.p = 0; val.len = val.cap = 0;
                    int r = read_var(&p, &val, flags);
                    if (r == -2) {                       /* $(...) inside "" */
                        char *cmd = 0;
                        r = extract_cmd(&p, &cmd);
                        if (r == 0) { val.len = 0; if (val.p) val.p[0] = '\0';
                                      r = run_cmd(cmd, &val, flags); }
                        free(cmd);
                    }
                    if (r == -1 || r == WRDE_NOSPACE) { sb_free(&val); return WRDE_NOSPACE; }
                    if (r) { sb_free(&val); return r; }
                    if (fld_write(&f, val.p ? val.p : "", val.len)) { sb_free(&val); return WRDE_NOSPACE; }
                    sb_free(&val);
                } else if (*p == '`') {
                    char *cmd = 0;
                    int r = extract_cmd(&p, &cmd);
                    if (r == 0) {
                        val.p = 0; val.len = val.cap = 0;
                        r = run_cmd(cmd, &val, flags);
                        if (r == 0 && fld_write(&f, val.p ? val.p : "", val.len)) r = WRDE_NOSPACE;
                        sb_free(&val);
                    }
                    free(cmd);
                    if (r) return r;
                } else { if (fld_putc(&f, *p)) return WRDE_NOSPACE; p++; }
            }
            if (*p != '"') return WRDE_SYNTAX;
            p++;
            continue;
        }

        if (c == '\\') {
            p++;
            if (!*p) return WRDE_SYNTAX;
            if (fld_putc(&f, *p)) return WRDE_NOSPACE;
            p++;
            continue;
        }

        if (c == '$') {
            val.p = 0; val.len = val.cap = 0;
            int r = read_var(&p, &val, flags);
            if (r == -2) {
                char *cmd = 0;
                r = extract_cmd(&p, &cmd);
                if (r == 0) {
                    if (val.p) { val.len = 0; val.p[0] = '\0'; }
                    r = run_cmd(cmd, &val, flags);
                    if (r == 0 && fld_expand_text(&f, out, val.p ? val.p : "", 1)) r = WRDE_NOSPACE;
                }
                free(cmd);
                sb_free(&val);
                if (r) return r;
                continue;
            }
            if (r == -1 || r == WRDE_NOSPACE) { sb_free(&val); return WRDE_NOSPACE; }
            if (r) { sb_free(&val); return r; }
            if (fld_expand_text(&f, out, val.p ? val.p : "", 1)) { sb_free(&val); return WRDE_NOSPACE; }
            sb_free(&val);
            continue;
        }

        if (c == '`') {
            char *cmd = 0;
            int r = extract_cmd(&p, &cmd);
            if (r == 0) {
                val.p = 0; val.len = val.cap = 0;
                r = run_cmd(cmd, &val, flags);
                if (r == 0 && fld_expand_text(&f, out, val.p ? val.p : "", 1)) r = WRDE_NOSPACE;
                sb_free(&val);
            }
            free(cmd);
            if (r) return r;
            continue;
        }

        if (c == '~' && !f.started) {
            if (p[1] == '\0' || p[1] == '/') {        /* ~ or ~/… -> $HOME */
                const char *home = getenv("HOME");
                if (home && fld_write(&f, home, strlen(home))) return WRDE_NOSPACE;
                p++;                                  /* the '/' is copied next pass */
                continue;
            }
            /* ~user: there is no passwd database here, so leave it literal */
            if (fld_putc(&f, '~')) return WRDE_NOSPACE;
            p++;
            continue;
        }

        if (glob_meta(c)) { f.meta = 1; if (fld_putc(&f, (char)c)) return WRDE_NOSPACE; p++; continue; }

        if (fld_putc(&f, (char)c)) return WRDE_NOSPACE;
        p++;
    }

    if (fld_flush(&f, out)) return WRDE_NOSPACE;
    sb_free(&f.field);
    return 0;
}

/* ── entry points ───────────────────────────────────────────────────────── */

int wordexp(const char *words, wordexp_t *we, int flags) {
    if (!we) return WRDE_BADCHAR;

    wlist w;
    w.v = 0; w.n = 0; w.cap = 0;

    int r = expand(words ? words : "", &w, flags);
    if (r) { wl_free(&w); return r; }

    size_t oldc = 0, oldoffs = 0;
    char **oldv = 0;
    if ((flags & WRDE_APPEND) && we->we_wordv) {
        oldc = we->we_wordc; oldoffs = we->we_offs; oldv = we->we_wordv;
    } else if (flags & WRDE_REUSE) {
        wordfree(we);
    }

    size_t offs = (flags & WRDE_DOOFFS) ? we->we_offs : (oldv ? oldoffs : 0);
    if (offs > 4096) { wl_free(&w); free(oldv); return WRDE_NOSPACE; }

    char **vec = malloc((offs + oldc + w.n + 1) * sizeof(char *));
    if (!vec) { wl_free(&w); free(oldv); return WRDE_NOSPACE; }

    size_t k = 0;
    for (size_t i = 0; i < offs; i++) vec[k++] = 0;
    for (size_t i = 0; i < oldc; i++) vec[k++] = oldv[oldoffs + i];
    for (size_t i = 0; i < w.n; i++) vec[k++] = w.v[i];
    vec[k] = 0;

    free(oldv);
    free(w.v);                    /* the strings now live in vec */

    we->we_offs  = offs;
    we->we_wordc = oldc + w.n;
    we->we_wordv = vec;
    return 0;
}

void wordfree(wordexp_t *we) {
    if (!we || !we->we_wordv) { if (we) { we->we_wordc = 0; we->we_wordv = 0; } return; }
    for (size_t i = 0; i < we->we_wordc; i++) free(we->we_wordv[we->we_offs + i]);
    free(we->we_wordv);
    we->we_wordv = 0;
    we->we_wordc = 0;
}
