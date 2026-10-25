// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// piano_hybrid.h - Instrument 7, the hybrid grand: a 100 ms PCM attack that is crossfaded into a
// freely decaying bank of 32 damped complex modes (docs/PIANO_HYBRID_PLAN.md, docs/PIANO_HYBRID_BANK.md)
//
// The algorithm is that of the offline model (tools/analysis/piano_hybrid_render.py):
//   att[j]  = LP_v( w0 pcm_i[j] + w1 pcm_i+1[j] )                      j < N_att
//   z_k     = Z_k(n, v)  at j = N_e,   z_k <- a_k z_k   every sample after
//   out[j]  = G(v) ((1 - h) att + h Re sum z_k)   j < N_att,   G(v) Re sum z_k   afterwards
// with h the smoothstep over the fade [N_e, N_att]. The sustain of a key is ONE state vector per key:
// a new strike merges its mode state into it when its crossfade ends (the poles belong to the key, so
// the merge is exact), the old string is never reset. Setup (attach, prepare) allocates; noteOn,
// noteOff, controller and process do not.
//
// Bank version 3 adds the high-band loop (docs/PIANO_HYBRID_LOOP_DESIGN.md): above the highest mode of a key, a loop of the
// attack's own high band (half-octave band pieces of the four layers; per velocity and band fractional loop points tuned to the
// band's partials and a two-exponential envelope), mixed in with the crossfade like the modes. The reference is
// tools/analysis/piano_hybrid_loop.py. Versions 4 and 5 are read (1: without the loop, 2: one loop length for all bands, 3: 32 modes).
//
// Bank version 5 (docs/CLAVINET_HYBRID_BANK.md, written by tools/analysis/clavinet_hybrid_export.py) generalises version 4 for
// other instruments: 2..5 layers with their anchors, the crossfade [N_e, N_att] per key (the PCM rows of a key are its own N_att
// long, one after the other; the header's N_att is the longest), and a loop record per key with its own band edges (bands need not touch: gap bands below the split) and its own
// range of the band pieces. A version 4 bank plays bit for bit as before.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config.h"

namespace pmp {

constexpr int kHybridModes = 48;      // modes per key in the bank (a key may use fewer); 48 since bank version 4 (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P7)
constexpr int kHybridLayers = 4;      // version 4: PP MP MF FF, anchors 40 76 92 114
constexpr int kHybridMaxLayers = 5;   // version 5: up to five layers (the clavinet: 25 70 100 120 127)
constexpr int kHybridVelocities = 127;
constexpr int kHybridLoopBands = 8;   // half-octave bands of the loop at most

struct HybridKeyRec {                 // as written by piano_hybrid_export.py (48 bytes, 64 byte stride) and clavinet_hybrid_export.py (64 bytes)
    uint16_t modes, damped;
    float f0, releaseT60, trimDb;
    double pcmScale[kHybridMaxLayers];   // int16 -> amplitude, per layer (version 4: the fifth is padding)
    uint32_t nE, nAtt;                // version 5: the crossfade of this key (version 4: zero, the header's)
};

struct HybridLoopRec {                // version 3: the loop of a key (180 bytes, 192 byte stride); bands == 0: no loop
    uint16_t bands, seam;             // seam: crossfade at every loop wrap [samples]
    float split, edges[9];            // [Hz]: split = edges[0], band b is edges[b] .. edges[b + 1]
    uint32_t pcmOffset;               // bytes into the loop PCM: [layer][band][segLen] int16
    uint32_t velOffset;               // floats into the velocity section: 127 x per band (start, len, phase, a1, s1, a2, s2)
    float scale[kHybridLayers][kHybridLoopBands];   // int16 -> amplitude: scale / 32767
};

struct HybridLoopRec5 {               // version 5: the loop of a key (248 bytes, 256 byte stride); bands == 0: no loop
    uint16_t bands, seam;
    float split;                      // [Hz] 3 % above the highest mode; bands below it are gap bands
    float lo[kHybridLoopBands], hi[kHybridLoopBands];   // [Hz] per band
    uint32_t seg0, segLen;            // the band pieces cover the attack samples seg0 .. seg0 + segLen of this key
    uint32_t pcmOffset;               // bytes into the loop PCM: [layer][band][segLen] int16
    uint32_t velOffset;               // floats into the velocity section: 127 x per band (start, len, phase, a1, s1, a2, s2)
    float scale[kHybridMaxLayers][kHybridLoopBands];
};

// Validated read-only view of a bank image in memory (the caller keeps the memory alive, 16-byte aligned).
class HybridBank {
public:
    bool attach(const uint8_t* data, size_t size, std::string* err = nullptr);
    bool valid() const { return keys_ != nullptr; }

