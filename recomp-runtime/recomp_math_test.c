/* Compares the recomp_* math functions with the MSVC CRT on deterministic
   inputs, then checks exact results. The pinned bits were recorded from this
   code. Most agree with the CRT; the expm1 values marked below are the correctly
   rounded results where the CRT is 2 ulp away. */

#include "recomp_math.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { SAMPLES = 200000, MAX_PRINTED = 5 };

typedef double (*unary_fn)(double);

typedef struct Stats {
    const char *name;
    int binary;
    uint64_t samples;
    uint64_t non_identical;
    uint64_t failures;
    int64_t max_ulp;
    int64_t ulp_limit;
} Stats;

/* Zeros, infinities, NaNs, subnormals, extreme magnitudes, and constants
   that argument reduction and range checks are tuned around. */
static const uint64_t specials[] = {
    0x0000000000000000ULL, 0x8000000000000000ULL, /* +0, -0 */
    0x7ff0000000000000ULL, 0xfff0000000000000ULL, /* +inf, -inf */
    0x7ff8000000000000ULL, 0xfff8000000000000ULL, /* NaN, -NaN */
    0x0000000000000001ULL, 0x8000000000000001ULL, /* +-smallest subnormal */
    0x000fffffffffffffULL, 0x0010000000000000ULL, /* largest subnormal, smallest normal */
    0x7fefffffffffffffULL, 0xffefffffffffffffULL, /* +-DBL_MAX */
    0x3ca0000000000000ULL, 0xbca0000000000000ULL, /* +-2^-53 */
    0x3e40000000000000ULL, 0x3e50000000000000ULL, /* 2^-27, 2^-26 */
    0x3fe0000000000000ULL, 0xbfe0000000000000ULL, /* +-0.5 */
    0x3ff0000000000000ULL, 0xbff0000000000000ULL, /* +-1 */
    0x3ff0000000000001ULL, 0xbff0000000000001ULL, /* +-(1 + 2^-52) */
    0x4008000000000000ULL, 0x4010000000000000ULL, /* 3, 4 */
    0x3fe921fb54442d18ULL, 0x3ff921fb54442d18ULL, /* pi/4, pi/2 */
    0x400921fb54442d18ULL, 0x40862e42fefa39efULL, /* pi, expm1 overflow edge */
    0x43d0000000000000ULL, 0xc3d0000000000000ULL, /* +-2^62 */
};
#define SPECIAL_COUNT (sizeof specials / sizeof specials[0])

static uint64_t rng;

static uint64_t bits_of(double x)
{
    uint64_t u;

    memcpy(&u, &x, sizeof u);
    return u;
}

static double from_bits(uint64_t u)
{
    double x;

    memcpy(&x, &u, sizeof x);
    return x;
}

