// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// dsp.h - small DSP building blocks of the output stage
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "config.h"

namespace pmp {

// A damped mode driven by a force F: q'' + 2 sigma q' + w0^2 q = F, output 2 sigma q' (unit
// gain at resonance). Exact zero-order-hold discretisation in delta form like the tine modes
// (key_voice.h), z += (P - 1) z + b F with z = (q' + sigma q) + j omega_d q: a direct form
// biquad loses its low, long-ringing poles in float (1 - cos w ~ 1e-5).
class ForcedMode {
public:
    void setup(double f0, double t60, double fs) {
        const double w0 = 2.0 * kPi * f0, sig = t60ToSigma(t60), h = 1.0 / fs;
        const double wd = std::sqrt(std::max(w0 * w0 - sig * sig, 1e-4 * w0 * w0));
        const double a = -sig * h, b = wd * h, sh = std::sin(0.5 * b);
        const double pr = std::expm1(a) * std::cos(b) - 2.0 * sh * sh, pi = std::exp(a) * std::sin(b);
        const double g = 1.0 / (sig * sig + wd * wd);
        pr_ = real(pr);
        pi_ = real(pi);
        br_ = real((wd * pi - sig * pr) * g);
        bi_ = real(-(sig * pi + wd * pr) * g);
        c_ = real(sig / wd);
        out_ = real(2.0 * sig);
    }
    void reset() { zr_ = zi_ = 0; }
    real process(real f) {
        const real dr = zr_ * pr_ - zi_ * pi_ + br_ * f;
        const real di = zr_ * pi_ + zi_ * pr_ + bi_ * f;
        zr_ += dr;
        zi_ += di;
        return out_ * (zr_ - c_ * zi_);          // 2 sigma dq/dt
    }

private:
    real pr_ = 0, pi_ = 0, br_ = 0, bi_ = 0, c_ = 0, out_ = 0, zr_ = 0, zi_ = 0;
};

// 2nd order sections (RBJ cookbook), transposed direct form II
class Biquad {
public:
    void lowpass(double fc, double q, double fs) {
        const double w = omega(fc, fs), c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        set((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void highpass(double fc, double q, double fs) {
        const double w = omega(fc, fs), c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        set((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }
    void lowShelf(double fc, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0), w = omega(fc, fs), c = std::cos(w);
        const double beta = std::sin(w) * std::sqrt(A) / 0.7071067811865476;  // slope 1
        set(A * ((A + 1) - (A - 1) * c + beta), 2 * A * ((A - 1) - (A + 1) * c),
            A * ((A + 1) - (A - 1) * c - beta), (A + 1) + (A - 1) * c + beta,
            -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - beta);
    }
    void highShelf(double fc, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0), w = omega(fc, fs), c = std::cos(w);
        const double beta = std::sin(w) * std::sqrt(A) / 0.7071067811865476;
        set(A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c),
            A * ((A + 1) + (A - 1) * c - beta), (A + 1) - (A - 1) * c + beta,
            2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
    }
    void peaking(double fc, double q, double gainDb, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0), w = omega(fc, fs), c = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * q);
        set(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
            1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }
    void bypass() { set(1, 0, 0, 1, 0, 0); }
    void reset() { s1_ = s2_ = 0; }
    real process1(real in) {
        const real y = b0_ * in + s1_;
        s1_ = b1_ * in - a1_ * y + s2_;
        s2_ = b2_ * in - a2_ * y;
        return y;
    }
    void process(float* x, int n) {
        for (int i = 0; i < n; ++i) x[i] = float(process1(x[i]));
    }

private:
    static double omega(double fc, double fs) { return 2.0 * kPi * std::min(fc, 0.45 * fs) / fs; }
    void set(double b0, double b1, double b2, double a0, double a1, double a2) {
        b0_ = real(b0 / a0); b1_ = real(b1 / a0); b2_ = real(b2 / a0);
        a1_ = real(a1 / a0); a2_ = real(a2 / a0);
    }
    real b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, s1_ = 0, s2_ = 0;
};

// 1st order high pass (coupling capacitor / DC blocker)
class HighPass1 {
public:
    void setup(double fc, double fs) { r_ = real(std::exp(-2.0 * kPi * fc / fs)); }
    void reset() { x1_ = y1_ = 0; }
    float process(float x) {
        const real y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return float(y);
    }

private:
    real r_ = real(0.999), x1_ = 0, y1_ = 0;
};

// Linear-phase half-band FIR (Kaiser window) for decimation by 2
class HalfbandDecimator {
public:
    // Transition band symmetric around fs_in/4. With 135 taps: passband up to 0.2268 fs_in
    // (20 kHz at 88.2 kHz) with < 0.001 dB ripple, stopband from 0.2732 fs_in below -90 dB.
    void design(int taps = 135, double attenuationDb = 90.0) {
        if (taps % 4 != 3) taps += (3 - taps % 4 + 4) % 4;  // 4k+3: outermost taps non-zero
        h_.assign(taps, 0.0);
        const int c = taps / 2;
        const double beta = 0.1102 * (attenuationDb - 8.7);
        auto i0 = [](double x) {
            double s = 1.0, t = 1.0;
            for (int k = 1; k < 50; ++k) {
                t *= (x / (2.0 * k)) * (x / (2.0 * k));
                s += t;
            }
            return s;
        };
        std::vector<double> h(size_t(taps), 0.0);
        double sum = 0.0;
        for (int k = 0; k < taps; ++k) {
            const int d = k - c;
            double sinc = 0.5;
            if (d != 0) sinc = std::sin(0.5 * kPi * d) / (kPi * d);
            const double r = double(d) / c;
            const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
            h[size_t(k)] = sinc * w;
            sum += h[size_t(k)];
        }
        for (int k = 0; k < taps; ++k) h_[size_t(k)] = real(h[size_t(k)] / sum);
        hist_.assign(2 * taps, 0.0f);
        pos_ = 0;
    }
    void reset() {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        pos_ = 0;
    }
    // in: 2*n samples, out: n samples
    void process(const float* in, float* out, int n) {
        const int N = int(h_.size());
        const int c = N / 2;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < 2; ++j) {       // history kept twice for contiguous reads
                pos_ = (pos_ + 1) % N;
                hist_[pos_] = hist_[pos_ + N] = in[2 * i + j];
            }
            const float* x = &hist_[pos_ + 1];  // x[0] = oldest ... x[N-1] = newest
            real acc = h_[c] * x[c];
            for (int k = 1; k <= c; k += 2) acc += h_[c - k] * (real(x[c - k]) + real(x[c + k]));
            out[i] = float(acc);
        }
    }
    const std::vector<real>& taps() const { return h_; }

private:
    std::vector<real> h_;
    std::vector<float> hist_;
    int pos_ = 0;
};

// A phase kept as a 64-bit fraction of a turn wraps by integer overflow and is exact in every
// build. As real in [0, 1): its top 24 (float) or 53 (double) bits, converted exactly.
inline real turns(uint64_t phase) {
    constexpr int kBits = std::numeric_limits<real>::digits;
    using Int = std::conditional_t<(kBits < 32), int32_t, int64_t>;
    return real(Int(phase >> (64 - kBits))) * (real(1) / real(uint64_t(1) << kBits));
}

// Micro pitch shifter pair on one delay line: output A is shifted up, B down by the same
// number of cents. Each output reads two taps whose delay ramps linearly (Doppler) and
// crossfades them with complementary windows, so the delay jumps happen at zero gain.
//
// The ramp phase is a 64-bit fixed-point fraction that wraps by integer overflow. Its
// increment is tiny (3e-6 per sample for 7 cents, 30 ms window, 44.1 kHz): a float
// accumulator near 1 would round every step by up to 1 % (7 cents) or 5 % (1 cent) and stop
// below 0.07 cent. The fixed-point phase is exact in every build and costs one 64-bit
// addition (two instructions) on a 32-bit core.
class StereoDetune {
public:
    static constexpr int kSize = 2048;   // 46 ms at 44.1 kHz, fixed: no heap (embedded)

