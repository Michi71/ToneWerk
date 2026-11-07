// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.h - the synthkeys instrument: 88 keys, sustain pedal, hybrid voices, mono
//
// Threading contract: all methods are meant to be called from one thread (the audio
// thread) or between process() calls. Hosts that receive MIDI on another thread push
// events through an EventQueue (event_queue.h) and apply them before process().
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "config.h"
#include "dsp.h"
#include "hybrid_bank.h"
#include "params.h"

namespace pmp {

class Engine {
public:
    Engine();
    explicit Engine(const Params& params);

    // --- setup (not real-time safe) -------------------------------------------------------
    void setParams(const Params& params);     // takes effect with the next prepare()
    const Params& params() const { return params_; }
    void setSampleRate(double fs);            // prepares the bank and the output chain
    void prepare();

    // --- MIDI style interface (real-time safe) --------------------------------------------
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void processMidiController(int cc, int value);
    void allNotesOff();
    void allSoundOff();

    void process(float* out, int nFrames);    // mono, host sample rate

    // --- live controls (real-time safe) -----------------------------------------------------
    void setVolume(double v) { volume_ = real(v); } // 0..1
    // the voicing of the hybrid voice (decay/unison/stretch/hardness on the poles)
    void setHybridVoicing(const HybridVoicing& v);
    // the hybrid voice's resonance bank (the strings that ring along)
    void setHybridResonance(const HybridResonance& r);

    // --- info -------------------------------------------------------------------------------
    double sampleRate() const { return fs_; }
    double internalRate() const { return fs_; }   // the bank runs at its own rate, resampled to fs_
    int    activeKeys() const;                   // sounding keys of the hybrid player
    int    maxVoices() const;                    // polyphony limit (num.maxVoices)
    bool   sustain() const { return sustain_; }
    bool   hybridActive() const { return hybridActive_; }   // the selected bank is prepared at this rate
    static bool inRange(int note) { return note >= kFirstKey && note <= kLastKey; }
    bool playable(int note) const {             // within the engine and the instrument's own range (keys.lo .. keys.hi)
        return inRange(note) && note >= int(params_.keysLo + 0.5f) && note <= int(params_.keysHi + 0.5f);
    }

private:
    // the bank and the host rate decide which player runs; a switch silences the old one
    void updateHybrid();

    // the hybrid voice: the selected embedded bank and its player
    HybridBank                     hybridBank_{};
    std::unique_ptr<HybridPlayer>  hybrid_;
    const uint8_t* hybridBankData_ = nullptr;   // the image hybridBank_ is attached to
    size_t         hybridBankSize_ = 0;
    bool   hybridActive_   = false;           // the player runs; its bank rate may differ from fs_
    bool   hybridResample_ = false;           // fs_ differs from the bank rate: resample the player's output
    Resampler hybridRes_;                     // bank rate -> fs_
    static constexpr int kHybridIn = 64;      // the player is fed in its own chunk size
    static constexpr int kHybridOutCap = 2048;   // outputs of one fill: 64 inputs at a ratio up to 32
    float  hybIn_[kHybridIn] = {};
    float  hybOut_[kHybridOutCap] = {};
    int    hybHave_ = 0, hybPos_ = 0;         // resampled outputs waiting to be handed to the host

    Params params_;
    double fs_ = 44100.0;

    bool   sustain_ = false;         // pedal position >= 0.5
    real   pedalPos_ = 0;            // CC64/127: 0 up, 1 fully down (half pedal)
    real   volume_  = 1;
    real   trim_    = 1;             // out.trimDb: the instrument's level

    HighPass1 hpf_;                  // DC blocker behind the voice
};

} // namespace pmp