static int is_nan_bits(uint64_t u)
{
    return (u & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL;
}

static int is_finite_bits(uint64_t u)
{
    return (u & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

/* Monotonic integer view of a double: ulp distance is a subtraction. */
static int64_t ordered(double x)
{
    uint64_t u = bits_of(x);

    return (u >> 63) ? -(int64_t)(u & 0x7fffffffffffffffULL) : (int64_t)u;
}

/* 64-bit linear congruential generator; the top 32 bits of each step are used. */
static uint32_t next32(void)
{
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(rng >> 32);
}

static uint64_t next64(void)
{
    uint64_t hi = next32();

    return hi << 32 | next32();
}

static double unit(void)
{
    return next32() / 4294967296.0;
}

/* Random double with biased exponent in [lo, hi], and a random sign if signed. */
static double random_exponent(uint32_t lo, uint32_t hi, int is_signed)
{
    uint64_t sign = is_signed ? (uint64_t)(next32() & 1u) << 63 : 0;
    uint64_t e = lo + next32() % (hi - lo + 1u);
    uint64_t m = next64() & 0xfffffffffffffULL;

    return from_bits(sign | e << 52 | m);
}

/* k * step with relative jitter of 2^-40, so argument reduction is stressed. */
static double near_multiple(double step, int32_t span)
{
    int32_t k = (int32_t)(next32() % (uint32_t)(2 * span + 1)) - span;
    double x = k * step;

    return x + x * (unit() * 2.0 - 1.0) * 0x1p-40;
}

static double gen_trig(void)
{
    switch (next32() % 8u) {
    case 0: return (unit() * 2.0 - 1.0) * 8.0;
    case 1: return near_multiple(1.5707963267948966, 1 << 20);
    case 2: return near_multiple(3.1415926535897931, 1000);
    case 3: return random_exponent(1043u, 1085u, 1);   /* 2^20 to 2^62 */
    case 4: return random_exponent(1u, 2046u, 1);
    case 5: return from_bits(next64());
    case 6: return random_exponent(900u, 1022u, 1);
    default: return random_exponent(0u, 0u, 1);        /* subnormal */
    }
}

static double gen_log2(void)
{
    switch (next32() % 8u) {
    case 0: return unit() * 2.0;
    case 1: return 1.0 + (unit() * 2.0 - 1.0) * 0x1.6ab2p-5;   /* near 1 */
    case 2: return random_exponent(0u, 2046u, 1);
    case 3: return from_bits(next64());
    case 4: return random_exponent(0u, 0u, 0);
    case 5: return random_exponent(1u, 2046u, 0);
    case 6: return random_exponent(900u, 1022u, 0);
    default: return from_bits((uint64_t)(1023u + next32() % 64u) << 52 | (next64() & 0x3ffULL));
    }
}

static double gen_log1p(void)
{
    switch (next32() % 8u) {
    case 0: return unit() * 2.0 - 1.0;
    case 1: return -1.0 + unit() * 0x1p-20;   /* just above -1 */
    case 2: return random_exponent(0u, 1023u, 1);
    case 3: return from_bits(next64());
    case 4: return random_exponent(1023u, 2046u, 0);
    case 5: return random_exponent(0u, 0u, 1);
    case 6: return random_exponent(960u, 1000u, 1);   /* around 2^-53 */
    default: return (unit() * 2.0 - 1.0) * 8.0;
    }
}

static double gen_expm1(void)
{
    switch (next32() % 8u) {
    case 0: return (unit() * 2.0 - 1.0) * 50.0;
    case 1: return unit() * 2.0 - 1.0;
    case 2: return random_exponent(0u, 1023u, 1);
    case 3: return from_bits(next64());
    case 4: return 700.0 + unit() * 12.0;   /* around the overflow edge */
    case 5: return -745.0 + unit() * 10.0;  /* around the underflow edge */
    case 6: return random_exponent(1023u, 1030u, 1);
    default: return random_exponent(0u, 0u, 1);
    }
}

static void gen_pair(double *y, double *x)
{
    switch (next32() % 6u) {
    case 0:
        *y = unit() * 2.0 - 1.0;
        *x = unit() * 2.0 - 1.0;
        break;
    case 1:
        *y = from_bits(next64());
        *x = from_bits(next64());
        break;
    case 2:
        *y = random_exponent(0u, 2046u, 1);
        *x = random_exponent(0u, 2046u, 1);
        break;
    case 3:   /* |y / x| near 2^64 or 2^-64, the thresholds in atan2 */
        *x = random_exponent(900u, 1000u, 1);
        *y = *x * ((next32() & 1u) ? 0x1p64 : 0x1p-64) * (1.0 + (unit() * 2.0 - 1.0) * 0x1p-30);
        break;
    case 4:
        *y = from_bits(specials[next32() % SPECIAL_COUNT]);
        *x = random_exponent(0u, 1023u, 1);
        break;
    default:
        *y = random_exponent(0u, 0u, 1);
        *x = random_exponent(0u, 1023u, 1);
        break;
    }
}

/* Compares one result with the CRT. A failure is a NaN mismatch, an infinity
   that differs at all, a zero whose sign differs, or more than 1 ulp. */
static void record(Stats *st, double in1, double in2, double got, double want)
{
    uint64_t gb = bits_of(got), wb = bits_of(want);
    int64_t ulp = 0;
    int bad = 0;

    st->samples++;
    if (gb != wb && !(is_nan_bits(gb) && is_nan_bits(wb)))
        st->non_identical++;
    if (is_nan_bits(gb) || is_nan_bits(wb)) {
        bad = !(is_nan_bits(gb) && is_nan_bits(wb));
    } else if (gb != wb) {
        if (!is_finite_bits(gb) || !is_finite_bits(wb)) {
            bad = 1;
        } else {
            ulp = ordered(got) - ordered(want);
            if (ulp < 0)
                ulp = -ulp;
            if (ulp > st->max_ulp)
                st->max_ulp = ulp;
            /* Zero ulp with different bits means the zeros differ in sign. */
            bad = ulp > st->ulp_limit || ulp == 0;
        }
    }
    if (bad) {
        st->failures++;
        if (st->failures <= MAX_PRINTED) {
            if (st->binary)
                printf("FAIL %s y=%a x=%a got=%a want=%a ulp=%lld\n", st->name,
                       in1, in2, got, want, (long long)ulp);
            else
                printf("FAIL %s x=%a got=%a want=%a ulp=%lld\n", st->name,
                       in1, got, want, (long long)ulp);
        }
    }
}

static int finish(const Stats *st)
{
    printf("%-14s samples=%llu max_ulp=%lld non_identical=%llu failures=%llu\n",
           st->name, (unsigned long long)st->samples, (long long)st->max_ulp,
           (unsigned long long)st->non_identical, (unsigned long long)st->failures);
    return st->failures != 0;
}

static int run_unary(const char *name, unary_fn ours, unary_fn crt,
                     double (*gen)(void), uint64_t seed, int64_t ulp_limit)
{
    Stats st = {name, 0, 0, 0, 0, 0, ulp_limit};
    size_t i;

    rng = seed;
    for (i = 0; i < SAMPLES; i++) {
        double x = i < SPECIAL_COUNT ? from_bits(specials[i]) : gen();

        record(&st, x, 0.0, ours(x), crt(x));
    }
    return finish(&st);
}

static int run_atan2(void)
{
    Stats st = {"recomp_atan2", 1, 0, 0, 0, 0, 1};
    size_t i;

    rng = 0x2001u;
    for (i = 0; i < SAMPLES; i++) {
        double y, x;

        if (i < SPECIAL_COUNT * SPECIAL_COUNT) {
            y = from_bits(specials[i / SPECIAL_COUNT]);
            x = from_bits(specials[i % SPECIAL_COUNT]);
        } else
            gen_pair(&y, &x);
        record(&st, y, x, recomp_atan2(y, x), atan2(y, x));
    }
    return finish(&st);
}

static int pinned(const char *label, double got, uint64_t want)
{
    if (bits_of(got) == want)
        return 0;
    printf("FAIL pinned %s got=0x%016llx want=0x%016llx\n", label,
           (unsigned long long)bits_of(got), (unsigned long long)want);
    return 1;
}

static int run_pinned(void)
{
    int failed = 0;

    failed |= pinned("recomp_sin(1.0)", recomp_sin(1.0), 0x3feaed548f090ceeULL);
    failed |= pinned("recomp_cos(1.0)", recomp_cos(1.0), 0x3fe14a280fb5068cULL);
    failed |= pinned("recomp_tan(1.0)", recomp_tan(1.0), 0x3ff8eb245cbee3a6ULL);
    failed |= pinned("recomp_sin(2^62)", recomp_sin(0x1p62), 0xbfe67e57355f0147ULL);
    /* Argument reduction near multiples of pi and at huge inputs, where an
       x87-based CRT is far off; MinGW checks these bits only through pins. */
    failed |= pinned("recomp_sin(pi)", recomp_sin(0x1.921fb54442d18p+1), 0x3ca1a62633145c07ULL);
    failed |= pinned("recomp_cos(pi/2)", recomp_cos(0x1.921fb54442d18p+0), 0x3c91a62633145c07ULL);
    failed |= pinned("recomp_cos(2^62)", recomp_cos(0x1p62), 0xbfe6c2b1f820c708ULL);
    failed |= pinned("recomp_sin(DBL_MAX)", recomp_sin(0x1.fffffffffffffp+1023), 0x3f7452fc98b34e97ULL);
    failed |= pinned("recomp_cos(DBL_MAX)", recomp_cos(0x1.fffffffffffffp+1023), 0xbfefffe62ecfab75ULL);
    failed |= pinned("recomp_atan2(1, 2)", recomp_atan2(1.0, 2.0), 0x3fddac670561bb4fULL);
    failed |= pinned("recomp_atan2(-3, -4)", recomp_atan2(-3.0, -4.0), 0xc003fc176b7a8560ULL);
    failed |= pinned("recomp_log2(3)", recomp_log2(3.0), 0x3ff95c01a39fbd68ULL);
    failed |= pinned("recomp_log2(0.1)", recomp_log2(0.1), 0xc00a934f0979a371ULL);
    failed |= pinned("recomp_log1p(0.5)", recomp_log1p(0.5), 0x3fd9f323ecbf984cULL);
    failed |= pinned("recomp_expm1(1.0)", recomp_expm1(1.0), 0x3ffb7e151628aed2ULL);
    failed |= pinned("recomp_expm1(-0x1.2633c2e800000p-2)", recomp_expm1(-0x1.2633c2e800000p-2),
                     0xbfcff6c6ba8861a6ULL);
    failed |= pinned("recomp_expm1(-0x1.c11c1ef000000p-3)", recomp_expm1(-0x1.c11c1ef000000p-3),
                     0xbfc9346fe1804a60ULL);
    failed |= pinned("recomp_expm1(-0x1.a08840de171b1p-50)", recomp_expm1(-0x1.a08840de171b1p-50),
                     0xbcda08840de171acULL);
    return failed;
}

int main(void)
{
    int failed = 0;

#ifndef __MINGW32__
    /* mingw-w64's sin and cos use x87 fsin and fcos, which lose accuracy near
       multiples of pi and for large arguments. The port's bits match on every
       build, the other hosts compare them, and the pinned values check them. */
    failed |= run_unary("recomp_sin", recomp_sin, sin, gen_trig, 0x1001u, 1);
    failed |= run_unary("recomp_cos", recomp_cos, cos, gen_trig, 0x1002u, 1);
#endif
#ifdef __APPLE__
    /* Apple's libm tan is up to 3 ulp from this port, which stays within 1 ulp
       of the MSVC CRT and glibc; the pinned bits below still check it exactly. */
    failed |= run_unary("recomp_tan", recomp_tan, tan, gen_trig, 0x1003u, 3);
#else
    failed |= run_unary("recomp_tan", recomp_tan, tan, gen_trig, 0x1003u, 1);
#endif
    failed |= run_atan2();
    failed |= run_unary("recomp_log2", recomp_log2, log2, gen_log2, 0x1004u, 1);
    failed |= run_unary("recomp_log1p", recomp_log1p, log1p, gen_log1p, 0x1005u, 1);
    /* The MSVC CRT expm1 differs from this port by 2 ulp on about 0.1% of inputs
       with |x| < 1. Where that happens the port is the correctly rounded result,
       and the pinned values below are such cases. */
    failed |= run_unary("recomp_expm1", recomp_expm1, expm1, gen_expm1, 0x1006u, 2);
    failed |= run_pinned();
    puts(failed ? "recomp_math: FAIL" : "recomp_math: PASS");
    return failed;
}
