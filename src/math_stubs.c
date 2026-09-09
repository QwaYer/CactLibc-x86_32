#include "math.h"

double ldexp(double x, int exp) {
    if (exp > 0) { while (exp--) x *= 2.0; }
    else { while (exp++) x /= 2.0; }
    return x;
}

double round(double x) {
    if (x >= 0.0) {
        double lo = (double)(long long)x;
        return (x - lo >= 0.5) ? lo + 1.0 : lo;
    }
    double hi = (double)(long long)x;
    double frac = x - hi;
    return (frac <= -0.5) ? hi - 1.0 : hi;
}

double ceil(double x) {
    double lo = (double)(long long)x;
    if (x == lo) return x;
    return (x > 0.0) ? lo + 1.0 : lo;
}

double floor(double x) {
    double lo = (double)(long long)x;
    if (x == lo) return x;
    return (x < 0.0) ? lo - 1.0 : lo;
}
