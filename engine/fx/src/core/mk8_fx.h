// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// mk8_fx.h - the Rhodes MK8 preamp and effect chain, ported from velvetkeys
//
// Taken from velvetkeys (src/vksynth/effect_chain.*, dsp_fastmath.h, dsp_lut.h, same author,
// GPL-3.0-or-later) and changed only where pmpiano needs it:
// - arithmetic in `real` like the rest of the real-time path, no iostream;
// - the LFO phases of Vari-Pan, chorus and phaser are 64-bit fractions of a turn (turns() in
//   dsp.h) with increments computed in double when a rate changes: summed in float they
//   drifted (phaser 28 dB, chorus 57 dB, Vari-Pan 51 dB between the float and double builds);
// - reset() clears every state, so allSoundOff() renders like a fresh engine;
// - the compressor starts without gain reduction (velvetkeys started at the 0.05 floor, -26 dB,
//   and needed the release time to recover);
// - the Envelope moves the centre of the mid peak as measured on the plugin; the touch-wah of
//   velvetkeys is gone.
//
// Signal flow: drive (diode saturation, 10 kHz anti-fizz) -> compressor -> mid peak, its centre
// moved by the Envelope -> bass and treble shelves -> Vari-Pan -> chorus -> phaser -> delay ->
// volume, soft limiter.
// The compressor sits ahead of the mid filter like the hardware (the manual's hearing warning
// pins it there); the V8 plugin runs it after the EQ, which the earlier per-stage fits followed.
// (its output reaches 0.98 / fastTanh(0.9) = 1.34; the engine's soft clip follows).
//
// Calibration against the Rhodes V8 Pro plugin (docs/PLAN.md §11): the controls take the
// plugin's units; effects measured on the plugin replace the velvetkeys ones step by step
// (so far: EQ, Envelope). The delay lines are allocated by init() for the delay time set then
// (not in the real-time path).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "config.h"
#include "dsp.h"

namespace pmp {
namespace mk8fx {

// tanh(x) ~ x (27 + x^2) / (27 + 9 x^2), bounded
inline real fastTanh(real x) {
    const real x2 = x * x;
    const real y = (x * (real(27) + x2)) / (real(27) + real(9) * x2);
    return std::clamp<real>(y, -1, 1);
}

// tan(x) for 0 <= x <= pi/4 (cut-off / sample rate stays small)
inline real fastTan(real x) {
    const real x2 = x * x;
    return x * (real(1) + x2 * (real(0.333333333) + x2 * real(0.133333333)));
}

// sweep of the mid peak in octaves for a level at the mid filter (dBFS of the plugin) and an
// Envelope amount 0..1: the characteristic measured on the Rhodes V8 Pro, K(level) * amount
real envelopeOctaves(real levelDb, real amount);

// sine table over one turn, linear interpolation
template <int kSize = 2048>
struct SinLUT {
    static_assert((kSize & (kSize - 1)) == 0, "LUT size must be a power of 2");
    real table[kSize];

    void init() {
        for (int i = 0; i < kSize; ++i) table[i] = real(std::sin(2.0 * kPi * double(i) / kSize));
    }
    real sin01(real phase) const {            // phase in turns, wrapped
        phase -= std::floor(phase);
        return sin01_nofloor(phase);
    }
    real sin01_nofloor(real phase) const {    // 0 <= phase < 1
        const real x = phase * real(kSize);
        int i0 = int(x);
        const real frac = x - real(i0);
        constexpr int mask = kSize - 1;
        const int i1 = (i0 + 1) & mask;
        i0 &= mask;
        return table[i0] + (table[i1] - table[i0]) * frac;
    }
};

// biquad for the mid peak (RBJ cookbook), direct form I
struct BiquadFilter {
    real b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    real x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void setPeaking(real freq, real sampleRate, real gainDb, real q);
    void setLowpass(real freq, real sampleRate, real q);
    real process(real input);
    void reset() { x1 = x2 = y1 = y2 = 0; }
};

// first-order section (s + wz) / (s + wp) * k, bilinear with prewarping at fw
struct FirstOrder {
    real b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;

