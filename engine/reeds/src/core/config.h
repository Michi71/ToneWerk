// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// config.h - global constants and small helpers of the pmreeds engine
#pragma once

#include <cmath>

namespace pmp {

// Arithmetic of the real-time path: double on desktop hosts (the calibration reference), float
// where single precision pays, as in four-lane NEON SIMD on the Raspberry Pi.
// Select with -DPMP_FLOAT (CMake: -DPMP_FLOAT=ON). Setup code always uses double.
#if defined(PMP_FLOAT)
using real = float;
#else
using real = double;
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PMP_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define PMP_INLINE __forceinline
#else
#define PMP_INLINE inline
#endif

// The keyboard of the engine spans both instruments: the Pianet T plays F1-E6 (MIDI 29-88), the Wurlitzer 200A
// A1-C7 (33-96). A preset sets its own range (keys.lo, keys.hi); the keys outside it stay silent.
constexpr int kFirstKey = 29;                        // F1
constexpr int kLastKey  = 96;                        // C7
constexpr int kNumKeys  = kLastKey - kFirstKey + 1;  // 68 keys
constexpr int kMaxModes = 16;                        // modes per key (reed)
constexpr int kBlock    = 32;                        // internal block size at the host rate

constexpr double kPi     = 3.14159265358979323846;
constexpr double kLn1000 = 6.907755278982137;        // ln(1000): converts T60 <-> decay rate

inline double midiToHz(double note, double a4 = 440.0) {
    return a4 * std::pow(2.0, (note - 69.0) / 12.0);
}

// amplitude decay rate sigma [1/s] for a given 60 dB decay time [s]
inline double t60ToSigma(double t60) { return kLn1000 / t60; }

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

} // namespace pmp
