#ifndef _GETOPT_H
#define _GETOPT_H

/* GNU long-option interface.  The short-option getopt() itself is declared in
 * <unistd.h> along with optarg/optind/opterr/optopt. */
#include "unistd.h"

struct option {
    const char *name;       /* long name, without the "--" */
    int         has_arg;    /* no_argument | required_argument | optional_argument */
    int        *flag;       /* NULL: getopt_long returns val; else *flag = val and returns 0 */
    int         val;        /* value to return / store */
};

#define no_argument        0
#define required_argument  1
#define optional_argument  2

/* Long options are matched exactly, or by an unambiguous prefix.
 *
 * Deliberate difference from GNU getopt: argv is NOT permuted, so scanning
 * stops at the first non-option argument (POSIX behaviour, and what getopt()
 * in this libc has always done).  GNU's "ls dir -l" style — options after a
 * positional — is therefore not reordered; put the options first. */
int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex);

/* As getopt_long, but "-name" is also tried as a long option before being
 * treated as a short-option cluster. */
int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex);

#endif