    void set(double fz, double fp, double k, double fw, double fs);
    real process(real x) {
        const real y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0; }
};

// tone of the plugin's bucket-brigade lines (delay echoes, chorus), as measured: high pass
// 146 Hz, resonant low pass 2597 Hz (Q 5), Butterworth low pass 8th order 2363 Hz, +3.2 dB
struct BbdFilter {
    FirstOrder hp;
    BiquadFilter lp[5];
    void setup(real fs);
    real process(real x) {
        x = hp.process(x);
        for (auto& f : lp) x = f.process(x);
        return x * real(1.445);
    }
    void reset() {
        hp.reset();
        for (auto& f : lp) f.reset();
    }
};

// first-order all-pass for the phaser
class AllPassFilter {
public:
    real process(real x, real a);
    void reset() { x1_ = y1_ = 0; }

private:
    real x1_ = 0, y1_ = 0;
};

// delay line with linear interpolation; init() allocates
class DelayLine {
public:
    void init(int maxSamples);
    void write(real sample);
    real read(real delaySamples) const;
    void clear();

private:
    std::vector<real> buffer_;
    int writeIndex_ = 0;
};

// post-drive one-pole low pass (anti-fizz)
struct OnePoleLP {
    real a = 0, z = 0;
    void setCutoff(real fc, real fs) {
        fc = std::clamp<real>(fc, 20, real(0.49) * fs);
        a = real(1) - std::exp(real(-2.0 * kPi) * fc / fs);
    }
    real process(real x) {
        z += a * (x - z);
        return z;
    }
    void reset() { z = 0; }
};

} // namespace mk8fx

class Mk8Effects {
public:
    Mk8Effects();

    void init(real sampleRate);                  // allocates the delay lines, then reset()
    void reset();                                // silence all states
    void process(float* left, float* right, int n);   // in place

    // Controls in the units of the Rhodes V8 Pro plugin (its parameter names in brackets).
    // amp: volume and drive in % (preamp_volume, preamp_drive); the envelope in %
    // (preamp_envelope) moves the centre of the mid peak up in octaves with the level at the
    // mid filter, as measured on the plugin; at 0 the peak stays where the EQ sets it
    void setVolume(real pct)   { volumePct_ = std::clamp<real>(pct, 0, 100); }
    void setDrive(real pct)    { drive_ = std::clamp<real>(pct, 0, 100) / 100; }
    void setEnvelope(real pct) { envelope_ = std::clamp<real>(pct, 0, 100) / 100; }
    // EQ in dB -15..+15 (eq_low, eq_mid_gain, eq_high), mid frequency 100..2000 Hz (eq_mid_freq)
    void setBass(real db)       { lowDb_ = std::clamp<real>(db, -15, 15); }
    void setMiddle(real db)     { midDb_ = std::clamp<real>(db, -15, 15); }
    void setMiddleFreq(real hz) { midHz_ = std::clamp<real>(hz, 100, 2000); }
    void setTreble(real db)     { highDb_ = std::clamp<real>(db, -15, 15); }
    // Vari-Pan: on, waveform 0 square, 1 ramp, 2 triangle, 3 sine (pan_wave), rate 0.1..3800 Hz,
    // depth %
    void setPanOn(bool on)     { vpOn_ = on; }
    void setPanWave(int w)     { vpWave_ = std::clamp(w, 0, 3); }
    void setPanRate(real hz)   { vpHz_ = std::clamp<real>(hz, real(0.1), 3800); }
    void setPanDepth(real pct) { vpDepth_ = std::clamp<real>(pct, 0, 100) / 100; }
    // compressor: on, amount %, makeup 0..20 dB (comp_amount, comp_makeup)
    void setCompressorOn(bool on) { compOn_ = on; }
    void setCompAmount(real pct)  { amount_ = std::clamp<real>(pct, 0, 100) / 100; }
    void setCompMakeup(real db)   { makeupDb_ = std::clamp<real>(db, 0, 20); }
    // chorus: rate 0.2..20 Hz, depth 0..10 ms; phaser: rate 0.1..10 Hz, depth %
    void setChorusOn(bool on)       { chorusOn_ = on; }
    void setChorusRate(real hz)     { chorusHz_ = std::clamp<real>(hz, real(0.2), 20); }
    void setChorusDepth(real ms)    { chorusMs_ = std::clamp<real>(ms, 0, 10); }
    void setPhaserOn(bool on)       { phaserOn_ = on; }
    void setPhaserRate(real hz)     { phaserHz_ = std::clamp<real>(hz, real(0.1), 10); }
    void setPhaserDepth(real pct)   { phaserDepth_ = std::clamp<real>(pct, 0, 100) / 100; }
    // delay: time 0..4 s, feedback %, mix %, ping-pong
    void setDelayOn(bool on)        { delayOn_ = on; }
    void setDelayTime(real s)       { delaySec_ = std::clamp<real>(s, 0, 4); }
    void setDelayFeedback(real pct) { delayFeedback_ = std::clamp<real>(pct, 0, 100) / 100; }
    void setDelayMix(real pct)      { delayMix_ = std::clamp<real>(pct, 0, 100) / 100; }
    void setDelayPingPong(bool on)  { delayPingPong_ = on; }

private:
    real driveStage(real x, mk8fx::OnePoleLP& lp) const;
    void updateEq();                     // coefficients, only when an EQ control changed
    real lfoValue(real phase, int waveform) const;
    void updateIncrements();             // LFO increments, only when a rate changed

