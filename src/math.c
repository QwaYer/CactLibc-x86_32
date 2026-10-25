/*
 * CactLib math: every function declared in <math.h>, freestanding.
 *
 * The transcendental functions are the i686 x87 instructions (fsqrt, fsin,
 * fcos, fptan, fpatan, fyl2x, f2xm1/fscale), so the results carry the FPU's
 * 64-bit mantissa and no external libm is needed.  The functions that are
 * really integer structure (fabs, frexp, modf, fmod) are plain bit/counting
 * code.
 *
 * Trig accuracy: fsin/fcos/fptan are only good for arguments of modest
 * magnitude, so the entry points reduce modulo 2*pi first.  The reduction is
 * done in 80-bit precision with fprem against an extended 2*pi, which keeps
 * the error near the argument's own ulp rather than the ~1e-11 a double
 * reduction would give for arguments in the thousands.
 */

#include "math.h"
#include "errno.h"
#include <stdint.h>

#define CACT_NAN    (__builtin_nan(""))
#define CACT_INF    (__builtin_inf())
#define CACT_PI_2   1.5707963267948966192313216916398
#define CACT_2PI_L  6.283185307179586476925286766559005768394338798750211641949889L

/* 2^y for y already in ST(0): split off the nearest integer, evaluate the
 * fraction with f2xm1 and let fscale apply the exponent.  Shared verbatim by
 * exp() (y = x*log2(e) from the FPU) and pow() (y = y*log2(x) from fyl2x). */
#define CACT_EXP2_TAIL \
        "fld %%st(0)\n\t"            \
        "frndint\n\t"                \
        "fxch\n\t"                   \
        "fsub %%st(1), %%st(0)\n\t"  \
        "f2xm1\n\t"                  \
        "fld1\n\t"                   \
        "faddp\n\t"                  \
        "fscale\n\t"                 \
        "fstp %%st(1)\n\t"           \
        "fstpl %0"

/* ── helpers ────────────────────────────────────────────────────────────── */

/* ln(x), x > 0. */
static double _ln(double x) {
    double r;
    __asm__ volatile ("fldln2\n\tfldl %1\n\tfyl2x\n\tfstpl %0"
                      : "=m"(r) : "m"(x) : "st");
    return r;
}

/* log10(x), x > 0. */
static double _log10(double x) {
    double r;
    __asm__ volatile ("fldlg2\n\tfldl %1\n\tfyl2x\n\tfstpl %0"
                      : "=m"(r) : "m"(x) : "st");
    return r;
}

/* Truncate toward zero.  Only used where an integer check is needed (modf,
 * pow), so the (long long) cast's range limit is not a problem. */
static double _trunc(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;  /* |x| >= 2^52 */
    return (x < 0.0) ? -floor(-x) : floor(x);
}

/* x modulo 2*pi in extended precision, for the trig entry points. */
static long double _reduce2pi(double x) {
    long double xl = (long double)x;
    long double y  = CACT_2PI_L;
    long double r;
    /* fprem is not always completed in one go; the C2 flag asks for another
     * pass over the partial remainder. */
    __asm__ volatile (
        "fldt %2\n\t"
        "fldt %1\n\t"
        "1:\n\t"
        "fprem\n\t"
        "fnstsw %%ax\n\t"
        "testb $4, %%ah\n\t"
        "jnz 1b\n\t"
        "fstp %%st(1)\n\t"
        "fstpt %0"
        : "=m"(r) : "m"(xl), "m"(y) : "ax", "st");
    return r;
}

/* ── integral structure (no FPU state) ──────────────────────────────────── */

double fabs(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u &= 0x7fffffffffffffffULL;
    return v.d;
}

double trunc(double x) {
    return _trunc(x);
}

double floor(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;  /* already integral */
    double lo = (double)(long long)x;
    if (x == lo) return x;
    return (x < 0.0) ? lo - 1.0 : lo;
}

double ceil(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    double lo = (double)(long long)x;
    if (x == lo) return x;
    return (x > 0.0) ? lo + 1.0 : lo;
}

double round(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    double f = floor(fabs(x) + 0.5);        /* half away from zero, as in C */
    return (x < 0.0) ? -f : f;
}

/* x * 2^n in one exact step.  fscale multiplies ST(0) by 2^trunc(ST(1)) with
 * the full extended-range exponent, so there is no repeated doubling and no
 * intermediate overflow of a result that is itself in range. */
static double _scale2(double x, int n) {
    double r;
    __asm__ volatile ("fildl %2\n\tfldl %1\n\tfscale\n\tfstp %%st(1)\n\tfstpl %0"
                      : "=m"(r) : "m"(x), "m"(n) : "st");
    return r;
}

