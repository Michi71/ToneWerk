// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.h - the pmreeds instrument: up to 68 keys, sustain pedal, pickup electrics, mono
//
// Threading contract: all methods are meant to be called from one thread (the audio
// thread) or between process() calls. Hosts that receive MIDI on another thread push
// events through an EventQueue (event_queue.h) and apply them before process().
#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#if defined(_WIN32)
#include <malloc.h>
#endif

#include "config.h"
#include "dsp.h"
#include "key_voice.h"
#include "params.h"

namespace pmp {

class Engine {
public:
    Engine();
    explicit Engine(const Params& params);
    // From precomputed key data for sample rate fs: no reed designer, no
    // auto-voicing. keys holds kNumKeys configurations and is used in place, without a copy
    // (on a microcontroller: a table in flash), so it has to outlive the engine. params holds
    // the exported parameters.
    Engine(const Params& params, double fs, const KeyConfig* keys);

    // --- setup (not real-time safe) -------------------------------------------------------
    void setParams(const Params& params);     // takes effect with the next prepare()
    const Params& params() const { return params_; }
    void setSampleRate(double fs);            // prepares all keys incl. auto-voicing
    void prepare(bool autoVoice = true);
    void prepare(double fs, const KeyConfig* keys);   // precomputed key data, used in place
    bool prepareKey(int note, bool autoVoiceKey = false);   // single key (analysis); without auto-voicing its gain is 1

    // --- MIDI style interface (real-time safe) --------------------------------------------
    void noteOn(int note, int velocity);
    // velocity: the key's release speed (0..127); the damper's blow and the catch of the swinging reed scale with it
    void noteOff(int note, int velocity = 64);
    void processMidiController(int cc, int value);
    void allNotesOff();
    void allSoundOff();

    void process(float* out, int nFrames);    // mono, host sample rate

    // --- live controls (real-time safe) -----------------------------------------------------
    void setVoicing(double delta);            // added to pickup offset (in units of width)
    void setVolume(double v) { volume_ = real(v); } // 0..1
    double voicing() const { return voicing_; }

    // --- info -------------------------------------------------------------------------------
    double sampleRate() const { return fs_; }
    double internalRate() const { return fs_ * os_; }
    int    activeKeys() const { return activeCount_; }   // sounding keys incl. stolen ones
    int    maxVoices() const;                           // polyphony limit (num.maxVoices)
    static constexpr int kMaxChoking = 4;               // stolen voices fading at a time
    bool   sustain() const { return sustain_; }
    double hammerVelocity(int velocity) const;
    const KeyConfig& keyConfig(int note) const { return keys_[keyIndex(note)]; }  // as prepared
    KeyVoice& voice(int note) { return voices_[keyIndex(note)]; }

    static bool inRange(int note) { return note >= kFirstKey && note <= kLastKey; }
    bool playable(int note) const {             // within the engine and the instrument's own range (keys.lo .. keys.hi)
        return inRange(note) && note >= int(params_.keysLo + 0.5f) && note <= int(params_.keysHi + 0.5f);
    }
    static int keyIndex(int note) { return note < kFirstKey ? 0 : note > kLastKey ? kNumKeys - 1 : note - kFirstKey; }

private:
    KeyConfig makeKeyConfig(int note) const;
    // damper engagement the current pedal position leaves: 1 = fully on (pedal up),
    // 0 = lifted (pedal down); between damper.pedalLost and damper.pedalFull the
    // rail smoothsteps (lost motion of the rail, felt pre-load)
    real pedalDamperFraction() const;
    KeyConfig* ownKeys();                     // writable key data (reed designer, auto-voicing)
    void setupVoices();                       // voices and filters from keys_
    void autoVoice(int only = -1);              // all keys, or key index `only`
    void activate(int k);
    void makeRoom(int k);                     // polyphony limit before key k starts
    void applyVoicing();
    void setupOutput();                       // mech modes, release thump, pedal clunk, level
    void applyMechScales();                   // params_.mechByKey -> the voices' force gains
    void resetOutput();                       // filter states after the voices
    void renderBlock(int m);                  // m <= kBlock frames into bufOut_

    Params params_;
    double fs_ = 44100.0;
    int    os_ = 2;

