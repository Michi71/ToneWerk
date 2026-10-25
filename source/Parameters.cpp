// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "Parameters.h"

#include <core/params.h>

namespace tw
{
juce::StringArray instrumentNames()
{
    juce::StringArray names;
    for (int i = 0; i < pmp::Params::presetCount(); ++i)
        names.add (pmp::Params::presetName (i));
    return names;
}

juce::String instrumentId (int index)
{
    return pmp::Params::presetId (juce::jlimit (0, pmp::Params::presetCount() - 1, index));
}

namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;

    constexpr int kVersion = 1;

    std::unique_ptr<AudioParameterFloat> number (const char* id, const char* name, NormalisableRange<float> range,
        float def, const char* unit, int decimals = 1)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, kVersion }, name, range, def,
            juce::AudioParameterFloatAttributes()
                .withLabel (unit)
                .withStringFromValueFunction ([decimals, unit] (float v, int) {
                    return juce::String (v, decimals) + (*unit ? juce::String (" ") + unit : juce::String());
                }));
    }

    NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<AudioParameterBool> toggle (const char* id, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, kVersion }, name, def);
    }
} // namespace

Layout createParameterLayout()
{
    const pmp::Params d; // the engine's defaults: the effect values every preset carries
    Layout layout;

    auto inst = std::make_unique<juce::AudioProcessorParameterGroup> ("inst", "Instrument", "|");
    inst->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::instrument, kVersion }, "Instrument",
        instrumentNames(), 0));
    inst->addChild (number (ids::volume, "Volume", { -48.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    inst->addChild (number (ids::hardness, "Hardness", { -200.0f, 200.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::decay, "Decay", { -90.0f, 300.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::unison, "Unison", { -100.0f, 200.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::stretch, "Stretch", { -20.0f, 20.0f, 0.1f }, 0.0f, "ct/oct"));
    layout.add (std::move (inst));

    auto fx = std::make_unique<juce::AudioProcessorParameterGroup> ("fx", "Effects", "|");
    fx->addChild (toggle (ids::fxOn, "FX On", false));
    fx->addChild (number (ids::fxVolume, "Preamp Level", { 0.0f, 100.0f, 0.1f }, d.fxVolume, "%", 0));
    fx->addChild (number (ids::fxDrive, "Drive", { 0.0f, 100.0f, 0.1f }, d.fxDrive, "%", 0));
    fx->addChild (number (ids::fxEnvelope, "Envelope", { 0.0f, 100.0f, 0.1f }, d.fxEnvelope, "%", 0));
    fx->addChild (number (ids::fxBass, "Bass", { -15.0f, 15.0f, 0.1f }, d.fxBass, "dB"));
    fx->addChild (number (ids::fxMid, "Mid", { -15.0f, 15.0f, 0.1f }, d.fxMid, "dB"));
    fx->addChild (number (ids::fxMidFreq, "Mid Freq", skewed (100.0f, 2000.0f, 450.0f), d.fxMidFreq, "Hz", 0));
    fx->addChild (number (ids::fxTreble, "Treble", { -15.0f, 15.0f, 0.1f }, d.fxTreble, "dB"));
    fx->addChild (toggle (ids::panOn, "Vari-Pan On", d.fxPanOn > 0.5f));
    fx->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::panWave, kVersion }, "Vari-Pan Wave",
        juce::StringArray { "Square", "Ramp", "Triangle", "Sine" }, juce::jlimit (0, 3, int (d.fxPanWave + 0.5f))));
    fx->addChild (number (ids::panRate, "Vari-Pan Rate", skewed (0.1f, 20.0f, 2.0f), d.fxPanRate, "Hz", 2));
    fx->addChild (number (ids::panDepth, "Vari-Pan Depth", { 0.0f, 100.0f, 0.1f }, d.fxPanDepth, "%", 0));
    fx->addChild (toggle (ids::compOn, "Compressor On", d.fxCompOn > 0.5f));
    fx->addChild (number (ids::compAmount, "Compressor Amount", { 0.0f, 100.0f, 0.1f }, d.fxCompAmount, "%", 0));
    fx->addChild (number (ids::compMakeup, "Compressor Makeup", { 0.0f, 20.0f, 0.1f }, d.fxCompMakeup, "dB"));
    fx->addChild (toggle (ids::chorusOn, "Chorus On", d.fxChorusOn > 0.5f));
    fx->addChild (number (ids::chorusRate, "Chorus Rate", skewed (0.2f, 20.0f, 2.0f), d.fxChorusRate, "Hz", 2));
    fx->addChild (number (ids::chorusDepth, "Chorus Depth", { 0.0f, 10.0f, 0.01f }, d.fxChorusDepth, "ms", 2));
    fx->addChild (toggle (ids::phaserOn, "Phaser On", d.fxPhaserOn > 0.5f));
    fx->addChild (number (ids::phaserRate, "Phaser Rate", skewed (0.1f, 10.0f, 1.0f), d.fxPhaserRate, "Hz", 2));
    fx->addChild (number (ids::phaserDepth, "Phaser Depth", { 0.0f, 100.0f, 0.1f }, d.fxPhaserDepth, "%", 0));
    fx->addChild (toggle (ids::delayOn, "Delay On", d.fxDelayOn > 0.5f));
    fx->addChild (number (ids::delayTime, "Delay Time", skewed (0.01f, 4.0f, 0.5f), juce::jmax (0.01f, d.fxDelayTime), "s", 2));
    fx->addChild (number (ids::delayFeedback, "Delay Feedback", { 0.0f, 100.0f, 0.1f }, d.fxDelayFeedback, "%", 0));
    fx->addChild (number (ids::delayMix, "Delay Mix", { 0.0f, 100.0f, 0.1f }, d.fxDelayMix, "%", 0));
    fx->addChild (toggle (ids::delayPingPong, "Delay Ping-Pong", d.fxDelayPingPong > 0.5f));
    layout.add (std::move (fx));

    return layout;
}

const juce::StringArray& allParameterIds()
{
    static const juce::StringArray all {
        ids::instrument, ids::volume, ids::hardness, ids::decay, ids::unison, ids::stretch,
        ids::fxOn, ids::fxVolume, ids::fxDrive, ids::fxEnvelope, ids::fxBass, ids::fxMid, ids::fxMidFreq, ids::fxTreble,
        ids::panOn, ids::panWave, ids::panRate, ids::panDepth, ids::compOn, ids::compAmount, ids::compMakeup,
        ids::chorusOn, ids::chorusRate, ids::chorusDepth, ids::phaserOn, ids::phaserRate, ids::phaserDepth,
        ids::delayOn, ids::delayTime, ids::delayFeedback, ids::delayMix, ids::delayPingPong
    };
    return all;
}
} // namespace tw
