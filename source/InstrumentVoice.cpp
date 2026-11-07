// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "InstrumentVoice.h"

#include "Parameters.h"

#include <cmath>

namespace tw
{
InstrumentVoice::InstrumentVoice (const VoiceConfig& config) : config_ (config)
{
    const auto& inst = instrument (config.instrument);
    engines::Setup setup;
    setup.preset = inst.preset;
    setup.voicing = config.voicing;
    setup.trimDb = inst.trimDb;
    setup.strikeScatter = false; // the strike variation is the plugin's Humanize, for every instrument
    std::string err;
    engine_ = engines::createEngine (inst.family, setup, &err);
    if (engine_ == nullptr)
        status_ = "Not available (" + juce::String (err) + ")";
}

void InstrumentVoice::prepare (double hostRate, int maxFrames)
{
    hostRate_ = hostRate;
    maxFrames_ = juce::jmax (1, maxFrames);
    if (engine_ == nullptr)
        return;
    engineRate_ = engine_->rateFor (hostRate_);
    engine_->prepare (engineRate_);
    const double ratio = engineRate_ / hostRate_;
    resample_ = std::abs (ratio - 1.0) > 1e-9;
    if (resample_)
        resampler_.prepare (ratio, maxFrames_);

    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto rate = juce::String (engineRate_ / 1000.0, 1) + " kHz";
    switch (instrument (config_.instrument).family)
    {
        case engines::Family::Tines:
            status_ = "Physical model" + dot + "pmtines" + dot + rate;
            break;
        case engines::Family::Reeds:
            status_ = "Physical model" + dot + "pmreeds" + dot + rate;
            break;
        case engines::Family::SynthKeys:
            status_ = "Hybrid: sampled attack, computed modes" + dot + rate;
            break;
    }
}

void InstrumentVoice::render (float* left, float* right, int n)
{
    jassert (n <= maxFrames_);
    if (engine_ == nullptr)
    {
        juce::FloatVectorOperations::clear (left, n);
        juce::FloatVectorOperations::clear (right, n);
        return;
    }
    if (!resample_)
    {
        engine_->process (left, n);
        juce::FloatVectorOperations::copy (right, left, n);
        return;
    }
    if (const int m = resampler_.inputNeeded (n); m > 0)
    {
        engine_->process (resampler_.writeLeft(), m);
        juce::FloatVectorOperations::copy (resampler_.writeRight(), resampler_.writeLeft(), m);
        resampler_.appended (m);
    }
    resampler_.process (left, right, n);
}
} // namespace tw