    real sampleRate_ = 48000;

    // controls (plugin units; fractions for %)
    real volumePct_ = 53, envelope_ = 0, drive_ = 0;
    real lowDb_ = 0, midDb_ = 0, midHz_ = 500, highDb_ = 0;
    bool vpOn_ = false;
    int  vpWave_ = 3;
    real vpHz_ = 1, vpDepth_ = 0;
    bool compOn_ = false;
    real amount_ = 0, makeupDb_ = 0;
    bool chorusOn_ = false, phaserOn_ = false, delayOn_ = false, delayPingPong_ = false;
    real chorusHz_ = 1, chorusMs_ = 0, phaserHz_ = 1, phaserDepth_ = 0;
    real delaySec_ = real(0.5), delayFeedback_ = 0, delayMix_ = real(0.5);

    // Envelope: level follower of the mid peak (mean square of both channels, then a one-pole on
    // its root: 0.18 s rising, 0.15 s falling), on the signal after the drive and the compressor
    real envPowerCoeff_ = 0, envAttackCoeff_ = 0, envReleaseCoeff_ = 0;
    real envPower_ = 0, envLevel_ = 0;
    int envCounter_ = 0;

    // EQ as measured on the plugin (PLAN §11): first-order shelves, mid peak Q 2.2; midStaticHz_
    // its centre without Envelope, midFreqHz_ the current one
    mk8fx::FirstOrder lowF_[2], highF_[2];
    mk8fx::BiquadFilter midF_[2];
    real midStaticHz_ = 93, midFreqHz_ = 93, midQ_ = real(2.2);
    real eqCache_[4] = {-99, -99, -99, -99};      // controls the coefficients belong to

    // drive (per block)
    // drive as measured on the plugin: gain, asymmetric tanh, low pass 2150 Hz, gain
    real drivePre_ = 1, drivePost_ = 1;
    mk8fx::OnePoleLP driveLP_[2];

    // Vari-Pan, compressor, chorus, phaser, delay
    uint64_t vpPhase_ = 0, vpInc_ = 0;
    real vpPanL_ = 0, vpPanR_ = 0, vpSmooth_ = 0;       // pan gains [dB], smoothing
    // compressor as measured on the plugin: rms detector, gain smoothed in dB
    real compPow_ = 0, compGainDb_ = 0;
    real compDetCoeff_ = 0, compAttackCoeff_ = 0, compReleaseCoeff_ = 0;
    mk8fx::DelayLine chorusL_, chorusR_;
    uint64_t chorusPhase_ = 0, chorusInc_ = 0;
    mk8fx::AllPassFilter phaser_[2][4];
    uint64_t phaserPhase_ = 0, phaserInc_ = 0;
    real incRates_[3] = {-1, -1, -1}, incRate_ = -1;   // rates the increments belong to
    real phaserAlpha_ = 0, phaserAlphaTarget_ = 0;
    int  phaserCounter_ = 0;
    mk8fx::DelayLine delayL_, delayR_;
    mk8fx::BbdFilter echoTone_[2], chorusTone_[2];

    mk8fx::SinLUT<2048> sin_;
};

} // namespace pmp
