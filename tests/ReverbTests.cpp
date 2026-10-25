// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include <Reverb.h>

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;

tw::Reverb::Settings wetOnly (float decay, float damping = 0.0f, float size = 0.5f)
{
    tw::Reverb::Settings s;
    s.mix = 1.0f;
    s.decay = decay;
    s.damping = damping;
    s.size = size;
    s.predelayMs = 0.0f;
    s.width = 1.0f;
    return s;
}

// the impulse response (left + right energy) of `seconds`
std::vector<double> impulseEnergy (const tw::Reverb::Settings& s, double seconds)
{
    tw::Reverb r;
    r.prepare (kRate);
    r.setSettings (s);
    const int n = int (seconds * kRate), block = 256;
    std::vector<double> e (static_cast<size_t> (n));
    std::vector<float> l (block), rr (block);
    for (int done = 0; done < n; done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (rr.begin(), rr.end(), 0.0f);
        if (done == 0)
            l[0] = rr[0] = 1.0f;
        r.process (l.data(), rr.data(), block);
        for (int i = 0; i < block && done + i < n; ++i)
            e[size_t (done + i)] = double (l[size_t (i)]) * l[size_t (i)] + double (rr[size_t (i)]) * rr[size_t (i)];
    }
    return e;
}

// RT60 from the Schroeder integral: twice the time from -5 to -35 dB (T30)
double rt60 (const std::vector<double>& e)
{
    std::vector<double> edc (e.size());
    double sum = 0.0;
    for (size_t i = e.size(); i-- > 0;)
        edc[i] = (sum += e[i]);
    const double total = edc[0];
    double t5 = -1.0, t35 = -1.0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total);
        if (t5 < 0.0 && db <= -5.0)
            t5 = double (i) / kRate;
        if (t35 < 0.0 && db <= -35.0)
        {
            t35 = double (i) / kRate;
            break;
        }
    }
    return 2.0 * (t35 - t5);
}

std::vector<float> noise (int n, uint32_t seed)
{
    std::vector<float> x (static_cast<size_t> (n));
    juce::Random rnd ((juce::int64) seed);
    for (auto& v : x)
        v = rnd.nextFloat() * 2.0f - 1.0f;
    return x;
}
} // namespace

TEST_CASE ("Reverb decays with the set RT60", "[reverb]")
{
    for (const float decay : { 1.0f, 2.0f, 4.0f })
        for (const float size : { 0.2f, 0.5f, 0.9f })
        {
            const double t = rt60 (impulseEnergy (wetOnly (decay, 0.0f, size), 3.0 * decay));
            WARN ("RT60 set " << decay << " s, size " << size << ": measured " << t << " s");
            INFO ("decay " << decay << ", size " << size);
            CHECK (std::abs (t / decay - 1.0) < 0.15);
        }
}

TEST_CASE ("Reverb damping shortens the highs", "[reverb]")
{
    // the energy of the second half second relative to the first, after a first-order high pass: smaller with damping
    auto late = [] (float damping) {
        const auto e = impulseEnergy (wetOnly (2.0f, damping), 1.0);
        double early = 0, lateSum = 0;
        for (size_t i = 0; i < e.size(); ++i)
            (i < e.size() / 2 ? early : lateSum) += e[i];
        return 10.0 * std::log10 (lateSum / early);
    };
    const double flat = late (0.0f), damped = late (1.0f);
    WARN ("late/early energy: damping 0 " << flat << " dB, damping 1 " << damped << " dB");
    CHECK (damped < flat - 1.0);
}

TEST_CASE ("Reverb at mix 0 passes the input unchanged", "[reverb]")
{
    tw::Reverb r;
    auto s = wetOnly (2.0f);
    s.mix = 0.0f;
    r.setSettings (s); // before prepare: no glide from the default mix
    r.prepare (kRate);
    auto l = noise (4096, 1), rr = noise (4096, 2);
    const auto l0 = l, r0 = rr;
    r.process (l.data(), rr.data(), 4096);
    CHECK (l == l0);
    CHECK (rr == r0);
}

TEST_CASE ("Reverb level and stability", "[reverb]")
{
    // wet level against dry for white noise at the defaults
    {
        tw::Reverb r;
        r.prepare (kRate);
        r.setSettings (wetOnly (2.0f, 0.5f));
        const int n = int (4.0 * kRate);
        auto l = noise (n, 3), rr = noise (n, 4);
        double dry = 0.0;
        for (int i = n / 2; i < n; ++i)
            dry += double (l[size_t (i)]) * l[size_t (i)];
        for (int done = 0; done < n; done += 512)
            r.process (l.data() + done, rr.data() + done, std::min (512, n - done));
        double wet = 0.0;
        for (int i = n / 2; i < n; ++i)
            wet += double (l[size_t (i)]) * l[size_t (i)];
        const double db = 10.0 * std::log10 (wet / dry);
        WARN ("wet / dry for white noise: " << db << " dB");
        CHECK (std::abs (db) < 4.0);
    }
    // the longest, largest, undamped setting with the size jumping around: bounded, and silent again after the input stops
    {
        tw::Reverb r;
        r.prepare (kRate);
        auto s = wetOnly (10.0f, 0.0f, 1.0f);
        const int block = 480;
        float peak = 0.0f, tail = 0.0f;
        juce::Random rnd (5);
        for (int b = 0; b < int (40.0 * kRate) / block; ++b)
        {
            const bool input = b < int (5.0 * kRate) / block;
            if (input && b % 10 == 0)
                s.size = rnd.nextFloat();
            r.setSettings (s);
            auto l = input ? noise (block, uint32_t (b)) : std::vector<float> (size_t (block), 0.0f);
            auto rr = l;
            r.process (l.data(), rr.data(), block);
            for (int i = 0; i < block; ++i)
            {
                peak = std::max (peak, std::abs (l[size_t (i)]));
                if (b >= int (39.0 * kRate) / block)
                    tail = std::max (tail, std::abs (l[size_t (i)]));
            }
        }
        WARN ("extreme setting: peak " << peak << ", tail after 34 s " << 20.0 * std::log10 (tail / peak + 1e-30) << " dB");
        CHECK (std::isfinite (peak));
        CHECK (peak < 20.0f);
        CHECK (tail < peak * 1e-6f); // -120 dB (RT60 10 s: about -200 dB expected)
    }
}

// CPU time of 10 s of stereo at 48 kHz (hidden: ./Tests "[.bench]")
TEST_CASE ("Reverb cost", "[.bench]")
{
    tw::Reverb r;
    r.setSettings (wetOnly (2.0f, 0.5f));
    r.prepare (kRate);
    std::vector<float> l (256, 0.1f), rr (256, 0.1f);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int done = 0; done < 480000; done += 256)
        r.process (l.data(), rr.data(), 256);
    WARN ("reverb, 10 s stereo at 48 kHz: " << juce::Time::getMillisecondCounterHiRes() - t0 << " ms");
}