    // Key data of all keys: the caller's table or own_, which the reed designer
    // and the auto-voicing fill. The voices take what they need and keep no copy.
    const KeyConfig*             keys_ = nullptr;
    std::unique_ptr<KeyConfig[]> own_;
    std::array<KeyVoice, kNumKeys>  voices_{};
    std::array<bool, kNumKeys>      keyDown_{};
    uint64_t rng_ = 1;                        // strike variation: xorshift state
    float    rngSeed_ = -1.0f;                // human.seed it was seeded with; a prepare keeps the running sequence
    double gaussian();                        // approx. N(0,1) from four uniform draws, no libm
    std::array<bool, kNumKeys>      listed_{};
    std::array<int, kNumKeys>       active_{};
    int activeCount_ = 0;

    bool   sustain_ = false;         // pedalPos_ >= 0.5 (stealing, note handling)
    real   pedalPos_ = 0;            // CC64/127: 0 up, 1 fully down (half pedal)
    double voicing_ = 0.0;
    real   volume_  = 1;
    real   trim_    = 1;             // out.trimDb: the instrument's level

    // mechanical resonances driven by the hammer force (host rate)
    static constexpr int kMechModes = 3;
    ForcedMode        mech_[kMechModes];
    real              mechGain_[kMechModes] = {};
    bool              mechOn_ = false;
    float             bufForce_[2 * kBlock] = {};

    // release thump (host rate): two modes struck by impulses that are scheduled at a key release
    static constexpr int kRelModes = 2, kRelEvents = 32;
    struct RelEvent { int due; real amp; int mode; };
    ForcedMode        rel_[kRelModes];
    real              relGain_[kRelModes] = {}, relNorm_[kRelModes] = {};   // gain x voiceRms, 1 / peak of a unit blow
    int               relDelay_[kRelModes] = {};                            // samples
    RelEvent          relEv_[kRelEvents] = {};
    int               relCount_ = 0, relRing_ = 0;                          // pending blows, samples the modes still ring
    int               relRingMax_ = 0;
    bool              relOn_ = false;
    void scheduleRelease(int k, real damperFraction);
    void renderRelease(int m);

    // pedal clunk (host rate): decaying noise through a band filter, one per direction
    Biquad            pedHp_[2], pedLp_[2];
    real              pedGain_[2] = {};                // gain x voiceRms / unit RMS, [0] down, [1] up
    real              pedEnv_ = 0, pedDecay_ = 1;      // amplitude of the noise now, factor per sample
    int               pedRing_ = 0, pedRingMax_ = 0, pedWhich_ = 0;
    bool              pedOn_ = false;
    void kickPedal(int which);
    void renderPedal(int m);

    // the blow (host rate): one noise burst per strike, all strikes in one envelope; its own generator
    Biquad            blowHp_, blowLp_, blowLp2_, blowHpG_, blowLpG_;   // steep path, and the gentle one (2nd order)
    bool              blowLp4_ = false;
    real              blowEnv_ = 0, blowDecay_ = 1, blowNorm_ = 0;   // amplitude now, factor per sample, gain x voiceRms
    real              blowEnvG_ = 0;                                  // amplitude of the gentle path
    int               blowRing_ = 0, blowRingMax_ = 0;
    bool              blowOn_ = false;
    uint64_t          blowRng_ = 1;
    real blowNoise();                         // N(0, 1) approx., own xorshift

    // strikes waiting for their lead (blow.leadMs): host samples left (-1: none) and the strike's values
    struct Pending { int left = -1; double speed = 0, level = 1, contact = 1; };
    std::array<Pending, kNumKeys> pending_{};
    int pendingCount_ = 0;
    int leadSamples(int note, int velocity) const;
    void fireStrike(int k, double speed, double level, double contact);
    int nextPending() const;                  // host samples to the next pending strike (kBlock if none sooner)
    void advancePending(int m);               // m host samples rendered: fire what is due
    void kickBlow(int note, double speed, double level);
    void renderBlow(int m);

    Biquad            lpf_;
    HalfbandDecimator decimator_;
    HighPass1         hpf_;
    float             bufInt_[2 * kBlock] = {};
    float             bufOut_[kBlock] = {};

};

} // namespace pmp
