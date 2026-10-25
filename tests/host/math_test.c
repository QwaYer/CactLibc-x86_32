/* math host test: src/math.c (renamed) compared with the host libm.
 *
 * Integral-structure functions are checked bit-for-bit (including the sign of
 * zero and the exact subnormal/overflow behaviour of ldexp); the x87
 * transcendentals are checked to a relative tolerance, since their last bits
 * come from the FPU's own sine/log and differ from libm's correctly-rounded
 * results. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>

double cact_sin(double);   double cact_cos(double);   double cact_tan(double);
double cact_sqrt(double);  double cact_floor(double); double cact_ceil(double);
double cact_round(double); double cact_trunc(double); double cact_fabs(double);
double cact_log(double);   double cact_log10(double); double cact_exp(double);
double cact_pow(double, double);
double cact_atan2(double, double);
double cact_atan(double);  double cact_asin(double);  double cact_acos(double);
double cact_fmod(double, double);
double cact_modf(double, double *);
double cact_frexp(double, int *);
double cact_ldexp(double, int);
int   *cact_host_errno_ptr(void);

static int checks, fails;

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

/* Exact: the bit patterns must match, which also pins -0.0 vs +0.0. */
static void ckx(const char *l, double g, double w) {
    checks++;
    if (isnan(w)) { if (!isnan(g)) { fails++; printf("FAIL %-18s got %.17g want NaN\n", l, g); } return; }
    if (bits(g) != bits(w)) {
        fails++;
        printf("FAIL %-18s got %.17g (%016llx) want %.17g (%016llx)\n",
               l, g, (unsigned long long)bits(g), w, (unsigned long long)bits(w));
    }
}

/* Relative tolerance (with an absolute floor for values near zero). */
static void ckr(const char *l, double g, double w, double tol) {
    checks++;
    if (isnan(w)) { if (!isnan(g)) { fails++; printf("FAIL %-18s got %.17g want NaN\n", l, g); } return; }
    if (isinf(w)) { if (g != w) { fails++; printf("FAIL %-18s got %.17g want %g\n", l, g, w); } return; }
    double d = fabs(g - w);
    if (d > tol * fabs(w) + 1e-300) {
        fails++;
        printf("FAIL %-18s got %.17g want %.17g (rel %.3g)\n", l, g, w, d / fabs(w));
    }
}

/* Absolute tolerance: used for the trig functions, whose error comes from the
   80-bit 2*pi modulus in the reduction (proportional to |x|), not from the
   value. */
static void ckabs(const char *l, double g, double w, double tol) {
    checks++;
    if (isnan(w)) { if (!isnan(g)) { fails++; printf("FAIL %-18s got %.17g want NaN\n", l, g); } return; }
    if (isinf(w)) { if (g != w) { fails++; printf("FAIL %-18s got %.17g want %g\n", l, g, w); } return; }
    if (fabs(g - w) > tol) {
        fails++;
        printf("FAIL %-18s got %.17g want %.17g (|err| %.3g > %.3g)\n", l, g, w, fabs(g - w), tol);
    }
}

static void ckint(const char *l, int g, int w) {
    checks++;
    if (g != w) { fails++; printf("FAIL %-18s got %d want %d\n", l, g, w); }
}