double ldexp(double x, int exp) {
    double r;

    if (x != x) return x;                                   /* NaN */
    if (x > 1.7976931348623157e308 || x < -1.7976931348623157e308) return x;
    if (x == 0.0 || exp == 0) return x;
    /* A double spans 2^-1074 .. 2^1024, so any exponent beyond these bounds
     * leaves the range whatever x is: no need to walk a huge exponent. */
    if      (exp >  2200) r = (x < 0.0) ? -CACT_INF : CACT_INF;
    else if (exp < -2200) r = (x < 0.0) ? -0.0 : 0.0;
    else                  r = _scale2(x, exp);

    /* ERANGE exactly where glibc reports it: overflow to an infinity, or
     * underflow all the way to zero.  A denormal result is not an error. */
    if (r > 1.7976931348623157e308 || r < -1.7976931348623157e308) errno = ERANGE;
    else if (r == 0.0) errno = ERANGE;
    return r;
}

double frexp(double x, int *exp) {
    union { double d; uint64_t u; } v;
    uint64_t bits;
    int e2;

    v.d = x;
    bits = v.u;
    e2 = (int)((bits >> 52) & 0x7ff);

    if (e2 == 0x7ff) { *exp = 0; return x; }          /* inf / NaN pass through */
    if (e2 == 0) {
        if ((bits << 1) == 0) { *exp = 0; return x; } /* +/-0 */
        /* Subnormal: scale into the normal range, then split that. */
        double s = x * 9007199254740992.0;            /* 2^53 */
        int e;
        double m = frexp(s, &e);
        *exp = e - 53;
        return m;
    }

    /* Normal: keep the fraction, force the exponent to -1 (value in [0.5,1)). */
    v.u = (bits & ~(0x7ffULL << 52)) | (0x3feULL << 52);
    *exp = e2 - 1022;
    return v.d;
}

double modf(double x, double *iptr) {
    double ip = _trunc(x);
    *iptr = ip;
    if (ip == x) return (x < 0.0) ? -0.0 : 0.0;
    return x - ip;
}

double fmod(double x, double y) {
    double ax, ay, r;
    int ex, ey, e;

    if (y == 0.0 || x != x || y != y) return CACT_NAN;
    ax = fabs(x);
    ay = fabs(y);
    if (ax < ay) return x;
    if (ax == ay) return (x < 0.0) ? -0.0 : 0.0;

    /* Binary long division on the exponents: ax = m*2^ex, ay = n*2^ey, ex > ey.
     * Subtracting ldexp(ay, e) for e from ex-ey down to 0 is exact (each
     * remainder stays a multiple of the smaller ulp). */
    frexp(ax, &ex);
    frexp(ay, &ey);
    r = ax;
    for (e = ex - ey; e >= 0; e--) {
        double t = ldexp(ay, e);
        if (r >= t) r -= t;
    }
    return (x < 0.0) ? -r : r;
}

/* ── roots and transcendentals ──────────────────────────────────────────── */

double sqrt(double x) {
    double r;
    if (x != x) return CACT_NAN;
    if (x < 0.0) return CACT_NAN;
    __asm__ volatile ("fldl %1\n\tfsqrt\n\tfstpl %0" : "=m"(r) : "m"(x) : "st");
    return r;
}

/* ── Payne–Hanek argument reduction ───────────────────────────────────────
 * `fprem` against an 80-bit 2*pi is limited by that modulus' own 64-bit
 * precision: the error grows as |x| * 2^-64 and swallows the result past
 * ~1e10.  A large argument is therefore reduced modulo pi/2 *exactly*, using
 * 1280 bits of 2/pi.  Writing x = m * 2^e with m a 53-bit integer,
 *
 *      x * (2/pi) = (m * Q) * 2^(e - 32*CACT_PIO2_WORDS),  Q = (2/pi)*2^(32N)
 *
 * so the integer part of that, modulo 4, is the quadrant and the bits below the
 * binary point give the remainder to 64 bits — the error no longer depends on
 * |x| at all.  (Q's first word matching fdlibm's ipio2[0] is the cross-check
 * that the table is really 2/pi.) */

#define CACT_PIO2_WORDS 40

