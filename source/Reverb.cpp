// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "Reverb.h"

#include <algorithm>
#include <cmath>

namespace tw
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    // line lengths at size 0.5 [ms]: spread over a factor 2.4, no common divisors in samples worth mentioning
    constexpr double kLineMs[Reverb::kLines] = { 31.3, 37.9, 43.1, 47.3, 53.9, 61.1, 67.3, 73.7 };
    constexpr double kLfoHz[Reverb::kLines] = { 0.13, 0.29, 0.41, 0.53, 0.67, 0.79, 0.89, 0.97 };
    constexpr double kDiffMs[4] = { 4.77, 3.59, 12.73, 9.31 };
    constexpr float kDiffG[4] = { 0.70f, 0.70f, 0.625f, 0.625f };
    // early reflections: time [ms] at size 0.5, gain; even taps left, odd taps right
    constexpr double kErMs[8] = { 7.1, 11.3, 16.7, 19.9, 27.1, 31.7, 41.3, 47.9 };
    constexpr float kErGain[8] = { 0.85f, 0.75f, 0.65f, 0.6f, 0.5f, 0.45f, 0.35f, 0.3f };
    constexpr float kErLevel = 0.35f;
    // input and output sign patterns (rows of an 8x8 Hadamard matrix): decorrelated left and right
    constexpr float kInL[Reverb::kLines] = { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float kInR[Reverb::kLines] = { 1, 1, -1, -1, 1, 1, -1, -1 };
    constexpr float kOutL[Reverb::kLines] = { 1, 1, 1, 1, -1, -1, -1, -1 };
    constexpr float kOutR[Reverb::kLines] = { 1, -1, -1, 1, 1, -1, -1, 1 };
    constexpr float kOutGain = 0.9f; // wet level: white noise in, wet RMS about the dry RMS at 2 s, damping 0.5 (tests)
    constexpr double kMaxScale = 1.55, kModMs = 0.12;

    double sizeScale (float size) { return 0.45 + 1.1 * double (std::clamp (size, 0.0f, 1.0f)); }

    // one step of an exponential glide that lands exactly on the target (mix 0 is then bit-transparent)
    inline void glideTo (float& v, float target, float coeff)
    {
        v += coeff * (target - v);
        if (std::abs (target - v) < 1e-6f)
            v = target;
    }

    // in-place fast Walsh-Hadamard transform of 8 values, orthonormal
    inline void hadamard8 (float* v)
    {
        for (int h = 1; h < 8; h *= 2)
            for (int i = 0; i < 8; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
        constexpr float norm = 0.35355339059327373f; // 1 / sqrt(8)
        for (int i = 0; i < 8; ++i)
            v[i] *= norm;
    }
} // namespace

void Reverb::Line::init (int minLength)
{
    uint32_t size = 1;
    while (size < uint32_t (minLength) + 4)
        size <<= 1;
    buf.assign (size, 0.0f);
    mask = size - 1;
    pos = 0;
}

void Reverb::Line::clear()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    pos = 0;
}

float Reverb::Line::read (float delay) const
{
    const int i = int (delay);
    const float f = delay - float (i);
    const float a = tap (i), b = tap (i + 1);
    return a + f * (b - a);
}

float Reverb::Allpass::process (float x)
{
    const float d = line.tap (delay - 1);
    const float v = x + g * d;
    line.write (v);
    return d - g * v;
}

void Reverb::prepare (double sampleRate)
{
    fs_ = sampleRate;
    const auto ms = [this] (double m) { return int (std::ceil (m * 1e-3 * fs_)); };
    preL_.init (ms (kMaxPredelayMs + kErMs[7] * kMaxScale) + 2);
    preR_.init (ms (kMaxPredelayMs + kErMs[7] * kMaxScale) + 2);
    for (int k = 0; k < 4; ++k)
    {
        diffL_[size_t (k)].line.init (ms (kDiffMs[k] * kMaxScale) + 2);
        diffR_[size_t (k)].line.init (ms (kDiffMs[k] * kMaxScale) + 2);
        diffL_[size_t (k)].g = diffR_[size_t (k)].g = kDiffG[k];
    }
    for (int i = 0; i < kLines; ++i)
    {
        lines_[size_t (i)].init (ms (kLineMs[i] * kMaxScale + 2.0 * kModMs) + 4);
        lfoInc_[size_t (i)] = kLfoHz[i] / fs_;
    }
    modDepth_ = float (kModMs * 1e-3 * fs_);
    glide_ = float (1.0 - std::exp (-1.0 / (0.08 * fs_))); // lengths and pre-delay: 80 ms
    smooth_ = float (1.0 - std::exp (-1.0 / (0.02 * fs_))); // mix and width: 20 ms
    dirty_ = true;
    updateTargets();
    length_ = lengthTarget_;
    predelay_ = predelayTarget_;
    dry_ = dryTarget_;
    wet_ = wetTarget_;
    width_ = widthTarget_;
    reset();
}

void Reverb::reset()
{
    preL_.clear();
    preR_.clear();
    for (auto& a : diffL_)
        a.line.clear();
    for (auto& a : diffR_)
        a.line.clear();
    for (auto& l : lines_)
        l.clear();
    state_.fill (0.0f);
    apState_.fill (0.0f);
    for (int i = 0; i < kLines; ++i)
        lfoPhase_[size_t (i)] = double (i) / kLines;
}

