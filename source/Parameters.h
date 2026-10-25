// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Parameters.h - the plugin's parameters: instrument, level, voicing of the hybrid voices, the MK8 effect chain
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tw
{
namespace ids
{
    // instrument and output
    inline constexpr const char* instrument = "instrument";
    inline constexpr const char* volume = "volume";
    // voicing on the poles and layers of the bank (README "Voicing" of pmpiano); a change rebuilds the instrument
    inline constexpr const char* hardness = "voicing_hardness";
    inline constexpr const char* decay = "voicing_decay";
    inline constexpr const char* unison = "voicing_unison";
    inline constexpr const char* stretch = "voicing_stretch";
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
} // namespace ids

// the instruments are the presets of the engine (pmp::Params::presetId)
juce::StringArray instrumentNames();
juce::String instrumentId (int index);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// every parameter ID, in layout order (presets, tests)
const juce::StringArray& allParameterIds();
} // namespace tw
