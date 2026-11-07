// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Parameters.h - the plugin's parameters: instrument, level, voicing of the sound engines, the MK8 effect chain
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <sound_engine.h>

namespace tw
{
namespace ids
{
    // instrument and output
    inline constexpr const char* instrument = "instrument";
    inline constexpr const char* volume = "volume";
    // voicing on top of the calibrated instrument (README "Voicing"); a change rebuilds the instrument. Hardness acts on every
    // instrument, decay, unison and stretch on the hybrid voices (on their poles), the rest on the physical models
    inline constexpr const char* hardness = "voicing_hardness";
    inline constexpr const char* decay = "voicing_decay";
    inline constexpr const char* unison = "voicing_unison";
    inline constexpr const char* stretch = "voicing_stretch";
    inline constexpr const char* hammerMass = "voicing_hammer_mass";
    inline constexpr const char* strikePoint = "voicing_strike_point";
    inline constexpr const char* gauge = "voicing_gauge";
    inline constexpr const char* pickupGap = "voicing_pickup_gap";
    inline constexpr const char* pickupOffset = "voicing_pickup_offset";
    inline constexpr const char* tonebarDetune = "voicing_tonebar_detune";
    inline constexpr const char* damperRelease = "voicing_damper_release";
    // the MK8 preamp and effect chain (pmp::Mk8Effects), in the units of the Rhodes V8 Pro plugin
    inline constexpr const char* fxOn = "fx_on";
    inline constexpr const char* fxVolume = "fx_volume";
    inline constexpr const char* fxDrive = "fx_drive";
    inline constexpr const char* fxEnvelope = "fx_envelope";
    inline constexpr const char* fxBass = "fx_bass";
    inline constexpr const char* fxMid = "fx_mid";
    inline constexpr const char* fxMidFreq = "fx_mid_freq";
    inline constexpr const char* fxTreble = "fx_treble";
    inline constexpr const char* panOn = "pan_on";
    inline constexpr const char* panWave = "pan_wave";
    inline constexpr const char* panRate = "pan_rate";
    inline constexpr const char* panDepth = "pan_depth";
    inline constexpr const char* compOn = "comp_on";
    inline constexpr const char* compAmount = "comp_amount";
    inline constexpr const char* compMakeup = "comp_makeup";
    inline constexpr const char* chorusOn = "chorus_on";
    inline constexpr const char* chorusRate = "chorus_rate";
    inline constexpr const char* chorusDepth = "chorus_depth";
    inline constexpr const char* phaserOn = "phaser_on";
    inline constexpr const char* phaserRate = "phaser_rate";
    inline constexpr const char* phaserDepth = "phaser_depth";
    inline constexpr const char* delayOn = "delay_on";
    inline constexpr const char* delayTime = "delay_time";
    inline constexpr const char* delayFeedback = "delay_feedback";
    inline constexpr const char* delayMix = "delay_mix";
    inline constexpr const char* delayPingPong = "delay_pingpong";
    // the FDN reverb at the end of the chain (Reverb.h), independent of the FX switch
    inline constexpr const char* reverbOn = "reverb_on";
    inline constexpr const char* reverbMix = "reverb_mix";
    inline constexpr const char* reverbSize = "reverb_size";
    inline constexpr const char* reverbDecay = "reverb_decay";
    inline constexpr const char* reverbDamping = "reverb_damping";
    inline constexpr const char* reverbPredelay = "reverb_predelay";
    inline constexpr const char* reverbWidth = "reverb_width";
    // strike variation: every note-on's velocity scatters a little (Humanize.h)
    inline constexpr const char* humanize = "humanize";
} // namespace ids

// The instruments: presets of the three sound engines. The index is the value of the instrument parameter (saved in projects
// and presets), so new instruments go at the end.
struct Instrument
{
    engines::Family family;
    const char* preset; // the engine's preset id
    const char* name;   // shown in the instrument choice
    float trimDb;       // the plugin's level balance on top of the engine preset
};
const std::vector<Instrument>& instruments();
const Instrument& instrument (int index); // clamped to the table
int instrumentIndex (const juce::String& preset); // -1 if unknown
juce::StringArray instrumentNames();
juce::String instrumentId (int index);

// the voicing controls of a family, in the order of the panel
const juce::StringArray& voicingIds (engines::Family family);
// every voicing control (the reset button)
const juce::StringArray& allVoicingIds();
// the short name of a voicing control on the panel of a family
juce::String voicingLabel (const juce::String& id, engines::Family family);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// every parameter ID, in layout order (presets, tests)
const juce::StringArray& allParameterIds();
} // namespace tw