static const uint32_t cact_two_over_pi[CACT_PIO2_WORDS] = {
    0xa2f9836eu, 0x4e441529u, 0xfc2757d1u, 0xf534ddc0u, 0xdb629599u, 0x3c439041u,
    0xfe5163abu, 0xdebbc561u, 0xb7246e3au, 0x424dd2e0u, 0x06492eeau, 0x09d1921cu,
    0xfe1deb1cu, 0xb129a73eu, 0xe88235f5u, 0x2ebb4484u, 0xe99c7026u, 0xb45f7e41u,
    0x3991d639u, 0x835339f4u, 0x9c845f8bu, 0xbdf9283bu, 0x1ff897ffu, 0xde05980fu,
    0xef2f118bu, 0x5a0a6d1fu, 0x6d367ecfu, 0x27cb09b7u, 0x4f463f66u, 0x9e5fea2du,
    0x7527bac7u, 0xebe5f17bu, 0x3d0739f7u, 0x8a5292eau, 0x6bfb5fb1u, 0x1f8d5d08u,
    0x56033046u, 0xfc7b6babu, 0xf0cfbc20u, 0x9af4361du,
};

/* `count` bits of a little-endian word array starting at bit `pos` (bit 0 is
 * the least significant); anything outside the array reads as zero. */
static uint64_t _bits_le(const uint32_t *w, int nwords, int pos, int count)
{
    uint64_t v = 0;
    int i;
    for (i = count - 1; i >= 0; i--) {
        int b = pos + i;
        v <<= 1;
        if (b >= 0 && b < nwords * 32)
            v |= (uint64_t)((w[b >> 5] >> (b & 31)) & 1u);
    }
    return v;
}

/* r = x - q*(pi/2) with |r| <= pi/4, and *quad = q mod 4, for any finite x. */
static long double _reduce_pio2(double x, int *quad)
{
    union { double d; uint64_t u; } v;
    int e, i, s, round_up, neg = 0;
    uint64_t m, f, carry;
    uint32_t m0, m1;
    uint32_t R[CACT_PIO2_WORDS + 3];
    long double fd;

    if (x < 0.0 || (x == 0.0 && 1.0 / x < 0)) { neg = 1; x = -x; }

    v.d = x;
    e = (int)((v.u >> 52) & 0x7ff);
    m = v.u & 0xfffffffffffffULL;
    if (e == 0) { e = -1074; }                      /* subnormal */
    else { m |= 1ULL << 52; e -= 1075; }            /* x = m * 2^e */

    /* R = m * Q, schoolbook in 32-bit words (m is two words). */
    for (i = 0; i <= CACT_PIO2_WORDS + 2; i++) R[i] = 0;
    m0 = (uint32_t)m;
    m1 = (uint32_t)(m >> 32);
    carry = 0;
    for (i = 0; i < CACT_PIO2_WORDS; i++) {
        uint64_t t = (uint64_t)m0 * cact_two_over_pi[CACT_PIO2_WORDS - 1 - i] + carry;
        R[i] = (uint32_t)t;
        carry = t >> 32;
    }
    R[CACT_PIO2_WORDS] = (uint32_t)carry;
    carry = 0;
    for (i = 1; i <= CACT_PIO2_WORDS; i++) {
        uint64_t t = (uint64_t)m1 * cact_two_over_pi[CACT_PIO2_WORDS - i]
                   + R[i] + carry;
        R[i] = (uint32_t)t;
        carry = t >> 32;
    }
    R[CACT_PIO2_WORDS + 1] = (uint32_t)carry;

    /* x*(2/pi) = R * 2^(e - 32N): the binary point sits just below bit s. */
    s = 32 * CACT_PIO2_WORDS - e;

    round_up = (s > 0) ? (int)_bits_le(R, CACT_PIO2_WORDS + 3, s - 1, 1) : 0;
    *quad = (int)((_bits_le(R, CACT_PIO2_WORDS + 3, s, 2) + (uint64_t)round_up) & 3);
    if (neg && *quad) *quad = (4 - *quad) & 3;

    f = _bits_le(R, CACT_PIO2_WORDS + 3, s - 64, 64);
    fd = (long double)f * (1.0L / 18446744073709551616.0L);      /* / 2^64 */
    if (round_up) fd -= 1.0L;
    if (neg) fd = -fd;
    return fd * (CACT_2PI_L / 4.0L);                             /* * pi/2 */
}

