// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include <Humanize.h>
#include <Parameters.h>
#include <PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace
{
void set (PluginProcessor& p, const char* id, float value)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

bool ready (PluginProcessor& p)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 6000 && !p.instrumentReady(); ++i)
    {
        p.processBlock (buffer, midi);
        juce::Thread::sleep (5);
    }
    return p.instrumentReady();
}

// one strike of C4 at velocity 100 out of silence: the left channel of 0.4 s
std::vector<float> strike (PluginProcessor& p)
{
    juce::AudioBuffer<float> buffer (2, 512);
    for (int b = 0; b < int (1.5 * 48000.0 / 512); ++b) // the previous strike off and gone
    {
        juce::MidiBuffer off;
        if (b == 0)
        {
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            off.addEvent (juce::MidiMessage::allSoundOff (1), 0); // the hybrid voices' slow share and harp ring on for seconds
        }
        p.processBlock (buffer, off);
    }
    std::vector<float> out;
    for (int b = 0; b < int (0.4 * 48000.0 / 512); ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        p.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }
    return out;
}

double rmsDb (const std::vector<float>& x)
{
    double s = 0.0;
    for (const float v : x)
        s += double (v) * v;
    return 10.0 * std::log10 (s / double (x.size()));
}
} // namespace

TEST_CASE ("Humanize scatters the velocity as specified", "[humanize]")
{
    tw::Humanize h (12345);
    CHECK (h.apply (100, 0.0) == 100);
    CHECK (h.apply (0, 1.0) == 0);
    for (const double amount : { 0.5, 1.0 })
    {
        double sum = 0.0, sum2 = 0.0;
        int lo = 127, hi = 1;
        constexpr int n = 20000;
        for (int i = 0; i < n; ++i)
        {
            const int v = h.apply (64, amount);
            const double d = std::log (double (v) / 64.0);
            sum += d, sum2 += d * d;
            lo = std::min (lo, v), hi = std::max (hi, v);
        }
        const double mean = sum / n, sigma = std::sqrt (sum2 / n - mean * mean);
        WARN ("amount " << amount << ": sigma of ln(velocity) " << sigma << " (target " << tw::Humanize::kSigmaAtFull * amount
                        << "), mean " << mean << ", range " << lo << ".." << hi);
        CHECK (std::abs (mean) < 0.005);
        CHECK (std::abs (sigma / (tw::Humanize::kSigmaAtFull * amount) - 1.0) < 0.15);
    }
    for (int i = 0; i < 1000; ++i)
    {
        const int v = h.apply (127, 1.0);
        CHECK ((v >= 1 && v <= 127));
    }
}

TEST_CASE ("Humanize makes repeated strikes differ, and off they are identical", "[humanize][audio]")
{
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    plugin.seedHumanize (7);
    REQUIRE (ready (plugin));

    set (plugin, tw::ids::humanize, 100.0f);
    std::vector<double> levels;
    for (int i = 0; i < 12; ++i)
        levels.push_back (rmsDb (strike (plugin)));
    double mean = 0.0, var = 0.0;
    for (const double l : levels)
        mean += l / double (levels.size());
    for (const double l : levels)
        var += (l - mean) * (l - mean) / double (levels.size());
    WARN ("humanize 100 %: level of 12 strikes scatters by " << std::sqrt (var) << " dB (" << plugin.instrumentStatus() << ")");
    CHECK (std::sqrt (var) > 0.1);
    CHECK (std::sqrt (var) < 2.0);

}

TEST_CASE ("Without humanize the hybrid voices repeat a strike bit for bit", "[humanize][audio]")
{
    // at the bank's rate (the Salamander bank: 44.1 kHz): through a resampler a strike lands at another fraction of a bank
    // sample each time. The physical models keep their felt scatter (human.contact).
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (44100.0, 512);
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (tw::instrumentIndex ("salamander") >= 0);
    set (plugin, tw::ids::instrument, float (tw::instrumentIndex ("salamander")));
    REQUIRE (ready (plugin));
    set (plugin, tw::ids::humanize, 0.0f);
    strike (plugin);
    CHECK (strike (plugin) == strike (plugin));
}

