#ifndef _MATH_H
#define _MATH_H

/* CactOS: freestanding x87 math (src/math.c).  No libm — the transcendentals
 * are FPU instructions and the integral helpers are bit/counting code. */

#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))

#define isnan(x)     __builtin_isnan(x)
#define isinf(x)     __builtin_isinf(x)
#define isfinite(x)  __builtin_isfinite(x)

double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double fabs(double x);
double log(double x);
double log10(double x);
double exp(double x);
double pow(double x, double y);
double atan2(double y, double x);
double atan(double x);
double asin(double x);
double acos(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);

#endif