    int rate() const { return rate_; }
    // the rate in the header of a bank file (0: no readable bank): the hosts pick their rate from it before any engine runs
    static int fileRate(const std::string& path);
    int firstKey() const { return first_; }
    int lastKey() const { return last_; }
    int attackFrames() const { return nAtt_; }      // N_att (version 5: the longest)
    int fadeStart() const { return nE_; }           // N_e (version 5: of the first key)
    int attackFrames(int note) const;               // N_att of a key
    int fadeStart(int note) const;                  // N_e of a key
    int layers() const { return nLay_; }
    int anchor(int layer) const { return anchors_[layer]; }
    double level() const { return level_; }         // the loudness law: attack RMS = level (v/refVel)^gamma
    double refVel() const { return refVel_; }
    double gamma() const { return gamma_; }
    double rampMs() const { return rampMs_; }       // damper felt compression time
    double pedalLost() const { return pedalLost_; } // CC64/127 below this keeps the dampers fully on
    double pedalFull() const { return pedalFull_; } // CC64/127 above this lifts them fully off
    const HybridKeyRec& key(int note) const { return *reinterpret_cast<const HybridKeyRec*>(keys_ + size_t(idx(note)) * kKeyStride); }   // 64 byte stride in the image
    const double* poles(int note) const { return poles_ + size_t(idx(note)) * kHybridModes * 2; }          // (sigma [1/s], omega [rad/s]) per mode
    const float* states(int note, int vel) const;                                                          // (re, im) per mode
    const double* filter(int note, int vel) const;                                                         // b0 b1 b2 a1 a2
    const int16_t* pcm(int note, int layer) const { return pcm_ + pcmOff_[size_t(idx(note))] + size_t(layer) * size_t(attackFrames(note)); }
    const char* revision() const { return revision_; }   // SGP source revision
    const char* state() const { return state_; }         // name of the fit state
    int version() const { return version_; }

    // --- the high-band loop (version 3)
    bool hasLoop() const { return loopKeys_ != nullptr; }
    int loopSegStart() const { return seg0_; }      // first sample of the band pieces in the attack (version 4)
    int loopSegLength() const { return segLen_; }
    const HybridLoopRec* loop(int note) const;      // version 4; nullptr: no loop on this key (or a version 5 bank)
    int loopBands(int note) const;                  // 0: no loop on this key
    int loopSeam(int note) const;
    int loopSegLength(int note) const;
    float loopScale(int note, int layer, int band) const;
    const int16_t* loopPcm(int note, int layer, int band) const;
    const float* loopVelocity(int note, int vel) const;

public:
    static constexpr size_t kKeyStride = 64;
    static constexpr size_t kLoopStride = 192;
    static constexpr size_t kLoopStride5 = 256;

private:
    int idx(int note) const { return (note < first_ ? first_ : note > last_ ? last_ : note) - first_; }
    const HybridLoopRec5* loop5(int note) const;
    int rate_ = 0, first_ = 0, last_ = 0, nAtt_ = 0, nE_ = 0, nLay_ = kHybridLayers, anchors_[kHybridMaxLayers] = {};
    double level_ = 0, refVel_ = 1, gamma_ = 0, rampMs_ = 10, pedalLost_ = 0.5, pedalFull_ = 0.85;
    const uint8_t* keys_ = nullptr;
    const double* poles_ = nullptr;
    const float* z_ = nullptr;
    const double* filt_ = nullptr;
    const int16_t* pcm_ = nullptr;
    const uint8_t* loopKeys_ = nullptr;
    const uint8_t* loopPcm_ = nullptr;
    const float* loopVel_ = nullptr;
    int version_ = 0, seg0_ = 0, segLen_ = 0;
    char revision_[41] = {}, state_[25] = {};
    std::vector<size_t> pcmOff_;          // first PCM frame of each key (version 5: rows of the key's own N_att)
};

// The resonance bank ("harp", docs/PIANO_HYBRID_RESONANCE.md section 4): per key a second set of its first `modes` modes, driven by
// the dry output of all other keys, never by its own, and added to the output. Nothing feeds back, so it is stable for any gain.
struct HybridResonance {
    bool   on = false;                            // off: the player is bit-identical to one without the harp
    double gain = 0;                              // input weight of a mode at the key's f0 (the coupling)
    int    modes = 16;                            // modes per key (at most the key's own)
    double tiltDb = 0;                            // [dB per octave] of the input weight above the key's f0
    bool   undamped = true;                       // keys without a damper ring along without the pedal too
};

// Voicing of the hybrid grand (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P8): on the poles of the bank, live and without allocation;
// the states Z(n, v) are not touched, so the crossfade from the attack stays continuous (keep unison and stretch small: the
// attack PCM is not retuned). All zero: bit-identical to the bank.
struct HybridVoicing {
    double decay = 0;                             // [%] T60 of every mode x 2^(decay/100)
    double unison = 0;                            // [%] spread of the modes of one partial (within 1 % of each other): x (1 + unison/100)
    double stretch = 0;                           // [cent per octave] about C4, on the poles of a key
    double hardness = 0;                          // [%] +100 = the timbre of 32 velocity steps harder (layers, filter, states, loop); the level follows the played velocity
};

// The host's executor for the work of a chunk (pi-e-piano docs/MULTICORE.md S1): run() calls fn(ctx, i) once for every i in
// [0, items), in any order or on several cores at once, and returns when all calls are done. The player hands it one item per key;
// the items share nothing, and their outputs are summed in key order afterwards, so the result does not depend on how they ran.
// No executor: the player runs the items itself, in order.
struct Parallel {
    virtual ~Parallel() = default;
    virtual void run(void (*fn)(void*, int), void* ctx, int items) = 0;
};

struct HybridParams {
    double level = -1, refVel = -1, gamma = -1;   // < 0: from the bank
    int    attackSlots = 146;                     // two per key (20 Hz repetition at 100 ms), plan 7.2
    int    fadePool = 16;                         // slots for attacks that are faded out (overload)
    double stealFadeMs = 5.0;
    double sleepEnergy = 1e-12;                   // a key sleeps when sum |z|^2 stays below this (about -120 dB)
    double rampMs = -1, pedalLost = -1, pedalFull = -1;   // < 0: from the bank
    HybridResonance res;
};

class HybridPlayer {
public:
    HybridPlayer();
    ~HybridPlayer();
    HybridPlayer(const HybridPlayer&) = delete;
    HybridPlayer& operator=(const HybridPlayer&) = delete;

