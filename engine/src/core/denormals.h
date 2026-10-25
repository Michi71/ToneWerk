// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// denormals.h - flush denormals to zero for the scope of the audio callback
#pragma once

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define PMP_DENORMALS_SSE 1
#endif

namespace pmp {

class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() {
#if defined(PMP_DENORMALS_SSE)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040);            // FTZ | DAZ
#elif defined(__aarch64__)
        unsigned long long fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24);                   // FZ
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~ScopedFlushDenormals() {
#if defined(PMP_DENORMALS_SSE)
        _mm_setcsr(static_cast<unsigned int>(saved_));
#elif defined(__aarch64__)
        const unsigned long long fpcr = saved_;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    unsigned long long saved_ = 0;
};

} // namespace pmp
