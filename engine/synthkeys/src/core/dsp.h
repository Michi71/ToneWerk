// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// dsp.h - small DSP building blocks behind the voices (the velocity low pass, the output
// high pass, the bank-rate resampler)
//
// Slimmer than pmtines/pmreeds: a sample player runs at the host rate directly, so the
// oversampling machinery (ForcedMode, HalfbandDecimator) of the physical models is not
// needed. The resampler carries the hybrid voice from the bank's rate to the host's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "config.h"

namespace pmp {

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

// 1st order high pass (DC blocker at the output)
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

// Arbitrary-ratio streaming resampler (windowed-sinc polyphase), for the hybrid voice: its
// bank has a fixed rate (22050 Hz), the host may run at any rate. The kernel is a Kaiser-
// windowed sinc, cut off below the lower Nyquist of the two rates (anti-aliasing both
// ways), evaluated into a phase table at setup: nearest-phase lookup per output sample -
// deterministic, so the engine stays bit-exact and block-size independent. The output count
// of a call follows from the totals (llround(in * ratio)), never from the chunk the host
// asked for. Latency: taps/2 input samples (compensated by starting the stream that far
// "in the past" - the first outputs see zeros there, a switch starts from silence anyway).
class Resampler {
public:
    // fsIn == fsOut is not bypassed here (the engine skips the resampler instead when the
    // rates match); this class always filters, deterministically, in one code path
    void setup(double fsIn, double fsOut) {
        ratio_ = fsOut / fsIn;
        inTotal_ = outTotal_ = 0;
        t_ = 0;                               // the first output reads the input around 0
        const double attenDb = 96.0, beta = 0.1102 * (attenDb - 8.7);
        const double cutoff = 0.90 * (ratio_ >= 1.0 ? 1.0 : ratio_);   // of the input Nyquist
        auto i0 = [](double x) {
            double s = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                s += term;
            }
            return s;
        };
        const double i0b = i0(beta);
        h_.assign(size_t(kPhases) * kTaps, 0.0);
        for (int p = 0; p < kPhases; ++p) {
            const double f = double(p) / kPhases;   // the output falls f into the input sample
            for (int q = 0; q < kTaps; ++q) {
                const double x = (kTaps / 2 - 1 + f) - q;   // h(t - k): t = i + f
                double sinc = cutoff;
                if (std::fabs(x) > 1e-12) sinc = std::sin(kPi * cutoff * x) / (kPi * x);
                const double r = x / double(kTaps / 2);
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                h_[size_t(p * kTaps + q)] = real(sinc * w);
            }
        }
        hist_.assign(size_t(kTaps - 1), 0.0f);
    }
    void reset() {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        inTotal_ = outTotal_ = 0;
        t_ = 0;
    }
    // n input samples -> out receives what they yield; returns that count
    int process(const float* in, int n, float* out) {
        work_.resize(size_t(kTaps - 1 + n));
        std::copy(hist_.begin(), hist_.end(), work_.begin());
        std::copy(in, in + n, work_.begin() + (kTaps - 1));
        const long long base = inTotal_ - (kTaps - 1);            // stream position of work_[0]
        const long long end = inTotal_ + n;
        int m = 0;
        for (;;) {
            const long long i0 = static_cast<long long>(std::floor(t_)) - kTaps / 2 + 1;
            if (i0 + kTaps > end) break;                          // this output needs input to come
            const int phase = int((t_ - std::floor(t_)) * kPhases + 0.5) % kPhases;
            real acc = 0;
            for (int q = 0; q < kTaps; ++q) {
                const long long at = i0 + q;
                const real x = at < base ? real(0) : real(work_[size_t(at - base)]);
                acc += h_[size_t(phase * kTaps + q)] * x;
            }
            out[m++] = float(acc);
            ++outTotal_;
            t_ += 1.0 / ratio_;
        }
        std::copy(work_.end() - (kTaps - 1), work_.end(), hist_.begin());
        inTotal_ += n;
        return m;
    }
    long long produced() const { return outTotal_; }

private:
    static constexpr int kTaps = 48, kPhases = 512;
    double ratio_ = 1, t_ = 0;
    long long inTotal_ = 0, outTotal_ = 0;
    std::vector<real> h_;
    std::vector<float> hist_;
    std::vector<float> work_;
};

} // namespace pmp