void Reverb::setSettings (const Settings& s)
{
    const auto differs = [] (float a, float b) { return std::abs (a - b) > 0.0f; };
    if (differs (s.mix, set_.mix) || differs (s.size, set_.size) || differs (s.decay, set_.decay)
        || differs (s.damping, set_.damping) || differs (s.predelayMs, set_.predelayMs) || differs (s.width, set_.width))
    {
        set_ = s;
        dirty_ = true;
    }
}

void Reverb::updateTargets()
{
    if (!dirty_)
        return;
    dirty_ = false;
    const double scale = sizeScale (set_.size);
    const double t60 = std::max (0.1, double (set_.decay));
    const double t60High = t60 * (1.0 - 0.85 * double (std::clamp (set_.damping, 0.0f, 1.0f)));
    for (int i = 0; i < kLines; ++i)
    {
        const double len = kLineMs[i] * scale * 1e-3 * fs_;
        lengthTarget_[size_t (i)] = float (len);
        // the gain per pass that gives the RT60: -60 dB over t60 seconds, at DC and at Nyquist
        const double gDc = std::pow (10.0, -3.0 * len / (fs_ * t60));
        const double gNy = std::pow (10.0, -3.0 * len / (fs_ * t60High));
        const double a1 = (gDc - gNy) / (gDc + gNy);
        a1_[size_t (i)] = float (a1);
        b0_[size_t (i)] = float (gDc * (1.0 - a1));
    }
    for (int k = 0; k < 4; ++k)
    {
        const int d = std::max (1, int (std::lround (kDiffMs[k] * std::sqrt (scale) * 1e-3 * fs_)));
        diffL_[size_t (k)].delay = d;
        diffR_[size_t (k)].delay = d + 7 + k; // a few samples apart: decorrelated channels
    }
    for (int k = 0; k < 8; ++k)
        erDelay_[size_t (k)] = float (kErMs[k] * scale * 1e-3 * fs_);
    predelayTarget_ = float (std::clamp (double (set_.predelayMs), 0.0, double (kMaxPredelayMs)) * 1e-3 * fs_);
    const double m = std::clamp (double (set_.mix), 0.0, 1.0) * 0.5 * 3.141592653589793;
    dryTarget_ = float (std::cos (m));
    wetTarget_ = float (std::sin (m));
    widthTarget_ = std::clamp (set_.width, 0.0f, 1.0f);
}

void Reverb::process (float* left, float* right, int n)
{
    updateTargets();
    std::array<float, kLines> y {};
    for (int s = 0; s < n; ++s)
    {
        glideTo (dry_, dryTarget_, smooth_);
        glideTo (wet_, wetTarget_, smooth_);
        glideTo (width_, widthTarget_, smooth_);
        predelay_ += glide_ * (predelayTarget_ - predelay_);

        const float inL = left[s], inR = right[s];
        preL_.write (inL);
        preR_.write (inR);
        const float pd = std::max (0.0f, predelay_);
        const float xL0 = preL_.read (pd), xR0 = preR_.read (pd); // read after write: delay 0 is this sample

        float erL = 0.0f, erR = 0.0f;
        for (int k = 0; k < 8; k += 2)
        {
            erL += kErGain[k] * preL_.read (pd + erDelay_[size_t (k)]);
            erR += kErGain[k + 1] * preR_.read (pd + erDelay_[size_t (k + 1)]);
        }

        float xL = xL0, xR = xR0;
        for (int k = 0; k < 4; ++k)
        {
            xL = diffL_[size_t (k)].process (xL);
            xR = diffR_[size_t (k)].process (xR);
        }

        float fdnL = 0.0f, fdnR = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& len = length_[size_t (i)];
            len += glide_ * (lengthTarget_[size_t (i)] - len);
            auto& ph = lfoPhase_[size_t (i)];
            ph += lfoInc_[size_t (i)];
            if (ph >= 1.0)
                ph -= 1.0;
            const float mod = modDepth_ * float (std::sin (kTwoPi * ph));
            // read before write: tap 0 is one sample old. The fractional part goes through a first-order allpass, not a linear
            // interpolation: that one is a low pass of up to |1 - 2 frac| at Nyquist per pass and shortened the RT60 by 20 %
            const float d = std::max (0.0f, len - 1.0f + mod);
            const int di = int (d);
            const float frac = d - float (di);
            const float ap = (1.0f - frac) / (1.0f + frac);
            auto& yPrev = apState_[size_t (i)];
            const float raw = ap * (lines_[size_t (i)].tap (di) - yPrev) + lines_[size_t (i)].tap (di + 1);
            yPrev = raw;
            auto& st = state_[size_t (i)];
            st = b0_[size_t (i)] * raw + a1_[size_t (i)] * st;
            y[size_t (i)] = st;
            fdnL += kOutL[i] * st;
            fdnR += kOutR[i] * st;
        }
        hadamard8 (y.data());
        for (int i = 0; i < kLines; ++i)
            lines_[size_t (i)].write (y[size_t (i)] + 0.5f * (kInL[i] * xL + kInR[i] * xR));

        constexpr float outNorm = kOutGain * 0.35355339059327373f;
        float wl = outNorm * fdnL + kErLevel * erL;
        float wr = outNorm * fdnR + kErLevel * erR;
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width_;
        wl = mid + side;
        wr = mid - side;
        left[s] = dry_ * inL + wet_ * wl;
        right[s] = dry_ * inR + wet_ * wr;
    }
}
} // namespace tw
