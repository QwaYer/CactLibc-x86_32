/* printf family host test.
 *
 * Every case is compared with glibc's snprintf, both the formatted bytes and
 * the return value.  The units under test are the real src/stdio.c (renamed,
 * see rename_stdio.h) so this exercises the shipped formatter, not a copy. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>

int  cact_snprintf(char *s, size_t n, const char *fmt, ...);
int  cact_sprintf(char *s, const char *fmt, ...);
int  cact_asprintf(char **p, const char *fmt, ...);
int *cact_host_errno_ptr(void);

static int checks, fails;

static void cmp(const char *got, int rg, const char *want, int rw, const char *fmt) {
    checks++;
    if (rg != rw || strcmp(got, want) != 0) {
        fails++;
        printf("FAIL  %-26s got(%d)=\"%s\"  want(%d)=\"%s\"\n", fmt, rg, got, rw, want);
    }
}

#define CK(fmt, ...) do { \
    char a[512], b[512]; \
    int ra = cact_snprintf(a, sizeof a, fmt, ##__VA_ARGS__); \
    int rb = snprintf(b, sizeof b, fmt, ##__VA_ARGS__); \
    cmp(a, ra, b, rb, fmt); \
} while (0)

#define CKN(n, fmt, ...) do { \
    char a[64], b[64]; \
    int ra = cact_snprintf(a, (n), fmt, ##__VA_ARGS__); \
    int rb = snprintf(b, (n), fmt, ##__VA_ARGS__); \
    cmp(a, ra, b, rb, fmt); \
} while (0)

int main(void) {
    /* integers, signs, flags */
    CK("%d", 0);
    CK("%d", 42);
    CK("%d", -42);
    CK("%d", -2147483647 - 1);
    CK("%i", 7);
    CK("%+d", 5);
    CK("%+d", -5);
    CK("%+d", 0);
    CK("% d", 5);
    CK("% d", -5);
    CK("% d", 0);
    CK("%+x", 255);
    CK("% x", 255);
    CK("%+u", 9u);
    CK("%u", 4294967295u);
    CK("%ld", 123456789L);
    CK("%ld", -1L);
    CK("%lu", 4000000000UL);
    CK("%lld", -9223372036854775807LL - 1);
    CK("%llu", 18446744073709551615ULL);
    CK("%llx", 0x123456789abcdef0ULL);
    CK("%hd", 70000);
    CK("%hhd", 300);
    CK("%hhu", 300);
    CK("%hu", 70000);
    CK("%zd", (ssize_t)-5);
    CK("%zu", (size_t)123456);
    CK("%jd", (intmax_t)-7);
    CK("%td", (ptrdiff_t)-9);

    /* width / precision / padding */
    CK("%8d|", 42);
    CK("%-8d|", 42);
    CK("%08d", 42);
    CK("%08d", -42);
    CK("%+08d", 42);
    CK("% 08d", 42);
    CK("%5.3d", 7);
    CK("%.0d", 0);
    CK("%.0d", 5);
    CK("%.5d", 12345);
    CK("%*d", 6, 42);
    CK("%-*d|", 6, 42);
    CK("%*d", -6, 42);
    CK("%.*f", 3, 3.14159);
    CK("%*.*f", 12, 2, 3.14159);

    /* bases */
    CK("%x", 0xdeadbeefu);
    CK("%X", 0xabc);
    CK("%#x", 0xdeadbeefu);
    CK("%#X", 0xabcu);
    CK("%#x", 0u);
    CK("%#o", 8u);
    CK("%#o", 0u);
    CK("%o", 511u);
    CK("%#010x", 255u);
    CK("%p", (void *)0x1234);
    CK("%p", (void *)0);
    CK("%10p|", (void *)0);
    CK("%-12p|", (void *)0xabc);

    /* floats */
    CK("%f", 3.14159265);
    CK("%.10f", 1.0 / 3.0);
    CK("%.0f", 2.5);
    CK("%.0f", 3.5);
    CK("%f", -0.0);
    CK("%+f", 0.0);
    CK("% f", 1.5);
    CK("%.2f", -1.005);
    CK("%e", 12345.678);
    CK("%E", 12345.678);
    CK("%.3e", -0.000123456);
    CK("%g", 0.0001234);
    CK("%g", 123456789.0);
    CK("%G", 1e-9);
    CK("%.5g", 1234.56789);
    CK("%#.0f", 5.0);
    CK("%#.0e", 5.0);
    CK("%#.0g", 5.0);
    CK("%#f", __builtin_inf());
    CK("%.17g", 0.1);
    CK("%12.4f|", 3.14159);
    CK("%-12.4f|", 3.14159);
    CK("%f", __builtin_inf());
    CK("%F", __builtin_inf());
    CK("%f", -__builtin_inf());
    CK("%f", __builtin_nan(""));
    CK("%E", __builtin_nan(""));

    /* strings, chars, percent */
    CK("%s", "hello");
    CK("%.3s", "hello");
    CK("%10s|", "hi");
    CK("%-10s|", "hi");
    CK("%.2s", "hi");
    CK("%s", (char *)0);
    CK("%s|%d|%s", "a", 1, "b");
    CK("%c", 'A');
    CK("%5c|", 'A');
    CK("%-5c|", 'A');
    CK("%%");
    CK("100%%%d", 5);
    CK("no conversions");

    /* positional arguments */
    CK("%2$d %1$d", 10, 20);
    CK("%1$s-%2$s", "a", "b");
    CK("%3$s %1$d %2$s", 7, "x", "y");
    CK("%2$*1$d", 5, 42);
    CK("%2$-8d|", 3, 42);
    CK("%1$d %1$d", 9);

    /* truncation and size-0 counting */
    CKN(4, "%s", "abcdefg");
    CKN(1, "%d", 12345);
    CKN(0, "%s", "abc");
    CKN(5, "%.3f", 3.14159);

    /* %n */
    {
        int n1 = -1, n2 = -1;
        char a[64], b[64];
        int ra = cact_snprintf(a, sizeof a, "ab%ncd", &n1);
        int rb = snprintf(b, sizeof b, "ab%ncd", &n2);
        cmp(a, ra, b, rb, "%n");
        checks++;
        if (n1 != n2) { fails++; printf("FAIL  %%n value: %d vs %d\n", n1, n2); }
    }
    {
        long n1 = -1, n2 = -1;
        char a[64], b[64];
        int ra = cact_snprintf(a, sizeof a, "hello%ln", &n1);
        int rb = snprintf(b, sizeof b, "hello%ln", &n2);
        cmp(a, ra, b, rb, "%ln");
        checks++;
        if (n1 != n2) { fails++; printf("FAIL  %%ln value: %ld vs %ld\n", n1, n2); }
    }

    /* %m (GNU) */
    *cact_host_errno_ptr() = ENOENT; errno = ENOENT; CK("%m");
    *cact_host_errno_ptr() = EACCES; errno = EACCES; CK("[%m]");
    *cact_host_errno_ptr() = ENOENT; errno = ENOENT; CK("%20m|");
    *cact_host_errno_ptr() = ENOENT; errno = ENOENT; CK("%.6m");

    /* ' grouping is inert in the C locale, exactly as in glibc */
    CK("%'d", 1234567);
    CK("%'d", -1234567);

    /* asprintf / sprintf */
    {
        char *p = 0;
        int r = cact_asprintf(&p, "%s=%d", "x", 7);
        checks++;
        if (r != 3 || !p || strcmp(p, "x=7")) {
            fails++;
            printf("FAIL  cact_asprintf -> %d \"%s\"\n", r, p ? p : "(null)");
        }
        free(p);
        char s[32], t[32];
        int r2 = cact_sprintf(s, "%05.1f", 3.25);
        int r3 = snprintf(t, sizeof t, "%05.1f", 3.25);
        checks++;
        if (r2 != r3 || strcmp(s, t)) {
            fails++;
            printf("FAIL  cact_sprintf -> %d \"%s\"  want %d \"%s\"\n", r2, s, r3, t);
        }
    }

    printf("printf: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
