#include "unistd.h"
#include "string.h"

char *optarg = 0;
int   optind = 1;
int   opterr = 1;
int   optopt = 0;

static void _opterr_msg(int c) {
    char b[64];
    int n = 0;
    const char *p = "invalid option -- '";
    while (*p) b[n++] = *p++;
    b[n++] = (char)c;
    b[n++] = '\'';
    b[n++] = '\n';
    write(2, b, n);
}

int getopt(int argc, char *const argv[], const char *optstring) {
    static char *nextchar = 0;
    if (optind == 0) { optind = 1; nextchar = 0; }

    for (;;) {
        if (nextchar == 0 || *nextchar == '\0') {
            if (optind >= argc) { nextchar = 0; return -1; }
            nextchar = argv[optind];
            if (nextchar[0] != '-' || nextchar[1] == '\0') {
                nextchar = 0;
                return -1;
            }
            if (nextchar[1] == '-' && nextchar[2] == '\0') {
                optind++;
                nextchar = 0;
                return -1;
            }
            nextchar += 1;
            if (*nextchar == '\0') { optind++; nextchar = 0; continue; }
        }

        int c = (unsigned char)*nextchar++;
        const char *p = strchr(optstring, c);
        if (!p) {
            optopt = c;
            nextchar = 0;
            optind++;
            if (opterr && optstring[0] != ':') _opterr_msg(c);
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
                if (opterr && optstring[0] != ':') _opterr_msg(c);
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