    void setup(double cents, double windowMs, double fs) {
        win_ = real(std::min(std::max(windowMs * 1e-3 * fs, 64.0), double(kSize - 8)));
        const double inc = std::fabs(std::pow(2.0, cents / 1200.0) - 1.0) / double(win_);
        inc_ = uint64_t(std::llround(std::min(inc, 0.25) * 0x1p64));
        reset();
    }
    void reset() {
        std::fill(buf_, buf_ + kSize, real(0));
        pos_ = 0;
        phase_ = 0;
    }
    void process(real x, real& up, real& down) {
        pos_ = (pos_ + 1) & (kSize - 1);
        buf_[pos_] = x;
        phase_ += inc_;
        const real p1 = turns(phase_), p2 = turns(phase_ + kHalf);
        // up: delay shrinks (read faster than write), down: delay grows
        up   = tap(win_ * (real(1) - p1)) * window(p1) + tap(win_ * (real(1) - p2)) * window(p2);
        down = tap(win_ * p1) * window(p1) + tap(win_ * p2) * window(p2);
    }
    double phase() const { return double(phase_ >> 11) * 0x1p-53; }   // [0, 1), for tests

private:
    static constexpr uint64_t kHalf = uint64_t(1) << 63;

    // smoothstep crossfade: w(p) + w(p + 1/2) = 1 like sin^2, but without a libm call
    static real window(real p) {
        const real t = p < real(0.5) ? real(2) * p : real(2) - real(2) * p;
        return t * t * (real(3) - real(2) * t);
    }
    real tap(real delay) const {         // linear interpolation, delay >= 2 samples
        const real d = delay + real(2);
        const int i = int(d);
        const real f = d - real(i);
        const real a = buf_[(pos_ - i) & (kSize - 1)], b = buf_[(pos_ - i - 1) & (kSize - 1)];
        return a + f * (b - a);
    }
    real buf_[kSize] = {};
    int  pos_ = 0;
    uint64_t phase_ = 0, inc_ = 0;       // fraction of the ramp cycle, 2^64 = 1
    real win_ = 1000;
};

// power amp: smooth saturation with optional asymmetry (even harmonics), unity small-signal gain
class Saturator {
public:
    void setup(real drive, real asym) {   // drive > 0 (linear gain into the tanh)
        drive_ = drive;
        asym_ = asym;
        tb_ = std::tanh(drive * asym);
        norm_ = real(1) / (drive * (real(1) - tb_ * tb_));
    }
    real process(real x) const { return (std::tanh(drive_ * (x + asym_)) - tb_) * norm_; }

private:
    real drive_ = 1, asym_ = 0, tb_ = 0, norm_ = 1;
};

// transparent below 0.9 of full scale, smooth saturation to +-1 above
inline float softClip(float x) {
    const float t = 0.9f;
    const float a = std::fabs(x);
    if (a <= t) return x;
    const float y = t + (1.0f - t) * std::tanh((a - t) / (1.0f - t));
    return x < 0.0f ? -y : y;
}

} // namespace pmp
