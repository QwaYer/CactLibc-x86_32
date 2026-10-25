#include "getopt.h"
#include "unistd.h"
#include "string.h"

char *optarg = 0;
int   optind = 1;
int   opterr = 1;
int   optopt = 0;

/* The current cluster of short options; NULL when a fresh argv element has to
 * be fetched.  One scanner serves getopt(), getopt_long() and
 * getopt_long_only(), so the three cannot drift apart. */
static char *nextchar = 0;

/* GNU-style permutation: non-options encountered while scanning are remembered
 * as a block [first_nonopt, last_nonopt) and swapped behind the options once
 * another option shows up, so `prog file -v` still sees -v (and the caller
 * finds the operands at optind afterwards). */
static int first_nonopt = 0;
static int last_nonopt = 0;

/* Swap the non-option block with the option block that follows it. */
static void exchange(char **argv) {
    int bottom = first_nonopt, middle = last_nonopt, top = optind;

    while (top > middle && middle > bottom) {
        if (top - middle > middle - bottom) {
            int len = middle - bottom;
            for (int i = 0; i < len; i++) {
                char *t = argv[bottom + i];
                argv[bottom + i] = argv[top - len + i];
                argv[top - len + i] = t;
            }
            top -= len;
        } else {
            int len = top - middle;
            for (int i = 0; i < len; i++) {
                char *t = argv[bottom + i];
                argv[bottom + i] = argv[middle + i];
                argv[middle + i] = t;
            }
            bottom += len;
        }
    }
    first_nonopt += optind - last_nonopt;
    last_nonopt = optind;
}

static void _opterr_msg_short(int c) {
    char b[80];
    int n = 0;
    const char *p = "invalid option -- '";
    while (*p) b[n++] = *p++;
    b[n++] = (char)c;
    b[n++] = '\'';
    b[n++] = '\n';
    write(2, b, n);
}

static void _opterr_msg_long(const char *body, const char *lead) {
    char b[192];
    int n = 0;
    for (const char *p = lead; *p && n < (int)sizeof(b) - 4; p++) b[n++] = *p;
    for (const char *p = body; *p && n < (int)sizeof(b) - 4; p++) b[n++] = *p;
    b[n++] = '\'';
    b[n++] = '\n';
    write(2, b, n);
}

#define LONG_NONE (-2)          /* not a long option: try the short path */
#define LONG_ERR  (-3)          /* already reported; the caller returns '?' */

/* Index of the matching long option: exact name wins, otherwise a unique
 * prefix; LONG_ERR when a prefix is ambiguous, -1 when nothing matches. */
static int long_match(const char *name, const struct option *lo, int *exact) {
    int found = -1, count = 0;
    size_t len = strlen(name);

    for (int i = 0; lo[i].name; i++) {
        if (strcmp(lo[i].name, name) == 0) { *exact = 1; return i; }
        if (strncmp(lo[i].name, name, len) == 0) { found = i; count++; }
    }
    *exact = 0;
    return (count == 1) ? found : (count > 1 ? LONG_ERR : -1);
}

static int do_long(const char *body, int double_dash, int argc, char *const argv[],
                   const char *optstring, const struct option *lo,
                   int *longindex, int long_only) {
    char name[128];
    const char *eq = strchr(body, '=');
    size_t nlen = eq ? (size_t)(eq - body) : strlen(body);
    if (nlen == 0 || nlen >= sizeof(name)) return LONG_NONE;
    memcpy(name, body, nlen);
    name[nlen] = '\0';

    int exact = 0;
    int idx = long_match(name, lo, &exact);
    if (idx == LONG_ERR) {                          /* ambiguous prefix */
        if (opterr) _opterr_msg_long(body, "ambiguous option '");
        optind++;
        return LONG_ERR;
    }
    if (idx < 0) {
        /* "-name" with long_only may still be a short-option cluster. */
        if (long_only && !double_dash) return LONG_NONE;
        if (opterr) _opterr_msg_long(body, "unrecognized option '");
        optopt = 0;
        optind++;
        return LONG_ERR;
    }

    const struct option *o = &lo[idx];
    const char *arg = 0;

    if (o->has_arg == no_argument) {
        if (eq) {
            if (opterr) _opterr_msg_long(body, "option doesn't allow an argument '");
            optind++;
            return LONG_ERR;
        }
    } else if (eq) {
        arg = eq + 1;
    } else if (o->has_arg == required_argument) {
        if (optind + 1 >= argc) {
            if (opterr && optstring[0] != ':')
                _opterr_msg_long(body, "option requires an argument '");
            optind++;
            return (optstring[0] == ':') ? ':' : LONG_ERR;
        }
        arg = argv[++optind];
    }

    optind++;
    optarg = (char *)arg;
    if (longindex) *longindex = idx;
    if (o->flag) { *o->flag = o->val; return 0; }
    return o->val;
}

