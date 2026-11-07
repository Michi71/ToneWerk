// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "Parameters.h"

#include <cmath>

namespace tw
{
using engines::Family;

const std::vector<Instrument>& instruments()
{
    // the Rhodes, the reed pianos (both physical models), the hybrid voices. The physical models keep the levels their
    // engines calibrate (the median of C3-C5 at velocity 100 on one target); the trims bring the hybrid voices to
    // them: at C4 velocity 100 the RMS of the first 200 ms was -25.3 (clavinet), -19.2 (Splendid), -25.1 (Salamander),
    // -18.0 dB (upright) against -21.3 ... -22.7 dB of the Rhodes (tests "[.levels]"). The Salamander gets 1 dB, not 3: its
    // sharp attacks put loud chords into the soft clip at +3 (0.013 % of the samples of chord4 v100, "[levels]")
    static const std::vector<Instrument> all {
        { Family::Tines, "mark1", "Rhodes Mark I", 0.0f },
        { Family::Tines, "mark2", "Rhodes Mark II", 0.0f },
        { Family::Tines, "mark5", "Rhodes Mark V", 0.0f },
        { Family::Tines, "mark7", "Rhodes Mark 7", 0.0f },
        { Family::Tines, "mark8", "Rhodes Mark 8", 0.0f },
        { Family::Tines, "sparkletop", "Rhodes Sparkle Top", 0.0f },
        { Family::Reeds, "wurli", "Wurlitzer 200A", 0.0f },
        { Family::Reeds, "pianet", "Hohner Pianet T", 0.0f },
        { Family::Hybrid, "clavinet", "Hohner Clavinet D6", 3.0f },
        { Family::Hybrid, "splendid_hybrid", "Splendid Grand Piano", -3.0f },
        { Family::Hybrid, "salamander", "Salamander Grand Piano", 1.0f },
        { Family::Hybrid, "upright", "Upright Piano KW", -4.0f },
    };
    return all;
}

const Instrument& instrument (int index)
{
    const auto& all = instruments();
    return all[size_t (juce::jlimit (0, int (all.size()) - 1, index))];
}

int instrumentIndex (const juce::String& preset)
{
    const auto& all = instruments();
    for (size_t i = 0; i < all.size(); ++i)
        if (preset == all[i].preset)
            return int (i);
    return -1;
}

juce::StringArray instrumentNames()
{
    juce::StringArray names;
    for (const auto& i : instruments())
        names.add (i.name);
    return names;
}

juce::String instrumentId (int index) { return instrument (index).preset; }

const juce::StringArray& voicingIds (Family family)
{
    static const juce::StringArray tines { ids::hardness, ids::hammerMass, ids::strikePoint, ids::gauge, ids::pickupGap,
        ids::pickupOffset, ids::tonebarDetune, ids::damperRelease };
    static const juce::StringArray reeds { ids::hardness, ids::hammerMass, ids::strikePoint, ids::gauge, ids::pickupGap,
        ids::pickupOffset, ids::damperRelease };
    static const juce::StringArray hybrid { ids::hardness, ids::decay, ids::unison, ids::stretch };
    return family == Family::Tines ? tines : family == Family::Reeds ? reeds : hybrid;
}

const juce::StringArray& allVoicingIds()
{
    static const juce::StringArray all { ids::hardness, ids::decay, ids::unison, ids::stretch, ids::hammerMass, ids::strikePoint,
        ids::gauge, ids::pickupGap, ids::pickupOffset, ids::tonebarDetune, ids::damperRelease };
    return all;
}

juce::String voicingLabel (const juce::String& id, Family family)
{
    if (id == ids::hardness) return "Hardness";
    if (id == ids::decay) return "Decay";
    if (id == ids::unison) return "Unison";
    if (id == ids::stretch) return "Stretch";
    if (id == ids::hammerMass) return "Mass";
    if (id == ids::strikePoint) return "Strike Point";
    if (id == ids::gauge) return family == Family::Reeds ? "Reed Gauge" : "Tine Gauge";
    if (id == ids::pickupGap) return "Pickup Gap";
    if (id == ids::pickupOffset) return "Pickup Offset";
    if (id == ids::tonebarDetune) return "Tonebar";
    if (id == ids::damperRelease) return "Damper";
    return id;
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
                    if (std::abs (v) < 0.5f * std::pow (10.0f, float (-decimals)))
                        v = 0.0f; // no "-0.00" from the rounding of a range around zero
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
    // the effect values the MK8 section of the earlier engine's presets carried (they stay the defaults of saved projects)
    struct
    {
        float fxVolume = 53, fxDrive = 25, fxEnvelope = 0, fxBass = 3, fxMid = -3, fxMidFreq = 500, fxTreble = 3;
        float fxPanOn = 1, fxPanWave = 3, fxPanRate = 1, fxPanDepth = 50;
        float fxCompOn = 1, fxCompAmount = 40, fxCompMakeup = 4;
        float fxChorusOn = 0, fxChorusRate = 2, fxChorusDepth = 1;
        float fxPhaserOn = 0, fxPhaserRate = 1, fxPhaserDepth = 40;
        float fxDelayOn = 1, fxDelayTime = 0.25f, fxDelayFeedback = 25, fxDelayMix = 25, fxDelayPingPong = 1;
    } const d;
    Layout layout;

    auto inst = std::make_unique<juce::AudioProcessorParameterGroup> ("inst", "Instrument", "|");
    inst->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::instrument, kVersion }, "Instrument",
        instrumentNames(), 0));
    inst->addChild (number (ids::volume, "Volume", { -48.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    inst->addChild (number (ids::hardness, "Hardness", { -200.0f, 200.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::decay, "Decay", { -90.0f, 300.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::unison, "Unison", { -100.0f, 200.0f, 1.0f }, 0.0f, "%", 0));
    inst->addChild (number (ids::stretch, "Stretch", { -20.0f, 20.0f, 0.1f }, 0.0f, "ct/oct"));
    inst->addChild (number (ids::humanize, "Humanize", { 0.0f, 100.0f, 1.0f }, 50.0f, "%", 0));
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

    auto reverb = std::make_unique<juce::AudioProcessorParameterGroup> ("reverb", "Reverb", "|");
    reverb->addChild (toggle (ids::reverbOn, "Reverb On", false));
    reverb->addChild (number (ids::reverbMix, "Reverb Mix", { 0.0f, 100.0f, 0.1f }, 25.0f, "%", 0));
    reverb->addChild (number (ids::reverbSize, "Reverb Size", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
    reverb->addChild (number (ids::reverbDecay, "Reverb Decay", skewed (0.3f, 10.0f, 2.0f), 2.0f, "s", 2));
    reverb->addChild (number (ids::reverbDamping, "Reverb Damping", { 0.0f, 100.0f, 0.1f }, 50.0f, "%", 0));
    reverb->addChild (number (ids::reverbPredelay, "Reverb Pre-Delay", { 0.0f, 200.0f, 0.1f }, 20.0f, "ms", 0));
    reverb->addChild (number (ids::reverbWidth, "Reverb Width", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    layout.add (std::move (reverb));

    // the technician's controls of the physical models (Rhodes, reed pianos); last, so the earlier parameters keep their places.
    // A heavier hammer and a thinner tine or reed stop at +100 % and -30 %: further, with a hard hammer, the contact of the
    // models runs away (engine/adapters)
    auto model = std::make_unique<juce::AudioProcessorParameterGroup> ("model", "Voicing", "|");
    model->addChild (number (ids::hammerMass, "Hammer Mass", { -80.0f, 100.0f, 1.0f }, 0.0f, "%", 0));
    model->addChild (number (ids::strikePoint, "Strike Point", { -50.0f, 100.0f, 1.0f }, 0.0f, "%", 0));
    model->addChild (number (ids::gauge, "Tine/Reed Gauge", { -30.0f, 100.0f, 1.0f }, 0.0f, "%", 0));
    model->addChild (number (ids::pickupGap, "Pickup Gap", { -80.0f, 300.0f, 1.0f }, 0.0f, "%", 0));
    model->addChild (number (ids::pickupOffset, "Pickup Offset", { -1.0f, 1.0f, 0.01f }, 0.0f, "", 2));
    model->addChild (number (ids::tonebarDetune, "Tonebar Detune", { -50.0f, 50.0f, 0.1f }, 0.0f, "ct"));
    model->addChild (number (ids::damperRelease, "Damper Release", { -90.0f, 300.0f, 1.0f }, 0.0f, "%", 0));
    layout.add (std::move (model));

    return layout;
}

const juce::StringArray& allParameterIds()
{
    static const juce::StringArray all {
        ids::instrument, ids::volume, ids::hardness, ids::decay, ids::unison, ids::stretch,
        ids::fxOn, ids::fxVolume, ids::fxDrive, ids::fxEnvelope, ids::fxBass, ids::fxMid, ids::fxMidFreq, ids::fxTreble,
        ids::panOn, ids::panWave, ids::panRate, ids::panDepth, ids::compOn, ids::compAmount, ids::compMakeup,
        ids::chorusOn, ids::chorusRate, ids::chorusDepth, ids::phaserOn, ids::phaserRate, ids::phaserDepth,
        ids::delayOn, ids::delayTime, ids::delayFeedback, ids::delayMix, ids::delayPingPong,
        ids::reverbOn, ids::reverbMix, ids::reverbSize, ids::reverbDecay, ids::reverbDamping, ids::reverbPredelay, ids::reverbWidth,
        ids::humanize,
        ids::hammerMass, ids::strikePoint, ids::gauge, ids::pickupGap, ids::pickupOffset, ids::tonebarDetune, ids::damperRelease
    };
    return all;
}
} // namespace tw
