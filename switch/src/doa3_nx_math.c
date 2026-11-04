// SPDX-License-Identifier: GPL-3.0-or-later
// Native versions of DOA3's hottest math helpers.
//
// The lifted versions keep every intermediate on the emulated x87 stack in
// guest-visible memory; these keep them in registers. Each one reproduces the
// helper's architectural effects: the same per-operation rounding (the x87
// precision control, RECOMP_FP_PC), the same outputs and register values, the
// stack slots it writes below ESP and the flags it leaves. x87 stack slots
// above the top are scratch the guest never reads, so they are not written.
//
// Generated code reaches them through RECOMP_ABI_CALL (doa3_nx_native.h) and
// indirect calls through doa3_nx_native_lookup (profile.cpp).
#ifndef DOA3_NX_NATIVE_OFF
#define DOA3_NX_NATIVE_OFF
#endif
#include "recomp_types.h"
#include "doa3_nx_native.h"

#define PC(x) RECOMP_FP_PC(x)

// Off (X held at boot, RECOMP_NATIVE_MATH=0): every helper runs the lifted
// version instead, to compare without rebuilding.
int doa3_nx_native_enabled = 1;
void sub_00154750(void);
void sub_001547E0(void);
void sub_001548E0(void);
void sub_00154570(void);
void sub_00154680(void);
#define LIFTED_IF_DISABLED(name) do { if (!doa3_nx_native_enabled) { name(); return; } } while (0)

// The current matrix the 0x00154680 / 0x00154570 helpers load.
#define CURRENT_MATRIX 0x0090FAA0u

static inline __attribute__((always_inline)) double mulf(uint32_t a, uint32_t b) { return PC((double)MEMF(a) * (double)MEMF(b)); }

// A float that single-precision arithmetic produced exactly as the x87 would
// with PC=24: zero, or normal and finite. Denormals, overflow and NaN keep
// the double exponent range on the x87, so those take the exact path.
static inline __attribute__((always_inline)) int plain(float x)
{
    const float a = __builtin_fabsf(x);
    return x == 0.0f || (a >= 1.17549435e-38f && a <= 3.40282347e38f);
}

// A product that underflowed to zero is not exact: the x87 keeps it tiny.
static inline __attribute__((always_inline)) int product_plain(float p, float x, float y)
{
    return p == 0.0f ? (x == 0.0f || y == 0.0f) : plain(p);
}

// ((a0*b0 + a1*b1) + a2*b2) [+ t], each operation rounded as the x87 does.
// With PC=24 single-precision operations round identically (double rounding
// through the x87's wider format is innocuous for +, -, *), so they run in
// float unless a value leaves the normal range. Built with -ffp-contract=off:
// a fused multiply-add would round differently.
static inline __attribute__((always_inline)) double dot3(uint32_t a0, uint32_t b0, uint32_t a1,
    uint32_t b1, uint32_t a2, uint32_t b2, int translate, uint32_t t)
{
    if ((g_fp_control_word & 0x300u) == 0u) {
        const float x0 = MEMF(a0), y0 = MEMF(b0), x1 = MEMF(a1), y1 = MEMF(b1), x2 = MEMF(a2), y2 = MEMF(b2);
        const float p0 = x0 * y0, p1 = x1 * y1, p2 = x2 * y2;
        const float s1 = p0 + p1, s2 = s1 + p2;
        const float s3 = translate ? s2 + MEMF(t) : s2;
        if (product_plain(p0, x0, y0) && product_plain(p1, x1, y1) && product_plain(p2, x2, y2) &&
            plain(s1) && plain(s2) && plain(s3)) {
            return s3;
        }
    }
    double s = PC(mulf(a0, b0) + mulf(a1, b1));
    s = PC(s + mulf(a2, b2));
    if (translate) s = PC(s + (double)MEMF(t));
    return s;
}

