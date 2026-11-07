// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// InstrumentVoice.h - one prepared sound engine (one instrument) and the resampler from its rate to the host rate
//
// The physical models (pmtines, pmreeds) run at their calibration rate, 44.1 kHz, and a short windowed-sinc resampler
// (Resampler.h) converts to the host rate; the hybrid voices (synthkeys) carry their banks to the host rate themselves. The
// engines render mono; the voice hands the same signal to both channels. Built and prepared off the audio thread
// (the loader thread); render() and the MIDI calls are real-time safe.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <sound_engine.h>

#include "Resampler.h"

#include <array>
#include <memory>

namespace tw
{
// what an instrument is built from
struct VoiceConfig
{
    int instrument = 0;
    engines::Voicing voicing;

    // the voicing controls in a fixed order (comparison, signature)
    std::array<float, 11> voicingValues() const
    {
        const auto& v = voicing;
        return { v.hardness, v.hammerMass, v.strikePoint, v.gauge, v.pickupGap, v.pickupOffset, v.tonebarDetune, v.damperRelease,
            v.decay, v.unison, v.stretch };
    }

    bool operator== (const VoiceConfig& o) const
    {
        if (instrument != o.instrument)
            return false;
        const auto a = voicingValues(), b = o.voicingValues();
        for (size_t i = 0; i < a.size(); ++i)
            if (!juce::exactlyEqual (a[i], b[i]))
                return false;
        return true;
    }
};

class InstrumentVoice
{
public:
    // Builds the engine of an instrument with its voicing. Not real-time safe.
    explicit InstrumentVoice (const VoiceConfig& config);

    // host rate and the largest render() call; prepares the engine and the resampler, allocates. Not real-time safe.
    void prepare (double hostRate, int maxFrames);
    double hostRate() const { return hostRate_; }
    int maxFrames() const { return maxFrames_; }

    // real time: n <= maxFrames frames at the host rate into left/right (overwritten)
    void render (float* left, float* right, int n);
    void noteOn (int note, int velocity)
    {
        if (engine_)
            engine_->noteOn (note, velocity);
    }
    void noteOff (int note)
    {
        if (engine_)
            engine_->noteOff (note);
    }
    void controller (int cc, int value)
    {
        if (engine_)
            engine_->controller (cc, value);
    }
    void allNotesOff()
    {
        if (engine_)
            engine_->allNotesOff();
    }

    const VoiceConfig& config() const { return config_; }
    double engineRate() const { return engineRate_; }
    bool silent() const { return engine_ == nullptr; } // the engine refused the preset (status says why)
    const juce::String& status() const { return status_; }
    int lowestKey() const { return engine_ ? engine_->lowestKey() : 0; }
    int highestKey() const { return engine_ ? engine_->highestKey() : 127; }

private:
    VoiceConfig config_;
    std::unique_ptr<engines::SoundEngine> engine_;
    double engineRate_ = 44100.0, hostRate_ = 0.0;
    int maxFrames_ = 0;
    juce::String status_;

    Resampler resampler_;
    bool resample_ = false;
};
} // namespace tw
