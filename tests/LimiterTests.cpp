// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include <Limiter.h>

#include <catch2/catch_test_macros.hpp>
#include <core/dsp.h>

#include <cmath>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

// signal-to-residual [dB] of the last half second against the best-fitting 1 kHz sine
double sineSnr (const std::vector<float>& y, double f)
{
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    const size_t from = y.size() / 2;
    for (size_t k = from; k < y.size(); ++k)
    {
        const double s = std::sin (2 * kPi * f * double (k)), c = std::cos (2 * kPi * f * double (k));
        ss += s * s, sc += s * c, cc += c * c, ys += y[k] * s, yc += y[k] * c;
    }
    const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double err = 0, sig = 0;
    for (size_t k = from; k < y.size(); ++k)
    {
        const double fit = a * std::sin (2 * kPi * f * double (k)) + b * std::cos (2 * kPi * f * double (k));
        err += (y[k] - fit) * (y[k] - fit), sig += fit * fit;
    }
    return 10 * std::log10 (sig / err);
}
} // namespace

TEST_CASE ("The limiter keeps a loud sine clean and passes a quiet one bit for bit", "[limiter]")
{
    const double f = 1000.0 / 48000.0;
    for (const float amp : { 0.5f, 2.0f })
    {
        std::vector<float> in (48000);
        for (size_t k = 0; k < in.size(); ++k)
            in[k] = amp * float (std::sin (2 * kPi * f * double (k)));
        auto clipOnly = in, limited = in;
        tw::Limiter lim;
        lim.prepare (48000.0);
        for (size_t k = 0; k < in.size(); k += 256)
            lim.process (limited.data() + k, nullptr, int (std::min<size_t> (256, in.size() - k)));
        for (auto* v : { &clipOnly, &limited })
            for (auto& x : *v)
                x = pmp::softClip (x);
        const double snrClip = sineSnr (clipOnly, f), snrLim = sineSnr (limited, f);
        WARN ("sine amplitude " << amp << ": soft clip alone " << snrClip << " dB, limiter + soft clip " << snrLim << " dB");
        if (amp < 0.85f)
            CHECK (limited == clipOnly); // below the threshold: untouched
        else
            CHECK (snrLim > snrClip + 30.0);
    }
}
