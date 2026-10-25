// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tw
{
namespace
{
    // modified Bessel function of the first kind, order 0 (Kaiser window)
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }
} // namespace

void Resampler::prepare (double ratio, int maxOut)
{
    ratio_ = ratio;
    // downsampling: the cutoff follows the output's Nyquist frequency, the kernel widens to keep the transition band
    const double stretch = std::max (1.0, ratio);
    const double cutoff = kCutoff / stretch; // relative to the input's Nyquist frequency
    half_ = int (std::ceil (kHalfTaps * stretch));
    const int points = half_ * kPhases + 2;
    table_.assign (size_t (points), 0.0f);
    const double norm = besselI0 (kBeta);
    for (int i = 0; i < points; ++i)
    {
        const double t = double (i) / kPhases;
        if (t >= half_)
            break;
        const double x = 3.14159265358979323846 * cutoff * t;
        const double sinc = i == 0 ? 1.0 : std::sin (x) / x;
        const double w = t / half_;
        table_[size_t (i)] = float (cutoff * sinc * besselI0 (kBeta * std::sqrt (1.0 - w * w)) / norm);
    }
    tableScale_ = kPhases;
    const int capacity = int (std::ceil ((maxOut + 1) * ratio)) + 2 * half_ + 8;
    left_.assign (size_t (capacity), 0.0f);
    right_.assign (size_t (capacity), 0.0f);
    reset();
}

void Resampler::reset()
{
    // half_ - 1 samples of silent history before the first input sample, which is the first output instant
    std::fill (left_.begin(), left_.end(), 0.0f);
    std::fill (right_.begin(), right_.end(), 0.0f);
    count_ = half_ - 1;
    pos_ = double (half_ - 1);
}

int Resampler::inputNeeded (int n) const
{
    // the last output instant reads up to floor(pos) + half_
    const double last = pos_ + double (n - 1) * ratio_;
    const int need = int (std::floor (last)) + half_ + 1;
    return std::max (0, need - count_);
}

float Resampler::tap (double t) const
{
    const double a = std::abs (t) * tableScale_;
    const int i = int (a);
    const float f = float (a - double (i));
    return table_[size_t (i)] + f * (table_[size_t (i) + 1] - table_[size_t (i)]);
}

void Resampler::process (float* left, float* right, int n)
{
    for (int k = 0; k < n; ++k)
    {
        const int i = int (pos_);
        const double frac = pos_ - double (i);
        float l = 0.0f, r = 0.0f;
        // taps j = i - half_ + 1 .. i + half_, at distance (j - pos) from the output instant
        for (int j = i - half_ + 1; j <= i + half_; ++j)
        {
            const float h = tap (double (j - i) - frac);
            l += h * left_[size_t (j)];
            r += h * right_[size_t (j)];
        }
        left[k] = l;
        right[k] = r;
        pos_ += ratio_;
    }
    // drop what no later output reads: keep half_ - 1 samples before floor(pos)
    const int drop = int (pos_) - (half_ - 1);
    if (drop > 0)
    {
        const int rest = count_ - drop;
        std::memmove (left_.data(), left_.data() + drop, size_t (rest) * sizeof (float));
        std::memmove (right_.data(), right_.data() + drop, size_t (rest) * sizeof (float));
        count_ = rest;
        pos_ -= drop;
    }
}
} // namespace tw
