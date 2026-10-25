#include "fnmatch.h"
#include "ctype.h"
#include "string.h"

/* A straightforward backtracking matcher: correctness first, and patterns are
 * short.  `*` retries every split point, so the recursion is bounded by the
 * string length in practice. */

static int lc(int c, int flags) {
    return (flags & FNM_CASEFOLD) ? tolower((unsigned char)c) : (unsigned char)c;
}

static int ch_eq(int a, int b, int flags) {
    return lc(a, flags) == lc(b, flags);
}

static int class_match(const char *cls, size_t n, int c) {
    unsigned char uc = (unsigned char)c;
    return (n == 5 && !strncmp(cls, "alpha", 5)  &&  isalpha(uc)) ||
           (n == 5 && !strncmp(cls, "digit", 5)  &&  isdigit(uc)) ||
           (n == 5 && !strncmp(cls, "alnum", 5)  &&  isalnum(uc)) ||
           (n == 5 && !strncmp(cls, "space", 5)  &&  isspace(uc)) ||
           (n == 5 && !strncmp(cls, "upper", 5)  &&  isupper(uc)) ||
           (n == 5 && !strncmp(cls, "lower", 5)  &&  islower(uc)) ||
           (n == 5 && !strncmp(cls, "punct", 5)  &&  ispunct(uc)) ||
           (n == 5 && !strncmp(cls, "cntrl", 5)  &&  iscntrl(uc)) ||
           (n == 5 && !strncmp(cls, "print", 5)  &&  isprint(uc)) ||
           (n == 5 && !strncmp(cls, "graph", 5)  &&  isgraph(uc)) ||
           (n == 5 && !strncmp(cls, "blank", 5)  && (uc == ' ' || uc == '\t')) ||
           (n == 6 && !strncmp(cls, "xdigit", 6) &&  isxdigit(uc));
}

/* One bracket expression; *pp points just past '['.  On return *pp is past the
 * closing ']' (or at the end when the expression is unterminated). */
static int match_bracket(const char **pp, int c, int flags) {
    const char *p = *pp;
    int negate = 0, matched = 0;

    if (*p == '!' || *p == '^') { negate = 1; p++; }

    /* A ']' immediately after the '[' (or '[!') is a literal member. */
    int first = 1;
    for (;;) {
        if (*p == '\0') { *pp = p; return 0; }          /* unterminated: no match */
        if (*p == ']' && !first) break;
        first = 0;

        if (p[0] == '[' && p[1] == ':') {               /* [:class:] */
            const char *cls   = p + 2;
            const char *close = cls;
            while (*close && !(close[0] == ':' && close[1] == ']')) close++;
            if (*close == '\0') { *pp = p; return 0; }
            if (class_match(cls, (size_t)(close - cls), c)) matched = 1;
            p = close + 2;
            continue;
        }

        int lo = (unsigned char)*p++;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && *p) lo = (unsigned char)*p++;

        if (*p == '-' && p[1] != ']' && p[1] != '\0') {
            p++;
            int hi = (unsigned char)*p++;
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && *p) hi = (unsigned char)*p++;
            if (lc(lo, flags) <= lc(c, flags) && lc(c, flags) <= lc(hi, flags))
                matched = 1;
        } else if (ch_eq(lo, c, flags)) {
            matched = 1;
        }
    }

    *pp = p + 1;                                        /* past ']' */
    return negate ? !matched : matched;
}

static int fnmatch_inner(const char *p, const char *s, int flags) {
    /* FNM_PERIOD: a leading '.' of a component is not matched by a wildcard. */
    int period_ok = !((flags & FNM_PERIOD) && *s == '.');

    for (; *p; p++) {
        switch (*p) {
        case '?':
            if (*s == '\0' || !period_ok) return 0;
            if ((flags & FNM_PATHNAME) && *s == '/') return 0;
            s++;
            period_ok = 1;
            continue;

        case '*': {
            while (p[1] == '*') p++;                    /* collapse runs */
            if (!period_ok) return 0;
            for (;;) {
                if (fnmatch_inner(p + 1, s, flags)) return 1;
                if (*s == '\0') return 0;
                if ((flags & FNM_PATHNAME) && *s == '/') return 0;
                s++;
            }
        }

        case '[': {
            if (*s == '\0' || !period_ok) return 0;
            if ((flags & FNM_PATHNAME) && *s == '/') return 0;
            const char *end = p + 1;
            int hit = match_bracket(&end, (unsigned char)*s, flags);
            if (!hit) return 0;
            p = end - 1;
            s++;
            period_ok = 1;
            continue;
        }

        case '\\':
            if (!(flags & FNM_NOESCAPE) && p[1]) p++;
            /* fall through */
        default: {
            int pc = (unsigned char)*p;
            if (!ch_eq(pc, (unsigned char)*s, flags)) return 0;
            s++;
            period_ok = !((flags & FNM_PERIOD) && pc == '/' && *s == '.');
            continue;
        }
        }
    }

    if (*s == '\0') return 1;
    if ((flags & FNM_LEADING_DIR) && *s == '/') return 1;
    return 0;
}

int fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) return FNM_NOMATCH;
    return fnmatch_inner(pattern, string, flags) ? 0 : FNM_NOMATCH;
}