// Same x87-compatible dot product when a caller has already resolved its RAM
// spans. This lets a batch of matrix transforms reuse one bounds check and
// the same loaded vector instead of resolving every scalar guest address.
static inline __attribute__((always_inline)) double dot3_values(float x0, float y0, float x1,
    float y1, float x2, float y2, int translate, float t)
{
    if ((g_fp_control_word & 0x300u) == 0u) {
        const float p0 = x0 * y0, p1 = x1 * y1, p2 = x2 * y2;
        const float s1 = p0 + p1, s2 = s1 + p2;
        const float s3 = translate ? s2 + t : s2;
        if (product_plain(p0, x0, y0) && product_plain(p1, x1, y1) && product_plain(p2, x2, y2) &&
            plain(s1) && plain(s2) && plain(s3)) {
            return s3;
        }
    }
    const double p0 = PC((double)x0 * (double)y0);
    const double p1 = PC((double)x1 * (double)y1);
    const double p2 = PC((double)x2 * (double)y2);
    double sum = PC(p0 + p1);
    sum = PC(sum + p2);
    if (translate) sum = PC(sum + (double)t);
    return sum;
}

// Vector (ECX) times the current matrix, rows 0-2 plus the translation row,
// into EDX. The z bits end up in EAX, ECX and the slot ECX was pushed to.
static inline __attribute__((always_inline)) void transform_current(int translate)
{
    const uint32_t v = ecx, out = edx, m = CURRENT_MATRIX;
    double r[3];
    uint32_t z_bits = 0u;
    const uint8_t *matrix_bytes = recomp_memory_ram(m, 16u * sizeof(float));
    const uint8_t *vector_bytes = recomp_memory_ram(v, 3u * sizeof(float));
    uint8_t *out_bytes = recomp_memory_ram(out, 3u * sizeof(float));
    uint8_t *stack_bytes = recomp_memory_ram(esp - 4u, sizeof(uint32_t));
    if (matrix_bytes != NULL && vector_bytes != NULL && out_bytes != NULL && stack_bytes != NULL) {
        const float *matrix = (const float *)(const void *)matrix_bytes;
        const float *vector = (const float *)(const void *)vector_bytes;
#pragma GCC unroll 3
        for (uint32_t c = 0u; c < 3u; ++c) {
            r[c] = dot3_values(matrix[c], vector[0], matrix[4u + c], vector[1],
                matrix[8u + c], vector[2], translate, matrix[12u + c]);
        }
        const float z = (float)r[2];
        __builtin_memcpy(&z_bits, &z, sizeof z_bits);
        *(uint32_t *)(void *)stack_bytes = z_bits; // fstp into the pushed ECX slot
        eax = z_bits;
        float *out_values = (float *)(void *)out_bytes;
        out_values[0] = (float)r[0];
        ((uint32_t *)(void *)out_bytes)[2] = z_bits;
        out_values[1] = (float)r[1];
    } else {
#pragma GCC unroll 3
        for (uint32_t c = 0u; c < 3u; ++c) {
            r[c] = dot3(m + c * 4u, v, m + 0x10u + c * 4u, v + 4u, m + 0x20u + c * 4u, v + 8u,
                translate, m + 0x30u + c * 4u);
        }
        const float z = (float)r[2];
        __builtin_memcpy(&z_bits, &z, sizeof z_bits);
        MEM32(esp - 4u) = z_bits; // fstp into the pushed ECX slot
        eax = z_bits;
        MEMF(out) = (float)r[0];
        MEM32(out + 8u) = z_bits;
        MEMF(out + 4u) = (float)r[1];
    }
    ecx = z_bits;
    esp += 4u;
}

void doa3_nx_native_00154750(void)
{
    LIFTED_IF_DISABLED(sub_00154750);
    transform_current(1);
}
void doa3_nx_native_001547E0(void)
{
    LIFTED_IF_DISABLED(sub_001547E0);
    transform_current(0);
}

