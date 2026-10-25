// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// InstrumentVoice.h - one prepared pmpiano engine (one instrument) and the resampler from its rate to the host rate
//
// A hybrid bank plays at its own rate only (44.1 kHz for the free-library banks, 48 kHz for the grand), so the engine runs at
// the bank's rate and a short windowed-sinc resampler (Resampler.h) converts to the host rate. Built and prepared off the audio
// thread (InstrumentLoader); render() and the MIDI calls are real-time safe.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <core/engine.h>

#include "Resampler.h"

#include <memory>
#include <vector>

namespace tw
{
// the bank image of one slot, 16-byte aligned, shared by the engines that play it
struct BankImage
{
    BankImage (const juce::File& file, int slot);
    ~BankImage();
    bool valid() const { return data != nullptr; }

    uint8_t* data = nullptr;
    size_t size = 0;
    int slot = 0;
    juce::File file;

    JUCE_DECLARE_NON_COPYABLE (BankImage)
};

// what an instrument is built from
struct VoiceConfig
{
    int instrument = 0;
    float hardness = 0, decay = 0, unison = 0, stretch = 0;
    int bankGeneration = 0; // bumped when the bank folder changes

    bool operator== (const VoiceConfig& o) const
    {
        return instrument == o.instrument && bankGeneration == o.bankGeneration && juce::exactlyEqual (hardness, o.hardness)
            && juce::exactlyEqual (decay, o.decay) && juce::exactlyEqual (unison, o.unison) && juce::exactlyEqual (stretch, o.stretch);
    }
};

class InstrumentVoice
{
public:
    // Builds the engine of an instrument: the preset, the voicing, the bank (nullptr: none found). Not real-time safe.
    InstrumentVoice (const VoiceConfig& config, std::shared_ptr<BankImage> bank);

    // host rate and the largest render() call; allocates. Not real-time safe.
    void prepare (double hostRate, int maxFrames);
    double hostRate() const { return hostRate_; }
    int maxFrames() const { return maxFrames_; }

    // real time: n <= maxFrames frames at the host rate into left/right (overwritten)
    void render (float* left, float* right, int n);
    void noteOn (int note, int velocity) { engine_->noteOn (note, velocity); }
    void noteOff (int note) { engine_->noteOff (note); }
    void controller (int cc, int value) { engine_->processMidiController (cc, value); }
    void allNotesOff() { engine_->allNotesOff(); }

    const VoiceConfig& config() const { return config_; }
    double engineRate() const { return engineRate_; }
    pmp::Engine::HybridState hybridState() const { return engine_->hybridState(); }
    bool silent() const { return silent_; }   // an instrument without a model and without its bank
    const juce::String& status() const { return status_; }

private:
    VoiceConfig config_;
    std::shared_ptr<BankImage> bank_;
    std::unique_ptr<pmp::Engine> engine_;
    double engineRate_ = 48000.0, hostRate_ = 0.0;
    int maxFrames_ = 0;
    bool silent_ = false;
    juce::String status_;

    Resampler resampler_;
    bool resample_ = false;
};
} // namespace tw