static int getopt_common(int argc, char *const argv[], const char *optstring,
                         const struct option *longopts, int *longindex,
                         int long_only) {
    if (optind == 0) { optind = 1; nextchar = 0; first_nonopt = last_nonopt = 1; }

    for (;;) {
        if (nextchar == 0 || *nextchar == '\0') {
            /* Permute: options first, then the operands. */
            if (last_nonopt > optind) last_nonopt = optind;
            if (first_nonopt > optind) first_nonopt = optind;

            if (first_nonopt != last_nonopt && last_nonopt != optind) exchange((char **)argv);
            else if (last_nonopt != optind) first_nonopt = optind;

            while (optind < argc &&
                   (argv[optind][0] != '-' || argv[optind][1] == '\0'))
                optind++;
            last_nonopt = optind;

            /* "--" ends the options: step over it, exchange it with any
             * pending non-options as if it were an option, then treat
             * everything after it as operands. */
            if (optind != argc && strcmp(argv[optind], "--") == 0) {
                optind++;
                if (first_nonopt != last_nonopt && last_nonopt != optind)
                    exchange((char **)argv);
                else if (first_nonopt == last_nonopt)
                    first_nonopt = optind;
                last_nonopt = argc;
                optind = argc;
            }

            if (optind >= argc) {           /* nothing but operands left */
                optind = first_nonopt;
                return -1;
            }

            char *arg = argv[optind];
            if (longopts && (arg[1] == '-' || long_only)) {
                const char *body = arg + (arg[1] == '-' ? 2 : 1);
                int r = do_long(body, arg[1] == '-', argc, argv, optstring,
                                longopts, longindex, long_only);
                if (r == LONG_ERR) { nextchar = 0; return '?'; }
                if (r != LONG_NONE) { nextchar = 0; return r; }
                /* LONG_NONE: a single dash that is not a long option. */
            }

            nextchar = arg + 1;
            if (*nextchar == '\0') { optind++; nextchar = 0; continue; }
        }

        int c = (unsigned char)*nextchar++;
        const char *p = strchr(optstring, c);
        if (!p) {
            optopt = c;
            if (*nextchar == '\0') { nextchar = 0; optind++; }   /* keep the rest of the cluster */
            if (opterr && optstring[0] != ':') _opterr_msg_short(c);
            return '?';
        }
        if (p[1] == ':') {
            if (*nextchar) {
                optarg = nextchar;
                nextchar = 0;
            } else if (optind + 1 < argc) {
                optarg = argv[++optind];
                nextchar = 0;
            } else {
                optopt = c;
                nextchar = 0;
                optind++;
                if (opterr && optstring[0] != ':') _opterr_msg_short(c);
                return (optstring[0] == ':') ? ':' : '?';
            }
            optind++;
            return c;
        }
        optarg = 0;
        if (!*nextchar) { nextchar = 0; optind++; }
        return c;
    }
}

int getopt(int argc, char *const argv[], const char *optstring) {
    return getopt_common(argc, argv, optstring, 0, 0, 0);
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    return getopt_common(argc, argv, optstring, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex) {
    return getopt_common(argc, argv, optstring, longopts, longindex, 1);
}
