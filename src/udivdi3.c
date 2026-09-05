/* Minimal soft division for 64-bit operands on i386 (no libgcc). */

typedef unsigned long long u64;
typedef long long i64;

u64 __udivdi3(u64 a, u64 b);
u64 __umoddi3(u64 a, u64 b);
i64 __divdi3(i64 a, i64 b);
i64 __moddi3(i64 a, i64 b);

u64 __udivdi3(u64 a, u64 b) {
    if (b == 0) return 0;
    u64 q = 0;
    u64 r = a;
    int shift = 0;
    while (b < r && !(b & 0x8000000000000000ULL)) {
        b <<= 1;
        shift++;
    }
    for (; shift >= 0; shift--) {
        if (r >= b) {
            r -= b;
            q |= 1ULL << shift;
        }
        b >>= 1;
    }
    return q;
}

u64 __umoddi3(u64 a, u64 b) {
    if (b == 0) return 0;
    return a - __udivdi3(a, b) * b;
}

i64 __divdi3(i64 a, i64 b) {
    int neg = (a < 0) != (b < 0);
    u64 ua = a < 0 ? (u64)(-(a + 1)) + 1ULL : (u64)a;
    u64 ub = b < 0 ? (u64)(-(b + 1)) + 1ULL : (u64)b;
    u64 q = __udivdi3(ua, ub);
    return neg ? -(i64)q : (i64)q;
}

i64 __moddi3(i64 a, i64 b) {
    return a - __divdi3(a, b) * b;
}