double sin(double x) {
    if (x != x) return CACT_NAN;
    if (fabs(x) >= 1.0e6) {                 /* large: reduce modulo pi/2 */
        int q;
        long double r = _reduce_pio2(x, &q);
        double s, c;
        __asm__ volatile ("fldt %2\n\tfsin\n\tfstpl %0\n\tfldt %2\n\tfcos\n\tfstpl %1"
                          : "=m"(s), "=m"(c) : "m"(r) : "st");
        switch (q) {
        case 1: return c;
        case 2: return -s;
        case 3: return -c;
        default: return s;
        }
    }
    {
        long double t = _reduce2pi(x);
        double r;
        __asm__ volatile ("fldt %1\n\tfsin\n\tfstpl %0" : "=m"(r) : "m"(t) : "st");
        return r;
    }
}

double cos(double x) {
    if (x != x) return CACT_NAN;
    if (fabs(x) >= 1.0e6) {
        int q;
        long double r = _reduce_pio2(x, &q);
        double s, c;
        __asm__ volatile ("fldt %2\n\tfsin\n\tfstpl %0\n\tfldt %2\n\tfcos\n\tfstpl %1"
                          : "=m"(s), "=m"(c) : "m"(r) : "st");
        switch (q) {
        case 1: return -s;
        case 2: return -c;
        case 3: return s;
        default: return c;
        }
    }
    {
        long double t = _reduce2pi(x);
        double r;
        __asm__ volatile ("fldt %1\n\tfcos\n\tfstpl %0" : "=m"(r) : "m"(t) : "st");
        return r;
    }
}

/* tan of an 80-bit argument on the FPU.  fptan leaves the tangent in ST(1) and
 * pushes 1.0 into ST(0). */
static double _tan_ld(long double t) {
    double r;
    __asm__ volatile ("fldt %1\n\tfptan\n\tfstp %%st(0)\n\tfstpl %0"
                      : "=m"(r) : "m"(t) : "st");
    return r;
}

double tan(double x) {
    if (x != x) return CACT_NAN;
    if (fabs(x) >= 1.0e6) {                 /* large: reduce modulo pi/2 */
        int q;
        long double r = _reduce_pio2(x, &q);
        double t = _tan_ld(r);
        /* tan(r + q*pi/2) is tan(r) for even q and -cot(r) for odd q. */
        return (q & 1) ? -1.0 / t : t;
    }
    return _tan_ld(_reduce2pi(x));
}

double atan2(double y, double x) {
    double r;
    __asm__ volatile ("fldl %1\n\tfldl %2\n\tfpatan\n\tfstpl %0"
                      : "=m"(r) : "m"(y), "m"(x) : "st");
    return r;
}

double atan(double x) {
    return atan2(x, 1.0);
}

double asin(double x) {
    if (x != x) return CACT_NAN;
    if (x > 1.0 || x < -1.0) return CACT_NAN;
    if (x == 1.0) return CACT_PI_2;
    if (x == -1.0) return -CACT_PI_2;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x != x) return CACT_NAN;
    if (x > 1.0 || x < -1.0) return CACT_NAN;
    return atan2(sqrt(1.0 - x * x), x);
}

double exp(double x) {
    double r;
    if (x != x) return CACT_NAN;
    /* y = x*log2(e) on the FPU stack, then 2^y. */
    __asm__ volatile ("fldl2e\n\tfmull %1\n\t" CACT_EXP2_TAIL
                      : "=m"(r) : "m"(x) : "st");
    return r;
}

double log(double x) {
    if (x != x || x < 0.0) return CACT_NAN;
    if (x == 0.0) return -CACT_INF;
    return _ln(x);
}

double log10(double x) {
    if (x != x || x < 0.0) return CACT_NAN;
    if (x == 0.0) return -CACT_INF;
    return _log10(x);
}

double pow(double x, double y) {
    int neg = 0;
    double r;

    if (y == 0.0) return 1.0;                       /* including pow(0,0) */
    if (x != x || y != y) return CACT_NAN;
    if (x == 1.0) return 1.0;
    if (x < 0.0) {
        /* Negative base: only an integer exponent is real; the sign follows
         * the exponent's parity. */
        double yi = _trunc(y);
        if (yi != y) return CACT_NAN;
        /* Every double of magnitude 2^52 or more is an even integer, so its
         * parity is known even though it cannot be cast to long long. */
        if (fabs(y) >= 9007199254740992.0) neg = 0;
        else neg = ((long long)yi & 1) ? 1 : 0;
        x = -x;
    }
    if (x == 0.0) return (y < 0.0) ? CACT_INF : 0.0;

    /* x^y = 2^(y*log2(x)): fyl2x forms the exponent on the FPU stack. */
    __asm__ volatile ("fldl %1\n\tfldl %2\n\tfyl2x\n\t" CACT_EXP2_TAIL
                      : "=m"(r) : "m"(y), "m"(x) : "st");
    return neg ? -r : r;
}
