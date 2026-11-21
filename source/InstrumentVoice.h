// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// InstrumentVoice.h - one prepared sound engine (one instrument) and the resampler from its rate to the host rate
//
// The physical models (the Rhodes and reed engines) run at their calibration rate, 44.1 kHz, and a short windowed-sinc resampler
// (Resampler.h) converts to the host rate; the hybrid voices carry their banks to the host rate themselves. The
// engines render mono; the voice hands the same signal to both channels. Built and prepared off the audio thread
// (the loader thread); render() and the MIDI calls are real-time safe.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <sound_engine.h>

#include "Resampler.h"

#include <array>
#include <atomic>
#include <memory>

namespace tw
{
// what an instrument is built from
struct VoiceConfig
{
    int instrument = 0;
    engines::Voicing voicing;
    int maxVoices = 0; // polyphony limit, 0: every key
    float tuneCents = 0; // pitch of the whole instrument: the resampler plays the engine this much faster or slower

    // the voicing controls in a fixed order (comparison, signature)
    std::array<float, 11> voicingValues() const
    {
        const auto& v = voicing;
        return { v.hardness, v.hammerMass, v.strikePoint, v.gauge, v.pickupGap, v.pickupOffset, v.tonebarDetune, v.damperRelease,
            v.decay, v.unison, v.stretch };
    }

    bool operator== (const VoiceConfig& o) const
    {
        if (instrument != o.instrument || maxVoices != o.maxVoices || !juce::exactlyEqual (tuneCents, o.tuneCents))
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
    // latencyFrames: the delay from a note-on to its sound every voice shall have (latencyFor); a voice with less of its own
    // (no resampler, or a shorter lookahead) delays its output by the rest. 0: only the voice's own.
    void prepare (double hostRate, int maxFrames, int latencyFrames = 0);

    // The longest delay a voice has at a host rate [host frames]: the resampler's lookahead at the lowest ratio, i.e. a
    // physical model (calibration rate) tuned all the way down. The plugin reports it to the host and pads every voice to it,
    // so the latency does not change with the instrument or the tuning.
    static constexpr double kCalibrationRate = 44100.0;
    static constexpr double kMaxTuneCents = 100.0;
    static int latencyFor (double hostRate);
    // this voice's own delay [host frames] and the padding on top of it
    double ownLatency() const { return resample_ ? double (resampler_.lookahead()) / resampler_.ratio() : 0.0; }
    int padding() const { return int (pad_.size()); }
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
    // the engine produced a non-finite sample; the voice stays silent from then on (audio thread; read anywhere)
    bool failed() const { return failed_.load (std::memory_order_relaxed); }
    const juce::String& status() const { return status_; }
    int activeKeys() const { return engine_ ? engine_->activeKeys() : 0; } // sounding keys (tests)
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
    std::vector<float> pad_; // delay line that fills the voice's latency up to the reported one
    size_t padAt_ = 0;
    void delay (float* x, int n);
    std::atomic<bool> failed_ { false };

    bool finite (const float* x, int n) const;
};
} // namespace tw
