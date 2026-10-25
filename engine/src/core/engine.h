// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.h - the pmpiano instrument: 73 keys, sustain pedal, pickup electrics
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
#include "clav_tone.h"
#include "dsp.h"
#include "key_voice.h"
#include "mk8_fx.h"
#include "params.h"
#include "piano_hybrid.h"

namespace pmp {

// 16-byte aligned memory for the bank images (posix_memalign is not on Windows)
inline void* alignedAlloc(size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, 16);
#else
    void* mem = nullptr;
    return posix_memalign(&mem, 16, size) == 0 ? mem : nullptr;
#endif
}
inline void alignedFree(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

class Engine {
public:
    Engine();
    explicit Engine(const Params& params);
    // From precomputed key data for sample rate fs: no tine designer, no
    // auto-voicing. keys holds kNumKeys configurations and is used in place, without a copy
    // (on a microcontroller: a table in flash), so it has to outlive the engine. params holds
    // the exported parameters.
    Engine(const Params& params, double fs, const KeyConfig* keys);

    // --- setup (not real-time safe) -------------------------------------------------------
    void setParams(const Params& params);     // takes effect with the next prepare()
    void refreshChain();                      // output-stage params only: rebuild the
                                              // chain between blocks (the D6 tone banks
                                              // flip while the instrument plays)
    const Params& params() const { return params_; }
    void setSampleRate(double fs);            // prepares all keys incl. auto-voicing
    void prepare(bool autoVoice = true);
    void prepare(double fs, const KeyConfig* keys);   // precomputed key data, used in place
    // --- hybrid voices (docs/PIANO_HYBRID_BANK.md, docs/CLAVINET_HYBRID_BANK.md, docs/WURLITZER_HYBRID.md): the bank image stays
    // with the caller (16-byte aligned, it has to outlive the engine or the next call). Nine bank slots (docs/SAMPLELIB_IMPORT.md),
    // hybrid.bank of the preset picks one: 0 the grand (piano_hybrid.bank), 1 the Rhodes Mark I (mark1_hybrid.bank), 2 the Mark II
    // and the Suitcase (mark2_hybrid.bank, docs/RHODES_HYBRID.md), 3 the Wurlitzer (wurli_hybrid.bank), 4 the clavinet
    // (clav_hybrid.bank), 5 the Pianet T (pianet_hybrid.bank), 6 the Steinway (steinway_hybrid.bank), 7 Grandpa's grand
    // (grandpas_hybrid.bank), 8 the Yamaha CP80 (cp80_hybrid.bank). With hybrid.on = 1 and a matching bank the hybrid voice plays.
    // The instruments without a physical model (the grand, the Pianet, the Steinway, Grandpa's, the CP80: hybridSlotHasModel)
    // stay silent without their bank; the others fall back to their physical models, the clavinet also in the pickup positions
    // the bank was not recorded in (clav.ab = 1, clav.cd = 0 only).
    // Setup, not real-time safe.
    enum class HybridState { Off, Active, NoBank, RateMismatch };   // NoBank / RateMismatch: hybrid.on is set but the voice cannot play
    static constexpr int kHybridSlots = 9;        // 0 grand, 1 Mark I, 2 Mark II (and the Suitcase), 3 Wurlitzer, 4 clavinet, 5 Pianet,
                                                  // 6 Steinway, 7 Grandpa's, 8 CP80
    static constexpr int kClavSlot = 4;
    static bool hybridSlotHasModel(int slot) { return slot >= 1 && slot <= 4; }   // a physical model plays without the bank
    // slot < 0: the slot of the current hybrid.bank
    bool setHybridBank(const uint8_t* data, size_t size, std::string* err = nullptr, int slot = -1);
    bool loadHybridBankFile(const std::string& path, std::string* err = nullptr, int slot = -1);   // reads the file into an image the engine owns
    // slot 0: $PMP_HYBRID_BANK, else banks/piano_hybrid.bank; the others $PMP_<NAME>_HYBRID_BANK, else banks/<name>_hybrid.bank with
    // <name> mark1, mark2, wurli, clav, pianet, steinway, grandpas, cp80 (in the working directory or the source tree)
    static std::string defaultHybridBankPath(int slot = 0);
    int hybridSlot() const;                    // the slot of the current hybrid.bank
    int hybridBankRate(int slot = -1) const;   // the rate of the bank in a slot [Hz] (0: no valid bank); a bank plays at its rate only
    void clearHybridBank(int slot = -1);
    void setHybridEnabled(bool on);           // live A/B switch (silences both sides, then switches)
    // The executor for the per-key work of the hybrid voices (HybridPlayer::setParallel; pi-e-piano docs/MULTICORE.md): the result
    // is the same with any executor and without one. Applies to every bank slot, also to banks set later. Not during process().
    void setParallel(Parallel* p);
    HybridState hybridState() const { return hybridState_; }
    bool hybridActive() const { return hybridActive_; }
    bool prepareKey(int note, bool autoVoiceKey = false);   // single key (analysis); without auto-voicing its gain is 1

    // --- MIDI style interface (real-time safe) --------------------------------------------
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void processMidiController(int cc, int value);
    void allNotesOff();
    void allSoundOff();

    void process(float* out, int nFrames);    // mono (L+R)/2, host sample rate
    void processStereo(float* left, float* right, int nFrames);

    // --- live controls (real-time safe) -----------------------------------------------------
    void setVoicing(double delta);            // added to pickup offset (in units of width)
    void setVolume(double v) { volume_ = real(v); } // 0..1
    void setVibratoDepth(double d);           // Suitcase stereo vibrato, 0..1
    void setTremoloDepth(double d);           // amplifier tremolo (200A), 0..1
    double tremoloDepth() const { return double(tremTarget_); }
    double tremoloPhase() const { return double(tremPhase_ >> 11) * 0x1p-53; }    // turns (tests)
    double voicing() const { return voicing_; }
    double vibratoDepth() const { return double(vibTarget_); }
    double vibratoPhase() const { return double(vibPhase_ >> 11) * 0x1p-53; }   // turns (tests)

    // --- info -------------------------------------------------------------------------------
    double sampleRate() const { return fs_; }
    double internalRate() const { return fs_ * os_; }
    int    activeKeys() const { return hybridActive_ ? hyb_[hybSlot_].player->sustainingKeys() : activeCount_; }   // sounding keys incl. stolen ones
    int    maxVoices() const;                           // polyphony limit (num.maxVoices)
    static constexpr int kMaxChoking = 4;               // stolen voices fading at a time
    bool   sustain() const { return sustain_; }
    double hammerVelocity(int velocity) const;
    const KeyConfig& keyConfig(int note) const { return keys_[keyIndex(note)]; }  // as prepared
    KeyVoice& voice(int note) { return voices_[keyIndex(note)]; }

    static bool inRange(int note) { return note >= kFirstKey && note <= kLastKey; }
    static int keyIndex(int note) { return note < kFirstKey ? 0 : note > kLastKey ? kNumKeys - 1 : note - kFirstKey; }

private:
    KeyConfig makeKeyConfig(int note) const;
    // damper engagement the current pedal position leaves: 1 = fully on (pedal up),
    // 0 = lifted (pedal down); between damper.pedalLost and damper.pedalFull the
    // rail smoothsteps (lost motion of the rail, felt pre-load)
    real pedalDamperFraction() const;
    KeyConfig* ownKeys();                     // writable key data (tine designer, auto-voicing)
    void setupVoices();                       // voices and filters from keys_
    void autoVoice(int only = -1);              // all keys, or key index `only`
    void activate(int k);
    void makeRoom(int k);                     // polyphony limit before key k starts
    void applyVoicing();
    void setupChain();
    void applyMechScales();                   // params_.mechByKey -> the voices' force gains
    void resetOutput();                       // filter and effect states after the voices
    void updateHybrid();                      // hybrid.on, the bank and the host rate decide which side plays
    void renderBlock(int m);                  // m <= kBlock frames into outL_/outR_
    void renderOutputStage(int m);            // bufOut_ -> outL_/outR_

    Params params_;
    double fs_ = 44100.0;
    int    os_ = 2;

    // Key data of all keys: the caller's table or own_, which the tine designer
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

    // hammer click (host rate): noise gated by the hammer force, band limited, added behind the chain
    bool              clickOn_ = false;
    real              clickGain_ = 0;
    Biquad            clickHp_, clickLp_;
    uint64_t          clickRng_ = 0x2545F4914F6CDD1DULL;
    float             bufClick_[kBlock] = {};
    float             bufClickForce_[2 * kBlock] = {};
    real clickNoise();                        // uniform, unit variance

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
    // the damper woosh of the hybrid grand (P6): one burst generator, restarted by every release
    Biquad            wooshHp_;
    real              wooshGain_ = 0, wooshEnv_ = 0, wooshDecay_ = 0;
    int               wooshRing_ = 0, wooshRingMax_ = 0, wooshDelay_ = 0, wooshDue_ = -1;
    real              wooshPendingAmp_ = 0;
    bool              wooshOn_ = false;
    real              pedGain_[2] = {};                // gain x voiceRms / unit RMS, [0] down, [1] up
    real              pedEnv_ = 0, pedDecay_ = 1;      // amplitude of the noise now, factor per sample
    int               pedRing_ = 0, pedRingMax_ = 0, pedWhich_ = 0;
    bool              pedOn_ = false;
    void kickPedal(int which);
    void kickWoosh(real fraction);
    void renderWoosh(int m);
    void renderPedal(int m);

    Biquad            lpf_;
    HalfbandDecimator decimator_;
    HighPass1         hpf_;
    float             bufInt_[2 * kBlock] = {};
    float             bufOut_[kBlock] = {};

    // instrument chain (Suitcase): tone controls, amp, speaker, vibrato, detune
    Biquad       pickupRes_;
    bool         pickupResOn_ = false;
    ClavTone     clavTone_;              // the D6 tone switch banks, behind the coil
    Biquad       toneLow_, toneHigh_, spkLow_, spkHigh_;
    bool         toneLowOn_ = false, toneHighOn_ = false, spkLowOn_ = false, spkHighOn_ = false;
    Saturator    amp_;
    bool         ampOn_ = false;
    bool         ampKnockOn_ = false;         // amp.perVoice: the amp is left on the knock modes, which are shared by the voices
    StereoDetune detune_;
    real         detuneMix_ = 0;
    uint64_t     vibPhase_ = 0, vibInc_ = 0;  // LFO phase and step per sample, 2^64 = 1 turn
    real         vibDepth_ = 0, vibTarget_ = 0, vibSmooth_ = 0;  // depth glides (no clicks)
    uint64_t     tremPhase_ = 0, tremInc_ = 0;  // amplifier tremolo: same LFO technique, own phase
    real         tremDepth_ = 0, tremTarget_ = 0, tremSmooth_ = 0;
    // MK8 preamp and effect chain (fx.on): created by prepare, not in the real-time path
    std::unique_ptr<Mk8Effects> fx_;
    bool         fxOn_ = false;
    float        outL_[kBlock] = {}, outR_[kBlock] = {};

    // hybrid voices: one bank and player per slot
    struct HybridSlot {
        std::unique_ptr<uint8_t[], void (*)(void*)> image{nullptr, alignedFree};   // image of loadHybridBankFile
        std::unique_ptr<HybridBank> bank;         // on the heap: the player keeps a pointer, the engine may be moved
        std::unique_ptr<HybridPlayer> player;
        double fs = 0;                            // the rate the player was prepared for
        bool   ready = false;                     // bank attached and the player prepared for fs_
    };
    HybridSlot   hyb_[kHybridSlots];
    int          hybSlot_ = 0;                 // the slot that plays (hybrid.bank)
    Parallel*    parallel_ = nullptr;          // setParallel
    HybridPlayer* hybrid() const { return hyb_[hybSlot_].player.get(); }
    bool         hybridActive_ = false;
    float        hybridRelease_[128] = {};   // decay.release of the preset per MIDI note, handed to the hybrid voice
    float        hybridVelDb_[128] = {};     // vel.gainDb of the preset per velocity (P4)
    HybridState  hybridState_ = HybridState::Off;
};

} // namespace pmp
