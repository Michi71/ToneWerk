// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include <Resampler.h>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Fit
{
    double gainDb, snrDb, delaySamples;
};

// least-squares fit of a sine of frequency f (cycles per output sample) to y[skip..]: gain against amplitude 1, the residual
// against the fit, and the delay of the fit against the ideal output sin(2 pi f k)
Fit fitSine (const std::vector<float>& y, double f, size_t skip)
{
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (size_t k = skip; k < y.size(); ++k)
    {
        const double s = std::sin (2 * kPi * f * double (k)), c = std::cos (2 * kPi * f * double (k));
        ss += s * s, sc += s * c, cc += c * c, ys += y[k] * s, yc += y[k] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double err = 0, sig = 0;
    for (size_t k = skip; k < y.size(); ++k)
    {
        const double fit = a * std::sin (2 * kPi * f * double (k)) + b * std::cos (2 * kPi * f * double (k));
        err += (y[k] - fit) * (y[k] - fit);
        sig += fit * fit;
    }
    const double amp = std::hypot (a, b);
    const double phase = std::atan2 (b, a); // y ~ amp sin(2 pi f k + phase): a delay d gives phase = -2 pi f d
    return { 20 * std::log10 (amp), 10 * std::log10 (sig / std::max (err, 1e-30)), -phase / (2 * kPi * f) };
}

// a sine of fHz at inRate through tw::Resampler to outRate, in blocks of varying size
std::vector<float> throughShort (double fHz, double inRate, double outRate, int frames)
{
    tw::Resampler r;
    r.prepare (inRate / outRate, 512);
    std::vector<float> outL (static_cast<size_t> (frames)), outR (static_cast<size_t> (frames));
    long written = 0;
    int done = 0, step = 0;
    while (done < frames)
    {
        const int n = std::min (frames - done, 1 + (step++ * 97) % 512);
        const int m = r.inputNeeded (n);
        float* l = r.writeLeft();
        float* rr = r.writeRight();
        for (int i = 0; i < m; ++i)
            l[i] = rr[i] = float (std::sin (2 * kPi * fHz / inRate * double (written + i)));
        written += m;
        r.appended (m);
        r.process (outL.data() + done, outR.data() + done, n);
        done += n;
    }
    return outL;
}

// the same through JUCE's WindowedSinc (the previous resampler), aligned by its base latency
std::vector<float> throughJuce (double fHz, double inRate, double outRate, int frames)
{
    juce::WindowedSincInterpolator interp;
    const double ratio = inRate / outRate;
    std::vector<float> in (size_t (frames * ratio) + 512);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = float (std::sin (2 * kPi * fHz / inRate * double (i)));
    std::vector<float> out (static_cast<size_t> (frames));
    interp.process (ratio, in.data(), out.data(), frames);
    return out;
}
} // namespace

TEST_CASE ("Short resampler: flat passband, clean stopband, no delay", "[resampler]")
{
    struct Case
    {
        double in, out;
    };
    for (const auto [inRate, outRate] : { Case { 44100, 48000 }, Case { 48000, 44100 }, Case { 44100, 96000 }, Case { 44100, 88200 } })
    {
        for (const double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0 })
        {
            const auto y = throughShort (f, inRate, outRate, 32768);
            const auto fit = fitSine (y, f / outRate, 256);
            const auto ref = fitSine (throughJuce (f, inRate, outRate, 32768), f / outRate, 512);
            WARN (inRate << " -> " << outRate << "  " << f << " Hz: gain " << fit.gainDb << " dB, SNR " << fit.snrDb
                          << " dB, delay " << fit.delaySamples << " | JUCE: gain " << ref.gainDb << " dB, SNR " << ref.snrDb
                          << " dB, delay " << ref.delaySamples);
            INFO (inRate << " -> " << outRate << ", " << f << " Hz");
            CHECK (std::abs (fit.delaySamples) < 0.01);
            if (f <= 15000.0)
            {
                CHECK (std::abs (fit.gainDb) < 0.1);
                CHECK (fit.snrDb > 65.0);
            }
            else
            {
                CHECK (fit.gainDb > -1.5); // 18 kHz: the edge of the transition band
                CHECK (fit.snrDb > 60.0);
            }
        }
    }
}

// CPU time of 10 s of stereo 44.1 -> 48 kHz (hidden: ./Tests "[.bench]")
TEST_CASE ("Resampler cost", "[.bench]")
{
    constexpr int frames = 480000, block = 256;
    std::vector<float> l (block), r (block);
    tw::Resampler shortSinc;
    shortSinc.prepare (44100.0 / 48000.0, block);
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int done = 0; done < frames; done += block)
    {
        const int m = shortSinc.inputNeeded (block);
        std::fill_n (shortSinc.writeLeft(), m, 0.1f);
        std::fill_n (shortSinc.writeRight(), m, 0.1f);
        shortSinc.appended (m);
        shortSinc.process (l.data(), r.data(), block);
    }
    const auto tShort = juce::Time::getMillisecondCounterHiRes() - t0;
    juce::WindowedSincInterpolator il, ir;
    std::vector<float> in (block * 2, 0.1f);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int done = 0; done < frames; done += block)
    {
        il.process (44100.0 / 48000.0, in.data(), l.data(), block);
        ir.process (44100.0 / 48000.0, in.data(), r.data(), block);
    }
    const auto tJuce = juce::Time::getMillisecondCounterHiRes() - t0;
    WARN ("10 s stereo: short " << tShort << " ms, JUCE WindowedSinc " << tJuce << " ms");
}