    // Not real-time safe. The bank has a fixed rate: another host rate is refused (no guessing, plan 10).
    bool prepare(double fs, const HybridBank& bank, const HybridParams& p = HybridParams());
    // Between process() calls, no allocation. Switching the harp on or off clears its state.
    void setResonance(const HybridResonance& r);
    // The damper's felt time and the pedal curve, live and without allocation (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P1):
    // a value <= 0 (rampMs) or < 0 (pedalLost, pedalFull) keeps the bank's. pedalFull is kept at least 0.05 above pedalLost.
    void setDamper(double rampMs, double pedalLost, double pedalFull);
    // T60 [s] of a key under its damper (decay.release of the preset), indexed by MIDI note 0..127; nullptr: the bank's values.
    // Live, no allocation (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P3).
    void setReleaseT60(const float* t60ByNote);
    // The slow phase under the damper (P3, stage 2): a share `share` of a key's amplitude (the polarisation the felt does not
    // reach) decays with T60 `slowT60` [s] while the rest decays with the key's release T60; share 0 keeps today's single phase
    // bit for bit. Live, no allocation.
    void setSlowPhase(double share, double slowT60);
    // vel.gainDb of the preset (P4): dB added to the bank's loudness law per MIDI velocity 0..127; nullptr: the law alone.
    // Live, no allocation; strikes already running keep their gain.
    void setVelocityGain(const float* dbByVelocity);
    void setVoicing(const HybridVoicing& v);
    // The executor of the per-key work (nullptr: in order on the calling thread). It must outlive the player or the next call.
    void setParallel(Parallel* p);
    // The voice limit (num.maxVoices): at most n keys sound (0 = all). A strike of a key that does not sound makes room: the
    // quietest sounding key gives up - a released one before a held or pedalled one, a key in its attack last - and fades
    // out within 20 ms. Keys already sounding are not cut when n drops. Live, no allocation.
    void setMaxKeys(int n);

    void noteOn(int note, int velocity);
    void noteOff(int note);
    // The clavinet's tangent leaves the string at the release: the whole string speaks, the pitch drops by `semitones`. The key's
    // poles are rebuilt at the scaled frequency (states keep amplitude and phase, like KeyVoice::setRetune); the next strike
    // restores them. The attack PCM and the high-band loop are not retuned. No allocation.
    void setRetune(int note, double semitones);
    void controller(int cc, int value);            // 64 sustain pedal, 120 sound off, 123 notes off
    void allSoundOff();                            // a 5 ms fade of everything, then silence
    void reset();                                  // hard: silence at once, like a freshly prepared player
    void process(float* out, int nFrames);         // mono, overwritten

    int attackVoices() const;                      // attacks in progress (first 100 ms of a strike)
    int sustainingKeys() const;                    // keys with a sounding mode state or loop
    int loops() const;                             // high-band loops sounding (active and fading out)
    long stolen() const { return stolen_; }        // attacks faded out for lack of a slot
    long dropped() const { return dropped_; }      // attacks lost for lack of a fade slot
    double gainOf(int velocity) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    long stolen_ = 0, dropped_ = 0;
};

} // namespace pmp