// cdecl (matrix, vector, out): vector times matrix plus translation.
void doa3_nx_native_001548E0(void)
{
    LIFTED_IF_DISABLED(sub_001548E0);
    const uint32_t m = MEM32(esp + 4u), v = MEM32(esp + 8u), out = MEM32(esp + 0xCu);
    double r[3];
#pragma GCC unroll 3
    for (uint32_t c = 0u; c < 3u; ++c) {
        r[c] = dot3(m + 0x20u + c * 4u, v + 8u, m + 0x10u + c * 4u, v + 4u, m + c * 4u, v,
            1, m + 0x30u + c * 4u);
    }
    MEMF(out) = (float)r[0];
    MEMF(out + 4u) = (float)r[1];
    MEMF(out + 8u) = (float)r[2];
    ecx = v;
    eax = out;
    // Flags of the helper's last "add eax, 4" (matrix + 4 -> matrix + 8).
    const uint32_t before = m + 4u;
    const int cf = (int)((((uint64_t)before + 4u) >> 32) & 1u);
    const uint32_t fa = before + 4u, fb = 4u;
    const int32_t fas = (int32_t)fa;
    g_eflags = (g_eflags & ~0x8C5u) | (cf ? 0x1u : 0u) | (RECOMP_PARITY8(fa) ? 0x4u : 0u) |
        (fa == 0u ? 0x40u : 0u) | (fas < 0 ? 0x80u : 0u) |
        ((((uint32_t)(((fa - fb) ^ fa) & (fb ^ fa)) >> 31) != 0u) ? 0x800u : 0u);
    esp += 4u;
}

// cdecl (matrix): copies 64 bytes into the current matrix, with SSE when the
// source is 16-byte aligned and rep movsd otherwise.
void doa3_nx_native_00154570(void)
{
    LIFTED_IF_DISABLED(sub_00154570);
    const uint32_t src = MEM32(esp + 4u);
    MEM32(esp - 4u) = g_ebp; // push ebp
    eax = src;
    uint32_t fa, fb = 0xFu;
    if ((src & 0xFu) != 0u) {
        fa = src & 0xFFu;
        MEM32(esp - 8u) = esi;
        MEM32(esp - 12u) = edi;
        if (!g_df) {
            for (uint32_t i = 0u; i < 16u; ++i) MEM32(CURRENT_MATRIX + i * 4u) = MEM32(src + i * 4u);
        } else {
            for (uint32_t i = 0u; i < 16u; ++i) MEM32(CURRENT_MATRIX - i * 4u) = MEM32(src - i * 4u);
        }
        ecx = 0u;
    } else {
        fa = CURRENT_MATRIX & 0xFFu;
        ecx = CURRENT_MATRIX;
        xmm0 = XMM_MEM(src);
        xmm1 = XMM_MEM(src + 0x10u);
        xmm2 = XMM_MEM(src + 0x20u);
        xmm3 = XMM_MEM(src + 0x30u);
        XMM_STORE(CURRENT_MATRIX, xmm0);
        XMM_STORE(CURRENT_MATRIX + 0x10u, xmm1);
        XMM_STORE(CURRENT_MATRIX + 0x20u, xmm2);
        XMM_STORE(CURRENT_MATRIX + 0x30u, xmm3);
    }
    const int32_t fas = (int32_t)(int8_t)fa, fbs = (int32_t)(int8_t)fb;
    g_eflags = (g_eflags & ~0x8C5u) | ((CMP_B(fa & fb, 0)) ? 0x1u : 0u) |
        ((RECOMP_PARITY8(fa & fb)) ? 0x4u : 0u) | ((TEST_Z(fa, fb)) ? 0x40u : 0u) |
        ((TEST_S(fas, fbs)) ? 0x80u : 0u);
    esp += 4u;
}

// Register (ECX = matrix) wrapper around 0x00154570.
void doa3_nx_native_00154680(void)
{
    LIFTED_IF_DISABLED(sub_00154680);
    const uint32_t matrix = ecx;
    PUSH32(esp, matrix);
    PUSH32(esp, 0x00154686u); // return address
    doa3_nx_native_00154570();
    POP32(esp, ecx);
    esp += 4u;
}

RecompFunction doa3_nx_native_lookup(uint32_t guest_address)
{
    switch (guest_address) {
    case 0x00154750u: return doa3_nx_native_00154750;
    case 0x001547E0u: return doa3_nx_native_001547E0;
    case 0x001548E0u: return doa3_nx_native_001548E0;
    case 0x00154570u: return doa3_nx_native_00154570;
    case 0x00154680u: return doa3_nx_native_00154680;
    default: return 0;
    }
}