int main(void) {
    /* volatile inputs, so the compiler cannot fold the reference calls */
    volatile double V[] = {
        0.0, 1.0, -1.0, 2.0, -2.0, 0.5, 3.5, -3.5, 2.5, -2.5,
        0.1, 12345.678, -98765.4321, 1e-300, 1e300, 1000.0, -1000.0,
        0.0001234, 123456789.0, 9.9999999, 0.999999999
    };
    const int NV = (int)(sizeof V / sizeof V[0]);

    for (int i = 0; i < NV; i++) {
        double x = V[i];
        ckx("fabs",  cact_fabs(x),  fabs(x));
        ckx("floor", cact_floor(x), floor(x));
        ckx("ceil",  cact_ceil(x),  ceil(x));
        ckx("round", cact_round(x), round(x));
        ckx("trunc", cact_trunc(x), trunc(x));
        ckr("sqrt",  cact_sqrt(x),  sqrt(x), 1e-15);
        /* The trig reduction multiplies the 2*pi modulus' ulp by the quotient,
           so the absolute error grows with |x| (~|x|*5.4e-20); test that band
           up to 1e10 and only require a sane value beyond it. */
        if (fabs(x) <= 1e10) {
            double tol = 1e-15 + fabs(x) * 1e-18;
            ckabs("sin", cact_sin(x), sin(x), tol);
            ckabs("cos", cact_cos(x), cos(x), tol);
            ckabs("tan", cact_tan(x), tan(x), tol * (1.0 + tan(x) * tan(x)));
        } else {
            double s = cact_sin(x), c = cact_cos(x), t = cact_tan(x);
            checks++;
            if (!(isfinite(s) && fabs(s) <= 1.0 &&
                  isfinite(c) && fabs(c) <= 1.0 && isfinite(t))) {
                fails++;
                printf("FAIL trig-range x=%.6g -> %.6g %.6g %.6g\n", x, s, c, t);
            }
        }
        ckr("atan",  cact_atan(x),  atan(x), 1e-14);
        ckr("atan2", cact_atan2(x, 1.5), atan2(x, 1.5), 1e-14);
        ckr("exp",   cact_exp(x),   exp(x),  1e-13);
        if (x > 0.0) {
            ckr("log",   cact_log(x),   log(x),   1e-14);
            ckr("log10", cact_log10(x), log10(x), 1e-14);
            ckr("pow",   cact_pow(x, 2.5), pow(x, 2.5), 1e-12);
        }
        {
            double gi, wi;
            ckx("modf-f",  cact_modf(x, &gi), modf(x, &wi));
            ckx("modf-ip", gi, wi);
        }
        {
            int ge, we; cact_frexp(x, &ge); frexp(x, &we);
            ckx("frexp", cact_frexp(x, &ge), frexp(x, &we));
            ckint("frexp-exp", ge, we);
        }
        if (x != 0.0) {
            ckx("fmod", cact_fmod(x, 0.7), fmod(x, 0.7));
            ckx("fmod2", cact_fmod(x, 3.0), fmod(x, 3.0));
        }
    }

    /* asin/acos domain */
    for (double x = -1.0; x <= 1.0; x += 0.25) {
        ckr("asin", cact_asin(x), asin(x), 1e-14);
        ckr("acos", cact_acos(x), acos(x), 1e-14);
    }
    ckx("asin(2)", cact_asin(2.0), NAN);
    ckx("acos(2)", cact_acos(2.0), NAN);

    /* ldexp: exact, including subnormal, overflow and saturation */
    {
        volatile int E[] = { 0, 1, -1, 10, -10, 52, 53, 1023, -1074, -1075,
                             2200, -2200, 2100, -2100, 100000, -100000,
                             2147483647, -2147483647 };
        const int NE = (int)(sizeof E / sizeof E[0]);
        volatile double X[] = { 1.0, -1.0, 0.5, 3.0, 1e300, 1e-300, 0.0, 5e-324 };
        const int NX = (int)(sizeof X / sizeof X[0]);
        for (int i = 0; i < NX; i++)
            for (int j = 0; j < NE; j++)
                ckx("ldexp", cact_ldexp(X[i], E[j]), ldexp(X[i], E[j]));
    }
    ckx("ldexp(nan)", cact_ldexp(NAN, 100), NAN);
    ckx("ldexp(inf)", cact_ldexp(INFINITY, -100), INFINITY);
    ckx("ldexp(-inf)", cact_ldexp(-INFINITY, 100), -INFINITY);

    /* ldexp's ERANGE, compared with glibc: overflow to an infinity and
     * underflow to zero are errors, a denormal result is not. */
    {
        volatile int E[] = { 5000, -5000, -1074, -1075, 100, 1000, 0, 5000 };
        volatile double X[] = { 1.0, 1.0, 1.0, 1.0, 1e300, 1.0, 0.0, 0.0 };
        for (int i = 0; i < 8; i++) {
            int ge, we;
            double g, w;
            *cact_host_errno_ptr() = 0;
            g = cact_ldexp(X[i], E[i]);
            ge = *cact_host_errno_ptr();
            errno = 0;
            w = ldexp(X[i], E[i]);
            we = errno;
            checks++;
            if (g != w || (ge != 0) != (we != 0)) {
                fails++;
                printf("FAIL ldexp(%g,%d) got %g errno=%d want %g errno=%d\n",
                       X[i], E[i], g, ge, w, we);
            }
        }
    }

    /* Large arguments: the Payne–Hanek reduction keeps sin/cos accurate out to
     * the largest doubles, where the fprem path against an 80-bit 2*pi runs out
     * of modulus (the README used to document the break around 1e10). */
    {
        static const double big[] = { 1e6, 1e7, 123456789.0, 1e10, 1e12, 1e17,
                                      1e30, 1e100, 1e300, 1.7e308 };
        for (unsigned i = 0; i < sizeof big / sizeof big[0]; i++) {
            double x = big[i];
            ckabs("big sin",  cact_sin(x),  sin(x),  1e-13);
            ckabs("big cos",  cact_cos(x),  cos(x),  1e-13);
            ckabs("big sin-", cact_sin(-x), sin(-x), 1e-13);
            ckabs("big cos-", cact_cos(-x), cos(-x), 1e-13);
            /* tan's absolute error scales with its derivative 1+tan^2, which is
             * the same shape the small-argument check uses. */
            ckabs("big tan",  cact_tan(x),  tan(x),  1e-15 * (1.0 + tan(x) * tan(x)));
            ckabs("big tan-", cact_tan(-x), tan(-x), 1e-15 * (1.0 + tan(-x) * tan(-x)));
        }
    }

    /* pow with a negative base: parity of the exponent decides the sign */
    ckx("pow(-2,3)",      cact_pow(-2.0, 3.0),      pow(-2.0, 3.0));
    ckx("pow(-2,2)",      cact_pow(-2.0, 2.0),      pow(-2.0, 2.0));
    ckx("pow(-2,2.5)",    cact_pow(-2.0, 2.5),      pow(-2.0, 2.5));
    /* 2^52 and up are always even integers: a positive result, never NaN */
    ckx("pow(-2,2^52)",   cact_pow(-2.0, 4503599627370496.0),
                          pow(-2.0, 4503599627370496.0));
    ckx("pow(-2,2^52-1)", cact_pow(-2.0, 4503599627370495.0),
                          pow(-2.0, 4503599627370495.0));
    ckx("pow(-1,2^60)",   cact_pow(-1.0, 1152921504606846976.0),
                          pow(-1.0, 1152921504606846976.0));
    ckx("pow(0,0)",       cact_pow(0.0, 0.0),       pow(0.0, 0.0));
    ckx("pow(0,-1)",      cact_pow(0.0, -1.0),      pow(0.0, -1.0));

    printf("math: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
