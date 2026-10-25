#ifndef _REGEX_H
#define _REGEX_H

#include <stddef.h>

/* POSIX 1003.2 regular expressions: BRE and ERE, leftmost-longest, with
 * subexpression capture. */

typedef int regoff_t;                   /* matches the 32-bit target's off_t range */

typedef struct {
    regoff_t rm_so;                     /* start offset, or -1 */
    regoff_t rm_eo;                     /* one past the end, or -1 */
} regmatch_t;

typedef struct {
    size_t re_nsub;                     /* number of parenthesised subexpressions */
    void  *re_priv;                     /* compiled program (internal) */
} regex_t;

/* cflags */
#define REG_EXTENDED 0x01               /* ERE instead of BRE */
#define REG_ICASE    0x02
#define REG_NEWLINE  0x04               /* '.' and negated classes stop at '\n';
                                           '^'/'$' also match at line boundaries */
#define REG_NOSUB    0x08               /* only the overall match is reported */

/* eflags */
#define REG_NOTBOL   0x01
#define REG_NOTEOL   0x02
#define REG_STARTEND 0x04               /* pmatch[0] carries the string range (GNU/BSD) */

/* error codes — regerror() maps these to strings */
#define REG_NOMATCH  1
#define REG_BADPAT   2
#define REG_ECOLLATE 3
#define REG_ECTYPE   4
#define REG_EESCAPE  5
#define REG_ESUBREG  6
#define REG_EBRACK   7
#define REG_EPAREN   8
#define REG_EBRACE   9
#define REG_BADBR   10
#define REG_ERANGE  11
#define REG_ESPACE  12
#define REG_BADRPT  13

int    regcomp(regex_t *preg, const char *regex, int cflags);
int    regexec(const regex_t *preg, const char *string, size_t nmatch,
               regmatch_t pmatch[], int eflags);
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size);
void   regfree(regex_t *preg);

#endif
