// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// mk8_fx.cpp - Rhodes MK8 preamp and effect chain (ported from velvetkeys, see mk8_fx.h)
#include "mk8_fx.h"

namespace pmp {
namespace mk8fx {
namespace {

constexpr real kDB20 = real(2.302585093 / 20.0);        // dB -> ln(gain)
constexpr real k20OverLn10 = real(20.0 / 2.302585093);  // ln(gain) -> dB

inline real db2lin(real dB) { return std::exp(dB * kDB20); }
inline real db2lin40(real dB) { return std::exp(dB * real(2.302585093 / 40.0)); }
inline real lin2dB(real x) { return k20OverLn10 * std::log(x); }

} // namespace

void FirstOrder::set(double fz, double fp, double k, double fw, double fs) {
    const double K = 2.0 * kPi * fw / std::tan(kPi * fw / fs);   // prewarped at fw
    const double wz = 2.0 * kPi * fz, wp = 2.0 * kPi * fp, a0 = K + wp;
    b0 = real(k * (K + wz) / a0);
    b1 = real(k * (wz - K) / a0);
    a1 = real((wp - K) / a0);
}

void BiquadFilter::setPeaking(real freq, real sampleRate, real gainDb, real q) {
    const real A = db2lin40(gainDb);
    const real w0 = real(2.0 * kPi) * freq / sampleRate;
    const real c = std::cos(w0), alpha = std::sin(w0) / (real(2) * q);
    const real a0 = real(1) + alpha / A;
    b0 = (real(1) + alpha * A) / a0;
    b1 = (real(-2) * c) / a0;
    b2 = (real(1) - alpha * A) / a0;
    a1 = (real(-2) * c) / a0;
    a2 = (real(1) - alpha / A) / a0;
}

void BiquadFilter::setLowpass(real freq, real sampleRate, real q) {
    const real w0 = real(2.0 * kPi) * freq / sampleRate;
    const real c = std::cos(w0), alpha = std::sin(w0) / (real(2) * q), a0 = real(1) + alpha;
    b0 = (real(1) - c) / real(2) / a0;
    b1 = (real(1) - c) / a0;
    b2 = b0;
    a1 = real(-2) * c / a0;
    a2 = (real(1) - alpha) / a0;
}

void BbdFilter::setup(real fs) {
    static constexpr real kButterQ[4] = {real(0.5098), real(0.6013), real(0.9000), real(2.5629)};
    hp.set(0.0, 146.0, 1.0, 146.0, double(fs));
    lp[0].setLowpass(2597, fs, real(5.03));
    for (int s = 0; s < 4; ++s) lp[s + 1].setLowpass(2363, fs, kButterQ[s]);
}

real BiquadFilter::process(real input) {
    const real y = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = y;
    return y;
}

// y[n] = a x[n] + x[n-1] - a y[n-1]
real AllPassFilter::process(real x, real a) {
    const real y = a * x + x1_ - a * y1_;
    x1_ = x;
    y1_ = std::fabs(y) < real(1e-15) ? real(0) : y;
    return y;
}

void DelayLine::init(int maxSamples) {
    buffer_.assign(size_t(std::max(maxSamples, 2)), real(0));
    writeIndex_ = 0;
}

void DelayLine::write(real sample) {
    buffer_[size_t(writeIndex_)] = sample;
    if (++writeIndex_ >= int(buffer_.size())) writeIndex_ = 0;
}

real DelayLine::read(real delaySamples) const {
    if (buffer_.empty()) return 0;
    const int size = int(buffer_.size());
    delaySamples = std::clamp<real>(delaySamples, 0, real(size - 1));
    real pos = real(writeIndex_) - delaySamples;
    if (pos < 0) pos += real(size);
    int i0 = int(pos);
    const real frac = pos - real(i0);
    if (i0 >= size) i0 -= size;
    const int i1 = i0 + 1 < size ? i0 + 1 : 0;
    return buffer_[size_t(i0)] * (real(1) - frac) + buffer_[size_t(i1)] * frac;
}

void DelayLine::clear() {
    std::fill(buffer_.begin(), buffer_.end(), real(0));
    writeIndex_ = 0;
}

} // namespace mk8fx

namespace mk8fx {

// Envelope (the plugin's preamp_envelope): a slow level follower moves the centre of the mid
// peak up in octaves, f = f_mid * 2^(amount * K(level)); gain and width stay. Measured on the
// Rhodes V8 Pro 1.1.3 (its other effects off, Volume 53 %) as the ratio wet/dry of a triad that
// the sustain pedal holds at a level, 200 strikes per point.
constexpr real kEnvLevelDb[] = {-70, -59, -54.8, -50.6, -45.8, -42.4, -40.5, -36.3, -34};
constexpr real kEnvOctaves[]  = {0, 0.4, 0.7, 1.1, 2.0, 3.2, 5.0, 6.3, 6.6};   // at Envelope 100 %

// The table is in dBFS of the plugin; the level the follower sees here is louder by this
// amount. Fitted so that the engine's sweep on single notes (A2, A3, C4 at velocities 60..127,
// preset mk8) matches the plugin's: 0.14 octaves rms error at 23 dB, 0.7 at the 19.7 dB that the
// drive and the compressor take. Their 19.7 dB are not the level here: the dry outputs differ by
// 27.9 dB in rms, and the limiter behind the follower has a small-signal gain of 1.8 dB.
constexpr real kPluginLevelDb = real(23.0);
constexpr real kEnvAttackSec  = real(0.18);      // level follower, rising: fit to the measured courses
constexpr real kEnvReleaseSec = real(0.15);      // falling

// Between the points linear. Below -59 dBFS the measurement loses resolution (the sweep is
// small there), so it goes down to silence; above -34 dBFS the plugin's sweep does not grow.
real envelopeOctaves(real levelDb, real amount) {
    constexpr int n = int(sizeof(kEnvLevelDb) / sizeof(kEnvLevelDb[0]));
    real k = kEnvOctaves[n - 1];
    for (int i = 1; i < n; ++i)
        if (levelDb < kEnvLevelDb[i]) {
            const real t = std::clamp<real>((levelDb - kEnvLevelDb[i - 1]) / (kEnvLevelDb[i] - kEnvLevelDb[i - 1]), 0, 1);
            k = kEnvOctaves[i - 1] + t * (kEnvOctaves[i] - kEnvOctaves[i - 1]);
            break;
        }
    return k * amount;
}

} // namespace mk8fx

namespace {

using mk8fx::db2lin;
using mk8fx::fastTan;
using mk8fx::fastTanh;
using mk8fx::lin2dB;

// increment of a 64-bit phase for a rate [Hz] at sample rate fs, in double
uint64_t phaseIncrement(double hz, double fs) {
    return uint64_t(std::llround(std::min(hz / fs, 0.25) * 0x1p64));
}

// tanh limiter with 0.98 ceiling
inline real softLimiter(real x) {
    constexpr real ceiling = real(0.98), a = real(0.9);
    static const real norm = fastTanh(a);
    return fastTanh(a * (x / ceiling)) / norm * ceiling;
}

// plugin preamp volume as measured (level relative to its default of 53 %)
real volumeGain(real pct) {
    static constexpr real kPct[8] = {0, 10, 25, 40, 53, 65, 80, 100};
    static constexpr real kDb[8]  = {-36.0, -35.3, -19.5, -7.3, 0.0, 5.3, 10.7, 16.5};
    int i = 0;
    while (i < 6 && pct > kPct[i + 1]) ++i;
    const real t = std::clamp<real>((pct - kPct[i]) / (kPct[i + 1] - kPct[i]), 0, 1);
    return db2lin(kDb[i] + t * (kDb[i + 1] - kDb[i]));
}

} // namespace

Mk8Effects::Mk8Effects() { sin_.init(); }

void Mk8Effects::init(real sr) {
    sampleRate_ = sr >= 1 ? sr : real(48000);
    envPowerCoeff_   = real(1) - std::exp(real(-1) / (sampleRate_ * real(0.008)));   // 8 ms mean square
    envAttackCoeff_  = real(1) - std::exp(real(-1) / (sampleRate_ * mk8fx::kEnvAttackSec));
    envReleaseCoeff_ = real(1) - std::exp(real(-1) / (sampleRate_ * mk8fx::kEnvReleaseSec));
    chorusL_.init(int(sampleRate_ * real(0.05)));
    chorusR_.init(int(sampleRate_ * real(0.05)));
    const int delayLen = int(sampleRate_ * (delaySec_ + real(0.05)));   // as set (up to 4 s)
    delayL_.init(delayLen);
    delayR_.init(delayLen);
    for (auto& f : driveLP_) f.setCutoff(2150, sampleRate_);
    for (int ch = 0; ch < 2; ++ch) {
        echoTone_[ch].setup(sampleRate_);
        chorusTone_[ch].setup(sampleRate_);
    }
    for (real& c : eqCache_) c = -99;             // new sample rate: recompute
    vpSmooth_         = real(1) - std::exp(real(-1) / (sampleRate_ * real(0.001)));
    compDetCoeff_     = real(1) - std::exp(real(-1) / (sampleRate_ * real(0.0091)));
    compAttackCoeff_  = real(1) - std::exp(real(-1) / (sampleRate_ * real(0.0562)));
    compReleaseCoeff_ = real(1) - std::exp(real(-1) / (sampleRate_ * real(0.0112)));
    incRate_ = -1;
    reset();
}

void Mk8Effects::reset() {
    envPower_ = envLevel_ = 0;
    envCounter_ = 0;
    for (int ch = 0; ch < 2; ++ch) {
        lowF_[ch].reset();
        midF_[ch].reset();
        highF_[ch].reset();
        driveLP_[ch].reset();
        echoTone_[ch].reset();
        chorusTone_[ch].reset();
        for (auto& ap : phaser_[ch]) ap.reset();
    }
    vpPhase_ = 0;
    vpPanL_ = vpPanR_ = 0;
    compPow_ = 0;
    compGainDb_ = 0;
    chorusL_.clear();
    chorusR_.clear();
    chorusPhase_ = 0;
    phaserPhase_ = 0;
    phaserAlpha_ = phaserAlphaTarget_ = 0;
    phaserCounter_ = 0;
    delayL_.clear();
    delayR_.clear();
}

// drive stage as measured on the plugin (PLAN §11): input gain, tanh with a small asymmetry
// (even harmonics), first-order low pass at 2150 Hz, output gain
real Mk8Effects::driveStage(real x, mk8fx::OnePoleLP& lp) const {
    constexpr real asym = real(0.055);
    static const real offset = std::tanh(asym);
    return lp.process(std::tanh(x * drivePre_ + asym) - offset) * drivePost_;
}

void Mk8Effects::updateIncrements() {
    if (incRates_[0] == vpHz_ && incRates_[1] == chorusHz_ && incRates_[2] == phaserHz_ &&
        incRate_ == sampleRate_)
        return;
    incRates_[0] = vpHz_;
    incRates_[1] = chorusHz_;
    incRates_[2] = phaserHz_;
    incRate_ = sampleRate_;
    const double fs = double(sampleRate_);
    vpInc_ = phaseIncrement(double(vpHz_), fs);
    chorusInc_ = phaseIncrement(double(chorusHz_), fs);
    phaserInc_ = phaseIncrement(double(phaserHz_), fs);
}

// EQ as measured on the plugin with its dry setting (PLAN §11):
// - bass: first-order shelf around 180 Hz (pole and zero spread symmetrically), 0.93 x the
//   setting;
// - treble: first-order shelf whose lower corner stays at 355 Hz, the upper one moves by the
//   gain;
// - mid: peak with Q 2.2 (cut 2.0) at 93 Hz x (setting / 100 Hz)^1.206, i.e. 93..3445 Hz.
void Mk8Effects::updateEq() {
    if (eqCache_[0] == lowDb_ && eqCache_[1] == midDb_ && eqCache_[2] == midHz_ &&
        eqCache_[3] == highDb_)
        return;
    eqCache_[0] = lowDb_;
    eqCache_[1] = midDb_;
    eqCache_[2] = midHz_;
    eqCache_[3] = highDb_;
    const double fs = double(sampleRate_);
    const double gl = std::pow(10.0, 0.93 * double(lowDb_) / 20.0), sl = std::sqrt(gl);
    for (auto& f : lowF_) f.set(180.0 * sl, 180.0 / sl, 1.0, 180.0, fs);
    const double gh = std::pow(10.0, double(highDb_) / 20.0);
    const double fz = gh >= 1.0 ? 355.0 : 355.0 / gh, fp = gh >= 1.0 ? 355.0 * gh : 355.0;
    for (auto& f : highF_) f.set(fz, fp, fp / fz, std::sqrt(fz * fp), fs);
    midStaticHz_ = real(93.0 * std::pow(double(midHz_) / 100.0, 1.206));
    midQ_ = midDb_ >= 0 ? real(2.2) : real(2.0);
    midFreqHz_ = midStaticHz_;                    // the Envelope starts from the static centre
    for (auto& m : midF_) m.setPeaking(midFreqHz_, sampleRate_, midDb_, midQ_);
}

real Mk8Effects::lfoValue(real phase, int waveform) const {
    switch (waveform) {
        case 1: return real(2) * std::fabs(real(2) * (phase - std::floor(phase + real(0.5)))) - real(1);
        case 2: return real(1) - real(2) * (phase - std::floor(phase));   // falling, as in the plugin
        case 3: return phase - std::floor(phase) < real(0.5) ? real(1) : real(-1);
        default: return sin_.sin01_nofloor(phase);
    }
}

void Mk8Effects::process(float* outL, float* outR, int numFrames) {
    // --- per block: parameters do not change inside a block --------------------------------
    updateEq();
    const bool useEnvelope = envelope_ > real(0.01);

    // compressor as measured on the plugin (PLAN §11): rms detector (9.1 ms) on the mean power
    // of both channels, hard knee, slope = amount (50 % = 2:1, 100 % = limiter), gain smoothed
    // in dB (attack 56 ms, release 11 ms), makeup a plain gain. The plugin's threshold of
    // -58.7 dBFS; the plugin plays 19.7 dB below the engine output (mk8 with vel.gainDb), so
    // here -39.0 dBFS.
    constexpr real thresholdDb = real(-39.0);
    const real compMakeup = db2lin(makeupDb_);

    // drive: on from 1 % (the plugin bypasses it below); input and output gain interpolated
    // between the fitted settings, shifted by the level offset of 19.7 dB between the plugin
    // and the engine output so that it saturates at the same point of the dynamics
    const bool driveOn = drive_ >= real(0.01);
    if (driveOn) {
        static constexpr real kPct[6]  = {1, 10, 25, 50, 75, 100};
        static constexpr real kPre[6]  = {0, 5.0, 23.5, 34.0, 44.3, 56.4};
        static constexpr real kPost[6] = {1.7, -0.6, -15.7, -20.5, -26.7, -35.3};
        const real pct = drive_ * 100;
        int i = 0;
        while (i < 4 && pct > kPct[i + 1]) ++i;
        const real t = std::clamp<real>((pct - kPct[i]) / (kPct[i + 1] - kPct[i]), 0, 1);
        constexpr real levelOffsetDb = real(19.7);
        drivePre_ = db2lin(kPre[i] + t * (kPre[i + 1] - kPre[i]) - levelOffsetDb);
        drivePost_ = db2lin(kPost[i] + t * (kPost[i + 1] - kPost[i]) + levelOffsetDb);
    }

    updateIncrements();
    const bool vpActive = vpOn_ && vpDepth_ > real(0.01);
    constexpr int kVelvetWave[4] = {3, 2, 1, 0};   // plugin square/ramp/tri/sine -> lfoValue()
    const bool chorusActive = chorusOn_ && chorusMs_ > real(0.1);
    const bool delayActive = delayOn_ && delayMix_ > real(0.01);
    const real delaySamples = delaySec_ * sampleRate_;
    const real delayWet = delayMix_ * delayMix_;
    const real delayDry = real(1) - real(0.44) * std::pow(delayMix_, real(2.5));
    const bool phaserActive = phaserOn_ && phaserDepth_ > real(0.01);
    const real vol = volumeGain(volumePct_);

    for (int i = 0; i < numFrames; ++i) {
        real inL = real(outL[i]), inR = real(outR[i]);

        // 1. drive
        if (driveOn) {
            inL = driveStage(inL, driveLP_[0]);
            inR = driveStage(inR, driveLP_[1]);
        }

        // 2. compressor (feed-forward, linked stereo) - ahead of the mid filter like
        //    the hardware: the manual's hearing warning ("the compressor drives the
        //    mid filter to resonance") pins it before the EQ in the real signal path
        if (compOn_) {
            compPow_ += compDetCoeff_ * (real(0.5) * (inL * inL + inR * inR) - compPow_);
            const real overDb = real(0.5) * lin2dB(compPow_ + real(1e-20)) - thresholdDb;
            const real target = overDb > 0 ? -overDb * amount_ : real(0);
            compGainDb_ += (target < compGainDb_ ? compAttackCoeff_ : compReleaseCoeff_) * (target - compGainDb_);
            const real g = db2lin(compGainDb_) * compMakeup;
            inL *= g;
            inR *= g;
        }

        // 3. mid peak. Its level follower runs always, on the signal after the drive and the
        //    compressor as in the plugin (their gain deepens the sweep; the volume comes last).
        //    With the Envelope the centre follows the level in octaves; gain and width stay.
        envPower_ += envPowerCoeff_ * (real(0.5) * (inL * inL + inR * inR) - envPower_);
        if (envPower_ < real(1e-30)) envPower_ = real(1e-30);          // stays normal in float
        const real envAmp = std::sqrt(envPower_);
        envLevel_ += (envAmp > envLevel_ ? envAttackCoeff_ : envReleaseCoeff_) * (envAmp - envLevel_);
        if (useEnvelope && (envCounter_++ & 7) == 0) {   // centre every 8 samples
            const real levelDb = lin2dB(envLevel_) - mk8fx::kPluginLevelDb;
            // loud and high up the sweep would cross the Nyquist frequency, where the biquad is
            // unstable: the centre stays below 0.45 of the sample rate
            const real f = std::min(midStaticHz_ * std::exp2(mk8fx::envelopeOctaves(levelDb, envelope_)),
                                    real(0.45) * sampleRate_);
            if (std::fabs(std::log2(f / midFreqHz_)) > real(1.0 / 1200)) {   // a cent
                midF_[0].setPeaking(f, sampleRate_, midDb_, midQ_);
                midF_[1].setPeaking(f, sampleRate_, midDb_, midQ_);
                midFreqHz_ = f;
            }
        }
        inL = midF_[0].process(inL);
        inR = midF_[1].process(inR);

        // 4. bass and treble shelves
        inL = highF_[0].process(lowF_[0].process(inL));
        inR = highF_[1].process(lowF_[1].process(inR));

        // 5. Vari-Pan as measured on the plugin: the gain in dB follows the LFO, left
        //    -swing (1 - lfo) / 2, right -swing (1 + lfo) / 2, swing 80 dB x depth^2
        //    (100 % mutes each side in turn); 1 ms smoothing against clicks
        if (vpActive) {
            const real lfo = lfoValue(turns(vpPhase_), kVelvetWave[vpWave_]);
            const real swing = real(80) * vpDepth_ * vpDepth_;
            vpPanL_ += vpSmooth_ * (-swing * real(0.5) * (real(1) - lfo) - vpPanL_);
            vpPanR_ += vpSmooth_ * (-swing * real(0.5) * (real(1) + lfo) - vpPanR_);
            inL *= db2lin(vpPanL_);
            inR *= db2lin(vpPanR_);
            vpPhase_ += vpInc_;
        }

        // 6. chorus as measured on the plugin: delay 1.5 ms + D (1 +- sin), D = 1.1 x depth,
        //    left and right in opposite phase; the wet signal passes the bucket-brigade tone
        //    filter; dry 0.71, wet 0.47
        if (chorusActive) {
            const real lfo = sin_.sin01_nofloor(turns(chorusPhase_));
            const real d = real(1.1) * chorusMs_, toSamples = sampleRate_ / real(1000);
            chorusL_.write(inL);
            chorusR_.write(inR);
            const real wetL = chorusL_.read((real(1.5) + d * (real(1) + lfo)) * toSamples);
            const real wetR = chorusR_.read((real(1.5) + d * (real(1) - lfo)) * toSamples);
            inL = inL * real(0.71) + chorusTone_[0].process(wetL) * real(0.47);
            inR = inR * real(0.71) + chorusTone_[1].process(wetR) * real(0.47);
            chorusPhase_ += chorusInc_;
        }

        // 7. phaser as measured on the plugin: 4 first-order all-pass stages, no feedback,
        //    50 % mix; the corner sweeps as a triangle in octaves around 500 Hz,
        //    +-2.9 octaves x depth^2 (notches at 0.414 and 2.414 x the corner)
        if (phaserActive) {
            phaserPhase_ += phaserInc_;
            if ((phaserCounter_++ % 32) == 0) {     // alpha every 32 samples, smoothed
                const real tri = lfoValue(turns(phaserPhase_), 1);
                const real f = real(500) * std::exp2(real(2.9) * phaserDepth_ * phaserDepth_ * tri);
                const real t = fastTan(real(kPi) * std::min(f, real(0.2) * sampleRate_) / sampleRate_);
                phaserAlphaTarget_ = (t - 1) / (t + 1);
            }
            phaserAlpha_ += real(0.05) * (phaserAlphaTarget_ - phaserAlpha_);
            real curL = inL, curR = inR;
            for (int s = 0; s < 4; ++s) {
                curL = phaser_[0][s].process(curL, phaserAlpha_);
                curR = phaser_[1][s].process(curR, phaserAlpha_);
            }
            inL = real(0.5) * (inL + curL);
            inR = real(0.5) * (inR + curR);
        }

        // 8. delay as measured on the plugin: read, then write input plus feedback. The echoes
        //    pass the bucket-brigade tone filter on the way out, so repeats do not darken.
        //    Ping-pong: the mono input enters the left line, the left output feeds the right
        //    line, and the feedback acts once per round trip (echo 2 as loud as echo 1).
        //    Mix: wet gain mix^2, the dry signal stays almost (1 - 0.44 mix^2.5).
        if (delayActive) {
            const real wetL = delayL_.read(delaySamples), wetR = delayR_.read(delaySamples);
            if (delayPingPong_) {
                delayL_.write(real(0.5) * (inL + inR) + wetR * delayFeedback_ * delayFeedback_);
                delayR_.write(wetL);
            } else {
                delayL_.write(inL + wetL * delayFeedback_);
                delayR_.write(inR + wetR * delayFeedback_);
            }
            inL = inL * delayDry + echoTone_[0].process(wetL) * delayWet;
            inR = inR * delayDry + echoTone_[1].process(wetR) * delayWet;
        }

        // 9. volume and soft limiter
        outL[i] = float(softLimiter(inL * vol));
        outR[i] = float(softLimiter(inR * vol));
    }
}

} // namespace pmp
