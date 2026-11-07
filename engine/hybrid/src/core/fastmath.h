// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// fastmath.h - libm replacements for the real-time path of float builds
//
// On a Cortex-M33 (single-precision FPU) newlib's powf costs about 280 instructions (measured
// with the M33 bench of the Pico port, removed 2026-10-20), and the hammer contact calls it once per sub-step. The functions
// below use only + * one division and the float exponent field, no tables (nothing to keep
// out of the XIP cache), and stay within a few ulp of the exact result.
//
// Double builds are the calibration reference and keep libm: see powr().
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "config.h"

namespace pmp {
namespace fastmath_detail {

inline float fromBits(uint32_t b) { float f; std::memcpy(&f, &b, sizeof f); return f; }
inline uint32_t toBits(float f) { uint32_t b; std::memcpy(&b, &f, sizeof b); return b; }

// 2^n for an integer n in [-126, 127]
inline float exp2Int(int n) { return fromBits(uint32_t(n + 127) << 23); }

// 2^f for f in [0, 1]: degree-6 minimax polynomial, below 1 ulp in float
inline float exp2Unit(float f) {
    return 1.0f + f * (0.693147044f + f * (0.240229306f + f * (0.0554852806f +
           f * (0.00967545167f + f * (0.00124678453f + f * 0.000216129194f)))));
}

// x = m 2^e with m in [sqrt(1/2), sqrt(2)], for a normal x > 0 (0 gives m = 1, e = -127)
inline float splitExponent(float x, int& e) {
    const uint32_t b = toBits(x);
    e = int(b >> 23) - 127;
    float m = fromBits((b & 0x007FFFFFu) | 0x3F800000u);
    if (m > 1.41421356f) {
        m *= 0.5f;
        ++e;
    }
    return m;
}

// log2 m for m in [sqrt(1/2), sqrt(2)]: 2/ln2 atanh(t) with t = (m - 1)/(m + 1), |t| <= 0.172;
// the series is truncated below 1e-9
inline float log2Near1(float m) {
    const float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    return t * (2.88539008f + t2 * (0.961796694f + t2 * (0.577078016f +
           t2 * (0.412198583f + t2 * 0.320598898f))));
}

} // namespace fastmath_detail

// 2^x: 0 at or below -126, 2^127 at or above 127
inline float fastExp2(float x) {
    using namespace fastmath_detail;
    if (!(x > -126.0f)) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    const float n = std::floor(x);
    return exp2Int(int(n)) * exp2Unit(x - n);
}

// log2 x for a normal x > 0
inline float fastLog2(float x) {
    using namespace fastmath_detail;
    int e;
    const float m = splitExponent(x, e);
    return float(e) + log2Near1(m);
}

// x^a for a normal x > 0 (0 for x = 0 and a > 0), results between 2^-126 and 2^127.
// x^a = 2^(a e) m^a, and the product a e is split exactly with an FMA: a plain
// exp2(a log2 x) would lose the low bits of a log2 x (log2 x ~ -20 for the felt compression)
// and be off by some 1e-6.
inline float fastPow(float x, float a) {
    using namespace fastmath_detail;
    int e;
    const float m = splitExponent(x, e);
    const float fe = float(e);
    const float p = a * fe;
    const float pLo = std::fma(a, fe, -p);                 // a e = p + pLo exactly
    const float n = std::floor(p);
    const float f = (p - n) + (pLo + a * log2Near1(m));    // p - n is exact
    const float n2 = std::floor(f);
    const int k = int(n) + int(n2);
    if (k < -126) return 0.0f;
    return exp2Int(k < 127 ? k : 127) * exp2Unit(f - n2);
}

// x^a on the real-time path (x > 0): fastPow in float builds, libm in double builds
inline real powr(real x, real a) {
#if defined(PMP_FLOAT)
    return fastPow(x, a);
#else
    return std::pow(x, a);
#endif
}

} // namespace pmp
