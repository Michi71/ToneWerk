// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.cpp - keys, pedal, output stage and auto-voicing
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "denormals.h"

namespace pmp {

Engine::Engine() { prepare(); }

Engine::Engine(const Params& params) : params_(params) { prepare(); }

Engine::Engine(const Params& params, double fs, const KeyConfig* keys) : params_(params) {
    prepare(fs, keys);
}

void Engine::setParams(const Params& params) { params_ = params; }

void Engine::refreshChain() { setupChain(); }

void Engine::setSampleRate(double fs) {
    fs_ = fs;
    prepare();
}

double Engine::hammerVelocity(int velocity) const {
    const double x = std::min(std::max(velocity, 0), 127) / 127.0;
    if (params_.velSpeed.n > 0)
        return std::max(double(params_.velSpeed(float(std::min(std::max(velocity, 0), 127)))), 1e-4);
    return params_.velMin + (params_.velMax - params_.velMin) * std::pow(x, double(params_.velGamma));
}

// A fixed Gaussian number per (seed, key, stream): the same for every prepare and every build, no state. The sum of
// four 16-bit draws of a splitmix64 hash approximates N(0, 1) up to +-3.5 sigma, like Engine::gaussian().
static double keyGaussian(double seed, int key, int stream) {
    auto mix = [](uint64_t z) {
        z += 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    const uint64_t r = mix(mix(uint64_t(std::max(seed, 0.0)) + 1) ^ (uint64_t(key) * 0xD1B54A32D192ED03ULL + uint64_t(stream) * 0x8CB92BA72F3D8DD7ULL));
    const double s = double(r & 0xFFFF) + double((r >> 16) & 0xFFFF) + double((r >> 32) & 0xFFFF) + double(r >> 48);
    return (s / 65536.0 - 2.0) * 1.7320508075688772;
}

KeyConfig Engine::makeKeyConfig(int note) const {
    const Params& p = params_;
    const Voicing& v = p.voicing;
    const float key = float(note);
    KeyConfig c;

    const double hardness = std::pow(2.0, -std::min(std::max(double(v.hammerHardness), -200.0), 200.0) / 100.0);
    const double massScale  = std::max(1.0 + 0.01 * double(v.hammerMass), 0.05);
    const double strikeScale = std::min(std::max(1.0 + 0.01 * double(v.strikePoint), 0.05), 3.0);
    const double gaugeScale = std::min(std::max(1.0 + 0.01 * double(v.tineGauge), 0.2), 3.0);
    const double gapScale   = std::min(std::max(1.0 + 0.01 * double(v.pickupGap), 0.1), 10.0);
    const double dampScale  = std::max(1.0 + 0.01 * double(v.damperRelease), 0.02);

    TineSpec spec;
    spec.f1            = midiToHz(note, p.a4);
    double tuneCents = p.keysTuneByKey.n ? double(p.keysTuneByKey(key)) : 0.0;     // measured offset of the instrument
    if (p.keysTuneCents != 0.0f)               // this key's own tuning: a fixed offset from the seed
        tuneCents += double(p.keysTuneCents) * keyGaussian(double(p.keysSeed), note, 1);
    if (tuneCents != 0.0) spec.f1 *= std::exp2(tuneCents / 1200.0);
    spec.diameter      = p.tineDiameter * float(gaugeScale);
    spec.youngs        = p.tineYoungs;
    spec.density       = p.tineDensity;
    spec.springMu      = p.springMuByKey.n ? p.springMuByKey(key) : p.springMu;
    spec.springPosRel  = p.springPosByKey.n ? p.springPosByKey(key) : p.springPos;
    spec.strikePosRel  = float(std::min(std::max(double(p.strikePos(key)) * strikeScale, 0.02), 0.5));
    spec.damperPosRel  = p.damperPos;
    spec.tonebarSplit  = p.tonebarSplit;
    spec.tonebarDetune = p.tonebarDetune + v.tonebarDetune;
    spec.elements      = int(p.femElements + 0.5f);
    const double fsInt = fs_ * os_;
    const int maxModes = std::min(std::max(int(p.maxModes + 0.5f), 1), kMaxModes);
    if (p.tineString >= 0.5f) {
        StringSpec s;
        s.f1            = spec.f1;
        s.tension       = p.stringTension;
        s.linearMass    = p.stringMassByKey.n ? double(p.stringMassByKey(key)) : p.stringMass;
        s.diameter      = p.stringCore;
        s.youngs        = p.stringYoungs;
        s.strikePosRel  = float(std::min(std::max(double(p.strikePos(key)) * strikeScale, 0.005), 0.45));
        double pickupPos = p.stringPickupPosByKey.n ? double(p.stringPickupPosByKey(key))
                                                    : double(p.stringPickupPos);
        // the D6 pickup rockers: clav.ab picks the coil-pair set (the B twins of the
        // curves), clav.cd moves from the solo positions to the parallel ones (docs/
        // CLAVINET.md 2.1 - A+D in phase, B+D out of phase, per the restored schematic)
        const bool setB = p.clavAb > 0.5f;
        const Curve& meters1 = (setB && p.stringPickupMetersByKeyB.n) ? p.stringPickupMetersByKeyB
                                                                     : p.stringPickupMetersByKey;
        const Curve& meters2 = (setB && p.stringPickup2MetersByKeyB.n) ? p.stringPickup2MetersByKeyB
                                                                      : p.stringPickup2MetersByKey;
        if (meters1.n || p.stringPickupMeters > 0.0f) {
            // a constant distance from the far end (the bridge at 4 cm), converted with the
            // designed length L = sqrt(T/mu) / 2 f1
            const double mu = p.stringMassByKey.n ? double(p.stringMassByKey(key)) : p.stringMass;
            const double L = std::sqrt(p.stringTension / mu) / (2.0 * s.f1);
            const double m = meters1.n ? double(meters1(key)) : double(p.stringPickupMeters);
            pickupPos = 1.0 - m / L;
            if (meters2.n || p.stringPickup2Meters > 0.0f) {
                const double m2 = meters2.n ? double(meters2(key)) : double(p.stringPickup2Meters);
                if (m2 > 0.0) {
                    s.pickup2PosRel = float(std::clamp(1.0 - m2 / L, 0.02, 0.98));
                    s.pickup2Gain   = p.clavCd > 0.5f
                        ? (setB ? -1.0f : 1.0f)
                        : (setB ? p.stringPickup2GainB : p.stringPickup2Gain);
                }
            }
        }
        s.pickupPosRel  = float(std::min(std::max(pickupPos, 0.02), 0.98));
        s.inharmB       = p.stringBByKey.n ? double(p.stringBByKey(key)) : p.stringB;
        s.deadModes     = int(p.stringDeadModes + 0.5f);
        s.deadGain      = p.stringDeadGain;
        c.modes = designString(s, std::min(0.45 * fsInt, 20000.0), maxModes);
    } else {
        c.modes = designTine(spec, std::min(0.45 * fsInt, 20000.0), maxModes);
    }

    // losses: measured fundamental decay, higher modes decay faster, tonebar a bit slower
    const double f1 = spec.f1;
    const double t60A = (p.clavAb > 0.5f && p.decayT60B.n) ? double(p.decayT60B(key)) : double(p.t60(key));
    const double sigma1 = t60ToSigma(std::max(t60A, 0.05));
    const double sigmaBar = sigma1 * p.tonebarLoss;
    for (int m = 0; m < c.modes.count; ++m) {
        const double f = c.modes.freq[m];
        if (f < 0.9 * spec.f1 && p.tineString >= 0.5f && p.stringDeadT60 > 0.0f) {
            // whole-string mode: the yarn damps it, the loss law of the speaking ladder
            // does not apply
            c.sigmaFree[m] = t60ToSigma(std::max(double(p.stringDeadT60), 0.01));
            continue;
        }
        const double hf2 = p.lossHf2ByKey.n ? p.lossHf2ByKey(key) : p.lossHf2;
        const double lossHf = p.lossHfByKey.n ? double(p.lossHfByKey(key)) : double(p.lossHf);
        const double lossExp = p.lossExpByKey.n ? double(p.lossExpByKey(key)) : double(p.lossExp);
        const double s1 = sigma1;
        double sigT = s1 + lossHf * (std::pow(f, lossExp) - std::pow(f1, lossExp)) +
                      hf2 * (f * f - f1 * f1);
        sigT = std::max(sigT, 0.5 * s1);
        const double share = std::min(std::max(c.modes.tonebarShare[m], 0.0), 1.0);
        c.sigmaFree[m] = (1.0 - share) * sigT + share * sigmaBar;
    }

    // damper: velocity-proportional felt contact, extra decay ~ phi(damper)^2 per mode,
    // scaled so that the fundamental decays with the release T60. The pressed felt also
    // damps the whole tine/tonebar/harp assembly: a uniform term (damper.harpT60) keeps
    // the residual falling instead of ringing on at the free decay (PLAN K11/K4).
    const int f0 = c.modes.fundamental;
    const double harpT60 = p.damperHarpT60.n ? double(p.damperHarpT60(key)) : 0.0;
    const double harpSigma = harpT60 > 0.0 ? t60ToSigma(harpT60) : 0.0;
    const double sigma0 = c.sigmaFree[f0];
    const double dSig = std::max(t60ToSigma(std::max(double(p.releaseT60(key)) * dampScale, 0.01)) - sigma0 - harpSigma, 0.0);
    const double phiD = c.modes.phiDamper[f0];
    const double cd = (phiD != 0.0) ? 2.0 * dSig / (phiD * phiD) : 0.0;
    for (int m = 0; m < c.modes.count; ++m)
        c.sigmaDamper[m] = 0.5 * cd * c.modes.phiDamper[m] * c.modes.phiDamper[m] + harpSigma;
    c.damperRampSec = p.damperRampMs * 1e-3;

    // hammer: stiffness from the contact time against a rigid wall at 1 m/s
    //   T = 2 dmax / v * I(a),   dmax = ((a+1) m v^2 / (2k))^(1/(a+1))
    c.hammerMass     = std::max(double(p.hammerMass(key)) * massScale, 1e-4);
    c.hammerAlpha    = std::max(double(p.hammerAlpha(key)), 1.0);
    c.hammerLambda   = p.hammerLambda;
    c.hammerSubsteps = std::max(1, int(p.hammerSubsteps + 0.5f));
    c.hammerSpeedHardening = p.hammerSpeedHardening + p.hammerSpeedHardeningByKey(key);
    const double a  = c.hammerAlpha, n = a + 1.0;
    const double ia = std::sqrt(kPi) * std::tgamma(1.0 + 1.0 / n) / std::tgamma(0.5 + 1.0 / n);
    const double dmax = std::max(double(p.contactMs(key)) * hardness, 0.01) * 1e-3 / (2.0 * ia);
    c.hammerK = n * c.hammerMass / (2.0 * std::pow(dmax, n));

    // pickup
    const double w = std::max(double(p.pickupWidth(key)) * gapScale, 0.01) * 1e-3;
    c.pickupWidth    = w;
    c.pickupOffset   = (p.pickupOffset(key) + v.pickupOffset + voicing_) * w;
    c.pickupExponent = p.pickupExponent;
    c.pickupType     = int(std::min(std::max(p.pickupType + 0.5f, 0.0f), 4.0f));
    c.pickupSection  = note <= 46 ? 0 : note <= 52 ? 1 : note <= 74 ? 2 : 3;       // reeds 1-14, 15-20, 21-42, 43-64 (MIDI 33..96)
    c.pickupHpfHz    = double(p.pickupHpfHz);
    c.pickupOnsetHz  = p.pickupOnsetMul.n ? double(p.pickupOnsetMul(key)) * midiToHz(note) : 0.0;
    c.pickupOnsetMs  = double(p.pickupOnsetMs);
    c.reedGlide      = p.reedGlide.n ? double(p.reedGlide(key)) : 0.0;

    c.gain = 1.0;
    c.sleepEnergy = 1e-16;
    return c;
}

// Key data this engine may write. Started from a table, it copies the table first, so the
// caller's data (possibly in flash) is never modified.
KeyConfig* Engine::ownKeys() {
    if (!own_) {
        own_.reset(new KeyConfig[kNumKeys]);
        if (keys_) std::copy(keys_, keys_ + kNumKeys, own_.get());
    }
    keys_ = own_.get();
    return own_.get();
}

void Engine::prepare(bool autoVoiceKeys) {
    os_ = params_.oversampling >= 1.5f ? 2 : 1;
    KeyConfig* keys = ownKeys();
    for (int k = 0; k < kNumKeys; ++k) keys[k] = makeKeyConfig(kFirstKey + k);
    setupVoices();
    if (autoVoiceKeys) autoVoice();
}

void Engine::prepare(double fs, const KeyConfig* keys) {
    fs_ = fs;
    os_ = params_.oversampling >= 1.5f ? 2 : 1;
    keys_ = keys;
    if (keys != own_.get()) own_.reset();
    voicing_ = 0.0;
    setupVoices();
}

void Engine::setupVoices() {
    const double fsInt = fs_ * os_;
    for (int k = 0; k < kNumKeys; ++k) voices_[k].configure(keys_[k], fsInt);
    applyMechScales();
    if (params_.humanSeed != rngSeed_) {      // seeded once and again when the seed changes, not on every prepare
        rng_ = 0x9E3779B97F4A7C15ULL * (uint64_t(std::max(params_.humanSeed, 0.0f)) + 1);
        rngSeed_ = params_.humanSeed;
    }
    keyDown_.fill(false);
    listed_.fill(false);
    activeCount_ = 0;
    sustain_ = false;
    pedalPos_ = 0;

    lpf_.lowpass(params_.lpfHz, params_.lpfQ, fsInt);
    lpf_.reset();
    decimator_.design(135, 90.0);
    hpf_.setup(params_.hpfHz, fs_);
    hpf_.reset();
    setupChain();
    updateHybrid();
}

void Engine::applyMechScales() {
    // an empty curve is 0 dB for every key: the scale is exactly 1 and nothing changes
    for (int k = 0; k < kNumKeys; ++k) {
        voices_[k].setMechScale(std::pow(10.0, double(params_.mechByKey(float(kFirstKey + k))) / 20.0));
        voices_[k].setClickScale(std::pow(10.0, double(params_.clickByKey(float(kFirstKey + k))) / 20.0));
    }
}

// Pedal clunk (docs/MK8_PEDAL.md): decaying noise, 20..120 Hz (down) or 40..190 Hz (up) with a steep fall above
// (the plugin's pedal noise has no lines); [0] pedal down, [1] pedal up
constexpr double kPedHpHz[2] = {20.0, 40.0}, kPedHpQ[2] = {1.0, 1.4};
constexpr double kPedLpHz[2] = {120.0, 190.0}, kPedLpQ[2] = {1.4, 4.0};
constexpr double kPedT60 = 0.5;
constexpr double kPedUnitRms[2] = {0.03740, 0.08342};    // RMS over the first 300 ms per unit of noise amplitude (mean over 40 seeds, dry)

double Engine::gaussian() {
    // xorshift64*; four 16-bit draws summed: mean 2, variance 1/3 -> N(0, 1) up to +-3.5 sigma
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    const uint64_t r = rng_ * 0x2545F4914F6CDD1DULL;
    const double s = double(r & 0xFFFF) + double((r >> 16) & 0xFFFF) + double((r >> 32) & 0xFFFF) + double(r >> 48);
    return (s / 65536.0 - 2.0) * 1.7320508075688772;
}

void Engine::setupChain() {
    const Params& p = params_;
    for (int k = 0; k < kNumKeys; ++k) {      // the force laws of the knock and the click (a prepareKey() applies them too)
        voices_[k].setOnsetVel(double(p.pickupOnsetVel));
        voices_[k].setForceLevelled(p.mechLevelled > 0.5f);
        voices_[k].setSaturation(p.ampStage > 0.5f && p.ampPerVoice > 0.5f && p.ampDrive > 0.0f ? dbToGain(p.ampDrive) : 0.0);
        const float key = float(kFirstKey + k);
        voices_[k].setDamperKnock(p.damperKnock == 0.0f ? 0.0 : keys_[k].hammerMass * double(p.damperKnock) *
                                  std::pow(10.0, double(p.damperKnockByKey(key)) / 20.0),
                                  double(p.damperKnockPeriods));
        voices_[k].setClickExponent(std::max(double(p.clickExpByKey.n ? p.clickExpByKey(key) : p.clickExp), 0.5));
        voices_[k].setMechExponent(std::max(double(p.mechExpByKey.n ? p.mechExpByKey(key) : p.mechExp), 0.5));
    }
    const bool stage = p.ampStage > 0.5f;                  // amp.on: tone, power amp and cabinet in or out of the path
    const bool setB = p.clavAb > 0.5f;                      // the D6 pickup rocker set
    const float resHz = setB && p.pickupResHzB > 0.0f ? p.pickupResHzB : p.pickupResHz;
    const float resQ  = setB && p.pickupResHzB > 0.0f ? p.pickupResQB  : p.pickupResQ;
    const float resDb = setB && p.pickupResHzB > 0.0f ? p.pickupResDbB : p.pickupResDb;
    pickupResOn_ = resHz > 0.0f && resDb != 0.0f;
    if (pickupResOn_) {
        pickupRes_.peaking(resHz, resQ, resDb, fs_);
        pickupRes_.reset();
    }
    clavTone_.setup(fs_, p.clavSoft > 0.5f, p.clavMedium > 0.5f,
                    p.clavTreble > 0.5f, p.clavBrilliant > 0.5f);
    toneLowOn_ = stage && p.toneBassDb != 0.0f;
    toneHighOn_ = stage && p.toneTrebleDb != 0.0f;
    spkLowOn_ = stage && p.spkLowHz > 0.0f;
    spkHighOn_ = stage && p.spkHighHz > 0.0f;
    toneLow_.lowShelf(150.0, p.toneBassDb, fs_);
    toneHigh_.highShelf(3000.0, p.toneTrebleDb, fs_);
    spkLow_.highpass(std::max(double(p.spkLowHz), 1.0), std::max(double(p.spkLowQ), 0.1), fs_);
    spkHigh_.lowpass(std::max(double(p.spkHighHz), 100.0), std::max(double(p.spkHighQ), 0.1), fs_);
    for (Biquad* b : {&toneLow_, &toneHigh_, &spkLow_, &spkHigh_}) b->reset();
    ampOn_ = stage && p.ampDrive > 0.0f && p.ampPerVoice <= 0.5f;     // per voice: the sum stays linear
    ampKnockOn_ = stage && p.ampDrive > 0.0f && p.ampPerVoice > 0.5f;
    amp_.setup(real(dbToGain(std::max(p.ampDrive, 0.0f))),
               real(std::min(std::max(double(p.ampAsym), 0.0), 0.5)));
    // Harp/action modes: velocity response of a damped mode to the force, unit gain at
    // resonance; the gain is the coupling.
    const float mhz[kMechModes] = {p.mech1Hz, p.mech2Hz, p.mech3Hz};
    const float mt60[kMechModes] = {p.mech1T60, p.mech2T60, p.mech3T60};
    const float mg[kMechModes] = {p.mech1Gain, p.mech2Gain, p.mech3Gain};
    mechOn_ = false;
    for (int j = 0; j < kMechModes; ++j) {
        const double f = std::min(std::max(double(mhz[j]), 10.0), 0.4 * fs_);
        mech_[j].setup(f, std::max(double(mt60[j]), 0.001), fs_);
        mech_[j].reset();
        mechGain_[j] = real(mg[j]);
        mechOn_ = mechOn_ || mg[j] != 0.0f;
    }
    trim_ = real(dbToGain(double(p.trimDb)));
    clickOn_ = p.clickGain != 0.0f;
    clickGain_ = real(p.clickGain);
    clickHp_.highpass(std::max(double(setB && p.clickHpHzB >= 0.0f ? p.clickHpHzB : p.clickHpHz), 20.0),
                      0.7071, fs_);
    clickLp_.lowpass(std::min(std::max(double(p.clickLpHz), 200.0), 0.45 * fs_), 0.7071, fs_);
    clickHp_.reset();
    clickLp_.reset();
    // release thump: the modes and the peak of a unit blow (the output of the mode is 2 sigma dq/dt)
    const float rhz[kRelModes] = {p.rel1Hz, p.rel2Hz};
    const float rt60[kRelModes] = {p.rel1T60, p.rel2T60};
    const float rg[kRelModes] = {p.rel1Gain, p.rel2Gain};
    const float rdl[kRelModes] = {p.rel1DelayMs, p.rel2DelayMs};
    relOn_ = false;
    relCount_ = relRing_ = relRingMax_ = 0;
    for (int j = 0; j < kRelModes; ++j) {
        const double f = std::min(std::max(double(rhz[j]), 10.0), 0.4 * fs_);
        rel_[j].setup(f, std::max(double(rt60[j]), 0.001), fs_);
        rel_[j].reset();
        ForcedMode probe = rel_[j];
        real peak = 0;
        const int n = int(std::min(4.0 * fs_ / f, 20.0 * fs_));
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(probe.process(i == 0 ? real(1) : real(0))));
        relNorm_[j] = peak > 0 ? real(1) / peak : real(0);
        relGain_[j] = real(rg[j]) * real(p.voiceRms);
        relDelay_[j] = int(std::max(double(rdl[j]), 0.0) * 1e-3 * fs_ + 0.5);
        relOn_ = relOn_ || rg[j] != 0.0f;
        relRingMax_ = std::max(relRingMax_, int((double(rt60[j]) * 1.2 + 0.1) * fs_) + relDelay_[j]);
    }
    // pedal clunk: band filters and decay; the gain is the RMS of the clunk over its first 300 ms relative to the
    // voice level
    pedOn_ = p.pedalDownGain != 0.0f || p.pedalUpGain != 0.0f;
    for (int w = 0; w < 2; ++w) {
        pedHp_[w].highpass(kPedHpHz[w], kPedHpQ[w], fs_);
        pedLp_[w].lowpass(std::min(kPedLpHz[w], 0.4 * fs_), kPedLpQ[w], fs_);
        pedHp_[w].reset();
        pedLp_[w].reset();
    }
    pedDecay_ = real(std::pow(10.0, -3.0 / (kPedT60 * fs_)));
    pedRingMax_ = int(1.6 * kPedT60 * fs_);            // -96 dB
    pedEnv_ = 0;
    pedRing_ = 0;
    pedGain_[0] = real(p.pedalDownGain) * real(p.voiceRms) / real(kPedUnitRms[0]);
    pedGain_[1] = real(p.pedalUpGain) * real(p.voiceRms) / real(kPedUnitRms[1]);
    // the damper woosh (hybrid grand, P6)
    wooshOn_ = p.damperWooshGain != 0.0f;
    wooshHp_.highpass(std::min(std::max(double(p.damperWooshHz), 20.0), 0.45 * fs_), 0.707, fs_);
    wooshHp_.reset();
    {
        const double t60 = std::max(double(p.damperWooshT60), 0.002);
        wooshDecay_ = real(std::pow(10.0, -3.0 / (t60 * fs_)));
        wooshRingMax_ = int(1.6 * t60 * fs_);
    }
    wooshDelay_ = int(std::max(double(p.damperWooshDelayMs), 0.0) * 1e-3 * fs_ + 0.5);
    wooshGain_ = real(p.damperWooshGain) * real(p.voiceRms);
    wooshEnv_ = 0;
    wooshRing_ = 0;
    wooshDue_ = -1;
    detune_.setup(p.detuneCents, p.detuneWinMs, fs_);
    fxOn_ = p.fxOn > 0.5f;
    if (fxOn_) {
        if (!fx_) fx_.reset(new Mk8Effects());
        Mk8Effects& fx = *fx_;
        fx.setVolume(p.fxVolume);       fx.setDrive(p.fxDrive);
        fx.setEnvelope(p.fxEnvelope);
        fx.setBass(p.fxBass);           fx.setMiddle(p.fxMid);
        fx.setMiddleFreq(p.fxMidFreq);  fx.setTreble(p.fxTreble);
        fx.setPanOn(p.fxPanOn > 0.5f);  fx.setPanWave(int(p.fxPanWave + 0.5f));
        fx.setPanRate(p.fxPanRate);     fx.setPanDepth(p.fxPanDepth);
        fx.setCompressorOn(p.fxCompOn > 0.5f);
        fx.setCompAmount(p.fxCompAmount);
        fx.setCompMakeup(p.fxCompMakeup);
        fx.setChorusOn(p.fxChorusOn > 0.5f);
        fx.setChorusRate(p.fxChorusRate);
        fx.setChorusDepth(p.fxChorusDepth);
        fx.setPhaserOn(p.fxPhaserOn > 0.5f);
        fx.setPhaserRate(p.fxPhaserRate);
        fx.setPhaserDepth(p.fxPhaserDepth);
        fx.setDelayOn(p.fxDelayOn > 0.5f);
        fx.setDelayTime(p.fxDelayTime);
        fx.setDelayFeedback(p.fxDelayFeedback);
        fx.setDelayMix(p.fxDelayMix);
        fx.setDelayPingPong(p.fxDelayPingPong > 0.5f);
        fx.init(real(fs_));             // allocates the delay lines, resets the states
    }
    detuneMix_ = real(p.detuneMix);
    vibInc_ = uint64_t(std::llround(std::min(std::fabs(double(p.vibRateHz)) / fs_, 0.25) * 0x1p64));
    vibTarget_ = vibDepth_ = real(std::min(std::max(double(p.vibDepth), 0.0), 1.0));
    vibSmooth_ = real(1.0 - std::exp(-1.0 / (0.01 * fs_)));    // 10 ms glide
    vibPhase_ = 0;
    tremInc_ = uint64_t(std::llround(std::min(std::fabs(double(p.tremRateHz)) / fs_, 0.25) * 0x1p64));
    tremTarget_ = tremDepth_ = real(std::min(std::max(double(p.tremDepth), 0.0), 1.0));
    tremSmooth_ = vibSmooth_;
    tremPhase_ = 0;
}

real Engine::clickNoise() {
    // xorshift64*: top 24 bits -> uniform in [-1, 1), scaled to unit variance (sqrt 3)
    clickRng_ ^= clickRng_ >> 12;
    clickRng_ ^= clickRng_ << 25;
    clickRng_ ^= clickRng_ >> 27;
    const uint64_t r = clickRng_ * 0x2545F4914F6CDD1DULL;
    return real((double(r >> 40) / 8388608.0 - 1.0) * 1.7320508075688772);
}

void Engine::setVibratoDepth(double d) { vibTarget_ = real(std::min(std::max(d, 0.0), 1.0)); }

void Engine::setTremoloDepth(double d) { tremTarget_ = real(std::min(std::max(d, 0.0), 1.0)); }

bool Engine::prepareKey(int note, bool autoVoiceKey) {
    if (!inRange(note)) return false;
    os_ = params_.oversampling >= 1.5f ? 2 : 1;
    const double fsInt = fs_ * os_;
    const int k = note - kFirstKey;
    KeyConfig* keys = ownKeys();
    keys[k] = makeKeyConfig(note);
    voices_[k].configure(keys[k], fsInt);
    lpf_.lowpass(params_.lpfHz, params_.lpfQ, fsInt);
    lpf_.reset();
    decimator_.design(135, 90.0);
    hpf_.setup(params_.hpfHz, fs_);
    hpf_.reset();
    setupChain();
    if (autoVoiceKey) autoVoice(k);
    return keys[k].modes.count > 0;
}

// Like a technician: strike every key at velocity 100 and balance the output level.
void Engine::autoVoice(int only) {
    ScopedFlushDenormals noDenormals;            // decayed high modes: float denormals are slow
    const double fsInt = fs_ * os_;
    const int nTotal = int(0.30 * fsInt), n0 = int(0.03 * fsInt), nEnergy = int(0.05 * fsInt);
    std::vector<float> buf(size_t(nTotal), 0.0f);
    const double v100 = hammerVelocity(100);
    KeyConfig* keys = ownKeys();
    for (int k = 0; k < kNumKeys; ++k) {
        if (only >= 0 && k != only) continue;
        KeyConfig c = keys[k];
        c.gain = 1.0;
        c.sleepEnergy = 0.0;
        KeyVoice v;
        v.configure(c, fsInt);
        v.setDamper(false);
        v.strike(v100);
        std::fill(buf.begin(), buf.end(), 0.0f);
        double eMax = 0.0;
        for (int i = 0; i < nTotal; i += 64) {
            const int m = std::min(64, nTotal - i);
            v.process(&buf[size_t(i)], m);
            if (i < nEnergy) eMax = std::max(eMax, v.energy());
        }
        double sum = 0.0;
        for (int i = n0; i < nTotal; ++i) sum += double(buf[size_t(i)]) * buf[size_t(i)];
        const double rms = std::sqrt(sum / (nTotal - n0));
        double gain = rms > 0.0
            ? params_.voiceRms / rms * dbToGain(params_.keyGainDb(float(kFirstKey + k))) : 1.0;
        if (params_.keysLevelDb != 0.0f)         // this key's own loudness: a fixed offset from the seed
            gain *= dbToGain(double(params_.keysLevelDb) * keyGaussian(double(params_.keysSeed), kFirstKey + k, 2));
        keys[k].gain = gain;
        keys[k].sleepEnergy = eMax * std::pow(10.0, -params_.sleepDb / 10.0);
        voices_[k].setGain(gain);
        voices_[k].setSleepEnergy(keys[k].sleepEnergy);
    }
}

void Engine::activate(int k) {
    if (listed_[k]) return;
    listed_[k] = true;
    active_[activeCount_++] = k;
}

int Engine::maxVoices() const {
    const int n = int(params_.maxVoices + 0.5f);
    return n <= 0 ? kNumKeys : std::min(n, kNumKeys);
}

// Polyphony limit: when a new key would exceed num.maxVoices, the quietest sounding voice
// is choked (it fades within a few ms), preferably one whose key is up and not sustained.
// Choking voices do not count, but at most kMaxChoking of them run: beyond that the one
// closest to its end is cut, so the CPU load stays bounded by maxVoices + kMaxChoking.
void Engine::makeRoom(int k) {
    const int limit = maxVoices();
    if (limit >= kNumKeys) return;
    for (;;) {                                // usually at most one pass steals
        int playing = 0, choking = 0, victim = -1, nearlyDone = -1;
        bool victimReleased = false;
        real victimLevel = 0, doneLevel = 0;
        for (int a = 0; a < activeCount_; ++a) {
            const int j = active_[a];
            if (j == k || !voices_[j].active()) continue;
            const real level = voices_[j].level();
            if (voices_[j].choking()) {
                ++choking;
                if (nearlyDone < 0 || level < doneLevel) {
                    nearlyDone = j;
                    doneLevel = level;
                }
                continue;
            }
            ++playing;
            const bool released = !keyDown_[j] && !sustain_;
            if (victim < 0 || (released && !victimReleased) ||
                (released == victimReleased && level < victimLevel)) {
                victim = j;
                victimReleased = released;
                victimLevel = level;
            }
        }
        if (playing < limit || victim < 0) return;
        if (choking >= kMaxChoking && nearlyDone >= 0) voices_[nearlyDone].reset();
        voices_[victim].choke();
    }
}

void Engine::noteOn(int note, int velocity) {
    if (!inRange(note)) return;
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    if (hybridActive_) {                          // the hybrid voice plays: the physical voices stay silent
        hybrid()->setMaxKeys(maxVoices() < kNumKeys ? maxVoices() : 0);   // num.maxVoices, as it is now
        hybrid()->noteOn(note, velocity);
        keyDown_[note - kFirstKey] = true;        // for the release noises (P6)
        return;
    }
    const int k = note - kFirstKey;
    if (!voices_[k].active() || voices_[k].choking()) makeRoom(k);   // a new voice
    keyDown_[k] = true;
    voices_[k].setDamper(false);
    voices_[k].setRetune(0.0);                          // a fresh strike traps the full string
    const int v = std::min(velocity, 127);
    double db = params_.velGainDb.n > 0 ? double(params_.velGainDb(float(v))) : 0.0;
    if (params_.velGainSlope.n > 0) db += double(params_.velGainSlope(float(note))) * (v - 127);
    const double level = db != 0.0 ? std::pow(10.0, db / 20.0) : 1.0;
    if (params_.damperVelDb.n > 0)
        voices_[k].setDamperScale(std::pow(10.0,
            double(params_.damperVelDb(float(v))) / 20.0));
    double speed = hammerVelocity(velocity), contact = 1.0;
    if (params_.humanSpeed != 0.0f) speed *= std::exp(double(params_.humanSpeed) * gaussian());
    if (params_.humanContact != 0.0f) contact = std::exp(double(params_.humanContact) * gaussian());
    voices_[k].strike(speed, level, contact);
    activate(k);
}

real Engine::pedalDamperFraction() const {
    const real lost = std::clamp(real(params_.damperPedalLost), real(0), real(0.95f));
    const real full = std::max(std::clamp(real(params_.damperPedalFull), real(0.05f), real(1)),
                               lost + real(0.05f));
    if (pedalPos_ <= lost) return real(1);            // dampers fully on
    if (pedalPos_ >= full) return real(0);            // lifted
    const real t = (pedalPos_ - lost) / (full - lost);
    const real lift = t * t * (real(3) - real(2) * t);   // smoothstep, C1 at both ends
    return real(1) - lift;
}

void Engine::noteOff(int note) {
    if (!inRange(note)) return;
    if (hybridActive_) {
        hybrid()->noteOff(note);
        // the noises of the release (P6): the damper lands as far as the pedal lets it, on keys that have one
        const int kh = note - kFirstKey;
        const bool wasDownH = keyDown_[kh];
        keyDown_[kh] = false;
        const real fractionH = pedalDamperFraction();
        const bool hasDamper = !hyb_[hybSlot_].bank || hyb_[hybSlot_].bank->key(note).damped != 0;
        if (wasDownH && fractionH >= real(1) && params_.stringReleaseSemitones > 0.0f)
            hybrid()->setRetune(note, double(params_.stringReleaseSemitones));   // the clavinet: the tangent lifts, the whole string speaks
        if (wasDownH && fractionH > 0 && hasDamper) {
            if (relOn_) scheduleRelease(kh, fractionH);
            if (wooshOn_) kickWoosh(fractionH);
        }
        return;
    }
    const int k = note - kFirstKey;
    const bool wasDown = keyDown_[k];
    keyDown_[k] = false;
    const real fraction = pedalDamperFraction();
    voices_[k].setDamperFraction(fraction);                // half pedal aware
    if (wasDown && fraction >= real(1) && params_.stringReleaseSemitones > 0.0f)
        voices_[k].setRetune(double(params_.stringReleaseSemitones));   // the tangent lifts, the whole string speaks
    if (wasDown && fraction > 0) {
        voices_[k].damperHit(fraction);                    // wakes a silent reed: it has to be in the list of the sounding keys
        if (voices_[k].active()) activate(k);
    }
    if (relOn_ && wasDown && fraction > 0) scheduleRelease(k, fraction);
}

// The damper lands on the tine and the key on its stop: each release strikes the two thump modes once
// (not with the pedal holding the dampers off). The blow is the same for every velocity and key apart
// from the by-key curves; human.release scatters its size.
void Engine::scheduleRelease(int k, real fraction) {
    const real key = real(kFirstKey + k);
    const Curve* byKey[kRelModes] = {&params_.rel1ByKey, &params_.rel2ByKey};
    for (int j = 0; j < kRelModes; ++j) {
        if (relGain_[j] == 0) continue;
        double amp = double(relGain_[j]) * double(relNorm_[j]) * double(fraction);
        if (byKey[j]->n > 0) amp *= std::pow(10.0, double((*byKey[j])(float(key))) / 20.0);
        if (params_.humanRelease != 0.0f) amp *= std::exp(double(params_.humanRelease) * gaussian());
        if (relCount_ == kRelEvents) {                     // full: drop the oldest
            for (int i = 1; i < kRelEvents; ++i) relEv_[i - 1] = relEv_[i];
            --relCount_;
        }
        relEv_[relCount_++] = RelEvent{relDelay_[j], real(amp), j};
    }
    relRing_ = relRingMax_;
}

void Engine::renderRelease(int m) {
    for (int i = 0; i < m; ++i) {
        real f[kRelModes] = {0, 0};
        for (int e = 0; e < relCount_; ++e)
            if (relEv_[e].due == i) f[relEv_[e].mode] += relEv_[e].amp;
        real y = 0;
        for (int j = 0; j < kRelModes; ++j) y += rel_[j].process(f[j]);
        bufOut_[i] += float(y);
    }
    int keep = 0;
    for (int e = 0; e < relCount_; ++e)
        if (relEv_[e].due >= m) { relEv_[keep] = relEv_[e]; relEv_[keep].due -= m; ++keep; }
    relCount_ = keep;
    if (relCount_ == 0) {
        relRing_ -= m;
        if (relRing_ <= 0) { relRing_ = 0; for (ForcedMode& r : rel_) r.reset(); }   // done ringing: no denormal tail
    }
}

// The damper rail goes up or comes down: a decaying noise burst through the band filter (one draw of the seeded
// generator for the size, then one per sample in renderPedal)
void Engine::kickPedal(int which) {
    if (!pedOn_ || pedGain_[which] == 0) return;
    real amp = pedGain_[which];
    if (params_.humanPedal != 0.0f) amp *= real(std::exp(double(params_.humanPedal) * gaussian()));
    pedWhich_ = which;
    pedEnv_ = amp;
    pedRing_ = pedRingMax_;
    pedHp_[which].reset();
    pedLp_[which].reset();
}

// The felt lands on the string: after the delay a decaying high-passed noise burst (hybrid grand, P6); a new release
// restarts the burst
void Engine::kickWoosh(real fraction) {
    real amp = wooshGain_ * fraction;
    if (params_.humanRelease != 0.0f) amp *= real(std::exp(double(params_.humanRelease) * gaussian()));
    wooshPendingAmp_ = amp;
    wooshDue_ = wooshDelay_;
}

void Engine::renderWoosh(int m) {
    for (int i = 0; i < m; ++i) {
        if (wooshDue_ == 0) {
            wooshEnv_ = wooshPendingAmp_;
            wooshRing_ = wooshRingMax_;
            wooshHp_.reset();
        }
        if (wooshDue_ >= 0) --wooshDue_;
        if (wooshRing_ > 0) {
            bufOut_[i] += float(wooshHp_.process1(wooshEnv_ * real(gaussian())));
            wooshEnv_ *= wooshDecay_;
            --wooshRing_;
        }
    }
}

void Engine::renderPedal(int m) {
    for (int i = 0; i < m; ++i) {
        const real y = pedLp_[pedWhich_].process1(pedHp_[pedWhich_].process1(pedEnv_ * real(gaussian())));
        pedEnv_ *= pedDecay_;
        bufOut_[i] += float(y);
    }
    pedRing_ -= m;
    if (pedRing_ <= 0) pedRing_ = 0;
}

void Engine::processMidiController(int cc, int value) {
    switch (cc) {
        case 1:    // mod wheel: move the pickups towards the tines' centre ("bark")
            setVoicing(0.4 * value / 127.0);
            break;
        case 7:
            volume_ = real(value / 127.0);
            break;
        case 64: {
            // half pedal: the position is continuous; the dampers of released keys
            // follow the rail with its lost motion (damper.pedalLost .. pedalFull)
            if (hybridActive_) hybrid()->controller(64, value);
            pedalPos_ = real(value) / 127;
            const bool wasSustain = sustain_;
            sustain_ = pedalPos_ >= real(0.5);
            if (sustain_ != wasSustain) kickPedal(sustain_ ? 0 : 1);
            const real f = pedalDamperFraction();
            for (int k = 0; k < kNumKeys; ++k)
                if (!keyDown_[k]) voices_[k].setDamperFraction(f);
            break;
        }
        case 120:
            if (hybridActive_) hybrid()->allSoundOff();     // a 5 ms fade instead of the hard cut
            else allSoundOff();
            break;
        case 123:
            allNotesOff();
            break;
        default:
            break;
    }
}

void Engine::allNotesOff() {
    for (HybridSlot& h : hyb_)
        if (h.player) h.player->controller(123, 0);
    sustain_ = false;
    pedalPos_ = 0;
    for (int k = 0; k < kNumKeys; ++k) {
        keyDown_[k] = false;
        voices_[k].setDamper(true);
    }
}

void Engine::allSoundOff() {
    allNotesOff();
    for (HybridSlot& h : hyb_)                    // hard: the engine renders like a fresh one afterwards
        if (h.player) h.player->reset();
    for (int k = 0; k < kNumKeys; ++k) {
        voices_[k].reset();
        listed_[k] = false;
    }
    activeCount_ = 0;
    resetOutput();
}

// Silences the filter and effect tails too: after allSoundOff() the engine renders exactly
// like a freshly prepared one (the Suitcase detune would otherwise keep its delay line and
// set the beat phase of the next note).
// --- hybrid voices --------------------------------------------------------------------------------------------
// hybrid.on, hybrid.bank, the bank and the host rate decide which side plays. NoBank and RateMismatch are reported (hybridState()
// tells the host what to say): the instruments without a physical model (hybridSlotHasModel) stay silent then, the others
// (the Rhodes, the Wurlitzer, the clavinet) play their physical models. A change of sides silences both.
int Engine::hybridSlot() const {
    return std::clamp(int(std::lround(params_.hybridBank)), 0, kHybridSlots - 1);
}

void Engine::updateHybrid() {
    const int slot = hybridSlot();
    // the clavinet bank is pickup set A in the solo position: the other rocker positions are the physical model's
    const bool recorded = slot != kClavSlot || (params_.clavAb > 0.5f && params_.clavCd < 0.5f);
    const bool want = params_.hybridOn > 0.5f && recorded;
    for (HybridSlot& h : hyb_)
        if (h.player && h.bank && h.bank->valid() && h.fs != fs_) {
            h.ready = h.player->prepare(fs_, *h.bank);             // false: the bank has another rate
            h.fs = fs_;
        }
    const bool slotChanged = slot != hybSlot_;
    hybSlot_ = slot;
    HybridSlot& h = hyb_[slot];
    if (h.player && h.ready) {
        HybridPlayer* hp = h.player.get();
        HybridResonance r;
        r.on = params_.resOn > 0.5f;
        r.gain = params_.resGain;
        r.modes = int(std::lround(params_.resModes));
        r.tiltDb = params_.resTiltDb;
        r.undamped = params_.resUndamped > 0.5f;
        hp->setResonance(r);
        // the preset's felt time and pedal curve, like the physical voices (pedalDamperFraction); the bank's values are
        // only the fallback of a player without a preset
        hp->setDamper(params_.damperRampMs, params_.damperPedalLost, params_.damperPedalFull);
        hp->setSlowPhase(params_.damperSlowShare, params_.damperSlowT60);
        HybridVoicing hv;                                           // P8: the voicing controls on the bank's poles
        hv.decay = params_.voicing.decay;
        hv.unison = params_.voicing.unison;
        hv.stretch = params_.voicing.stretch;
        hv.hardness = params_.voicing.hammerHardness;
        hp->setVoicing(hv);
        // vel.gainDb of the preset on top of the bank's law (P4; the grand's Pianoteq law, the normalised layers of the clavinet and
        // the Wurlitzer). hybrid.velGain = 0 leaves it to the bank: the MK8's layers carry the plugin's own dynamics
        if (params_.velGainDb.n > 0 && params_.hybridVelGain > 0.5f) {
            for (int v = 0; v < 128; ++v) hybridVelDb_[v] = params_.velGainDb(float(v));
            hp->setVelocityGain(hybridVelDb_);
        } else {
            hp->setVelocityGain(nullptr);
        }
        for (int n = 0; n < 128; ++n) hybridRelease_[n] = params_.releaseT60(float(n));   // decay.release of the preset (P3)
        hp->setReleaseT60(hybridRelease_);
    }
    HybridState st = HybridState::Off;
    if (want) st = !(h.player && h.bank && h.bank->valid()) ? HybridState::NoBank
                   : !h.ready ? HybridState::RateMismatch : HybridState::Active;
    const bool active = st == HybridState::Active;
    const bool changed = active != hybridActive_ || (active && slotChanged);
    hybridState_ = st;
    hybridActive_ = active;
    if (changed) allSoundOff();
}

int Engine::hybridBankRate(int slot) const {
    const HybridSlot& h = hyb_[slot < 0 ? hybridSlot() : std::min(slot, kHybridSlots - 1)];
    return h.bank && h.bank->valid() ? h.bank->rate() : 0;
}

void Engine::setParallel(Parallel* p) {
    parallel_ = p;
    for (HybridSlot& h : hyb_)
        if (h.player) h.player->setParallel(p);
}

bool Engine::setHybridBank(const uint8_t* data, size_t size, std::string* err, int slot) {
    HybridSlot& h = hyb_[slot < 0 ? hybridSlot() : std::min(slot, kHybridSlots - 1)];
    std::unique_ptr<HybridBank> bank(new HybridBank);
    if (!bank->attach(data, size, err)) return false;           // the previous bank, if any, stays
    h.player.reset(new HybridPlayer);                           // the old player goes before the old bank it points into
    h.player->setParallel(parallel_);
    h.bank = std::move(bank);
    h.fs = 0;                                                   // prepare for the current rate
    h.ready = false;
    updateHybrid();
    return true;                                                // a valid bank of another rate is accepted: hybridState() says RateMismatch
}

bool Engine::loadHybridBankFile(const std::string& path, std::string* err, int slot) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail("cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    void* mem = size > 0 ? alignedAlloc(size_t(size)) : nullptr;
    if (!mem) { std::fclose(f); return fail("cannot read " + path); }
    std::unique_ptr<uint8_t[], void (*)(void*)> raw(static_cast<uint8_t*>(mem), alignedFree);
    const size_t got = std::fread(raw.get(), 1, size_t(size), f);
    std::fclose(f);
    if (got != size_t(size)) return fail("short read of " + path);
    const int s = slot < 0 ? hybridSlot() : std::min(slot, kHybridSlots - 1);
    if (!setHybridBank(raw.get(), size_t(size), err, s)) return false;
    hyb_[s].image = std::move(raw);           // the previous image is released after the player has switched
    return true;
}

std::string Engine::defaultHybridBankPath(int slot) {
    static const char* const kEnv[kHybridSlots] = {"PMP_HYBRID_BANK", "PMP_MARK1_HYBRID_BANK", "PMP_MARK2_HYBRID_BANK",
                                                   "PMP_WURLI_HYBRID_BANK", "PMP_CLAV_HYBRID_BANK", "PMP_PIANET_HYBRID_BANK",
                                                   "PMP_STEINWAY_HYBRID_BANK", "PMP_GRANDPAS_HYBRID_BANK", "PMP_CP80_HYBRID_BANK"};
    static const char* const kFile[kHybridSlots] = {"banks/piano_hybrid.bank", "banks/mark1_hybrid.bank", "banks/mark2_hybrid.bank",
                                                    "banks/wurli_hybrid.bank", "banks/clav_hybrid.bank", "banks/pianet_hybrid.bank",
                                                    "banks/steinway_hybrid.bank", "banks/grandpas_hybrid.bank", "banks/cp80_hybrid.bank"};
    const int s = std::clamp(slot, 0, kHybridSlots - 1);
    const char* env = std::getenv(kEnv[s]);
    if (env && *env) return env;
    const std::string name = kFile[s];
    // the working directory first, then the source tree this was built from (the tools run from anywhere)
    std::string tree[] = {name,
#ifdef PMP_SOURCE_DIR
                          std::string(PMP_SOURCE_DIR) + "/" + name
#endif
    };
    for (const std::string& p : tree)
        if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return p; }
    return tree[0];
}

void Engine::clearHybridBank(int slot) {
    HybridSlot& h = hyb_[slot < 0 ? hybridSlot() : std::min(slot, kHybridSlots - 1)];
    h.player.reset();                         // the player first: it points into the bank
    h.bank.reset();
    h.image.reset();
    h.ready = false;
    h.fs = 0;
    updateHybrid();
}

void Engine::setHybridEnabled(bool on) {
    params_.hybridOn = on ? 1.0f : 0.0f;
    updateHybrid();
}

void Engine::resetOutput() {
    lpf_.reset();
    decimator_.reset();
    hpf_.reset();
    for (Biquad* b : {&toneLow_, &toneHigh_, &spkLow_, &spkHigh_}) b->reset();
    for (Biquad* b : {&clickHp_, &clickLp_}) b->reset();
    for (ForcedMode& m : mech_) m.reset();
    for (ForcedMode& m : rel_) m.reset();
    relCount_ = relRing_ = 0;
    pedRing_ = 0;
    pedEnv_ = 0;
    for (int w = 0; w < 2; ++w) { pedHp_[w].reset(); pedLp_[w].reset(); }
    wooshRing_ = 0; wooshEnv_ = 0; wooshDue_ = -1; wooshHp_.reset();
    if (fxOn_) fx_->reset();
    detune_.reset();
    vibPhase_ = 0;
    vibDepth_ = vibTarget_;
    tremPhase_ = 0;
    tremDepth_ = tremTarget_;
}

void Engine::setVoicing(double delta) {
    voicing_ = delta;
    applyVoicing();
}

// A live control: the voices move their pickups, the prepared key data stays as it is (it
// may be a table in flash). A later prepare() builds the voicing into the key data.
void Engine::applyVoicing() {
    for (int k = 0; k < kNumKeys; ++k) {
        const double w = keys_[k].pickupWidth;
        voices_[k].setPickupOffset((params_.pickupOffset(float(kFirstKey + k)) +
                                    params_.voicing.pickupOffset + voicing_) * w);
    }
}

void Engine::renderBlock(int m) {
    if (hybridActive_) {
        // the hybrid voice replaces the physical voices and everything behind them up to the output stage: the mode bank is
        // linear at the host rate, the board, the release and pedal noises and the 20 Hz coupling high pass belong to the model
        hybrid()->process(bufOut_, m);
        // ... except the noises of the action (P6): the pedal clunk, the release thump and the damper woosh add to the voice
        if (relOn_ && (relCount_ > 0 || relRing_ > 0)) renderRelease(m);
        if (pedOn_ && pedRing_ > 0) renderPedal(m);
        if (wooshOn_ && (wooshRing_ > 0 || wooshDue_ >= 0)) renderWoosh(m);
        renderOutputStage(m);
        return;
    }
    if (params_.hybridOn > 0.5f && !hybridSlotHasModel(hybSlot_)) {
        // the grand is the hybrid voice only (2026-10-09: the physical grand is gone), the instruments of the free libraries
        // (Pianet, Steinway, Grandpa's, CP80) never had a physical model: without a bank, or with a bank of another rate, they stay
        // silent - hybridState() says why, the hosts report it (the Rhodes, the Wurlitzer and the clavinet play their physical
        // models instead)
        std::fill(bufOut_, bufOut_ + m, 0.0f);
        renderOutputStage(m);
        return;
    }
    const int mi = m * os_;
    std::fill(bufInt_, bufInt_ + mi, 0.0f);
    float* force = mechOn_ ? bufForce_ : nullptr;
    float* clickForce = clickOn_ ? bufClickForce_ : nullptr;
    if (force) std::fill(bufForce_, bufForce_ + mi, 0.0f);
    if (clickForce) std::fill(bufClickForce_, bufClickForce_ + mi, 0.0f);
    for (int a = 0; a < activeCount_;) {
        const int k = active_[a];
        voices_[k].process(bufInt_, mi, force, clickForce);
        if (!voices_[k].active()) {
            listed_[k] = false;
            active_[a] = active_[--activeCount_];
        } else {
            ++a;
        }
    }
    lpf_.process(bufInt_, mi);
    if (os_ == 2) decimator_.process(bufInt_, bufOut_, m);
    else          std::copy(bufInt_, bufInt_ + m, bufOut_);
    if (mechOn_ || clickOn_) {
        // the force to the host rate (mean of each pair: enough for modes below ~1 kHz)
        for (int i = 0; i < m; ++i) {
            if (mechOn_) {
                const real f = os_ == 2 ? real(0.5) * (real(bufForce_[2 * i]) + real(bufForce_[2 * i + 1]))
                                        : real(bufForce_[i]);
                real y = 0;
                for (int j = 0; j < kMechModes; ++j)
                    if (mechGain_[j] != 0) y += mechGain_[j] * mech_[j].process(f);
                bufOut_[i] += float(ampKnockOn_ ? amp_.process(y) : y);
            }
            if (clickOn_) {
                const real f = os_ == 2 ? real(0.5) * (real(bufClickForce_[2 * i]) + real(bufClickForce_[2 * i + 1]))
                                        : real(bufClickForce_[i]);
                bufClick_[i] = float(clickLp_.process1(clickHp_.process1(clickGain_ * f * clickNoise())));
            }
        }
    }

    if (relOn_ && (relCount_ > 0 || relRing_ > 0)) renderRelease(m);
    if (pedOn_ && pedRing_ > 0) renderPedal(m);
    renderOutputStage(m);
}

// The output stage: the instrument chain (tone controls, amp, speaker, vibrato, detune), the gain, the soft clip and the effects,
// behind the physical voices or the hybrid voice.
void Engine::renderOutputStage(int m) {
    if (pickupResOn_ && !hybridActive_) pickupRes_.process(bufOut_, m);   // the coil at the amp input (the hybrid's recordings have it)
    if (clavTone_.any()) clavTone_.process(bufOut_, m); // the D6 tone switch banks
    // The amp stage (amp.on: tone controls, power amp, cabinet) models the instrument's own amplifier and speaker, which the
    // recordings of a hybrid bank already carry (the Wurlitzer: tone.bass +17 dB and the 6 kHz speaker are the calibration's
    // picture of the recording chain, docs/WURLITZER.md 3.10): the hybrid voice bypasses it, unless hybrid.amp says the stage is
    // the instrument's own and not in the bank (the Suitcase: the Mark II bank through the designed Suitcase chain). The tremolo
    // and the vibrato are features of the instrument and stay. The click rides on the physical voices only.
    const bool bypass = hybridActive_ && params_.hybridAmp < 0.5f;
    const bool toneLow = toneLowOn_ && !bypass, toneHigh = toneHighOn_ && !bypass, amp = ampOn_ && !bypass,
               spkLow = spkLowOn_ && !bypass, spkHigh = spkHighOn_ && !bypass, click = clickOn_ && !hybridActive_;

    // the loudness law of the hybrid voice is absolute (fitted to the finished output of the grand): no out.trimDb on top
    const real gain = real(params_.masterGain) * volume_ * (hybridActive_ ? real(1) : trim_);
    const bool clip = params_.softClip > 0.5f;
    // Peterson-style stereo vibrato (auto-pan): the 4 Hz LFO is evaluated at the block
    // edges and interpolated linearly, two sin() per block instead of one per sample. The
    // phase is a 64-bit fraction of a turn: exact in float builds too (a float phase in
    // radians drifted by up to 1e-5 of the rate).
    const bool vib = vibTarget_ > 0 || vibDepth_ > 0;
    const uint64_t vibNext = vibPhase_ + vibInc_ * uint64_t(m);
    real lfo = 0, dLfo = 0;
    if (vib) {
        lfo = std::sin(real(2.0 * kPi) * turns(vibPhase_));
        dLfo = (std::sin(real(2.0 * kPi) * turns(vibNext)) - lfo) / real(m);
    }
    vibPhase_ = vibNext;
    // amplifier tremolo (the 200A's LDR in the preamp): the same block-edge LFO with its own
    // phase, amplitude modulation between the tone controls and the power amp
    const bool trem = tremTarget_ > 0 || tremDepth_ > 0;
    const uint64_t tremNext = tremPhase_ + tremInc_ * uint64_t(m);
    real tlfo = 0, dTrem = 0;
    if (trem) {
        tlfo = std::sin(real(2.0 * kPi) * turns(tremPhase_));
        dTrem = (std::sin(real(2.0 * kPi) * turns(tremNext)) - tlfo) / real(m);
    }
    tremPhase_ = tremNext;
    for (int i = 0; i < m; ++i) {
        real y = hybridActive_ ? real(bufOut_[i]) : hpf_.process(bufOut_[i]);   // the modes carry no DC: no coupling high pass
        if (toneLow)  y = toneLow_.process1(y);
        if (toneHigh) y = toneHigh_.process1(y);
        if (trem) {
            tremDepth_ += tremSmooth_ * (tremTarget_ - tremDepth_);
            y *= real(1) - real(0.5) * tremDepth_ * (real(1) + tlfo);
            tlfo += dTrem;
        }
        if (amp)      y = amp_.process(y);
        if (spkLow)   y = spkLow_.process1(y);
        if (spkHigh)  y = spkHigh_.process1(y);
        if (click)    y += real(bufClick_[i]);
        real l = y, r = y;
        if (vib) {
            vibDepth_ += vibSmooth_ * (vibTarget_ - vibDepth_);
            const real h = real(0.5) * vibDepth_;
            l = y * (real(1) - h * (real(1) + lfo));
            r = y * (real(1) - h * (real(1) - lfo));
            lfo += dLfo;
        }
        if (detuneMix_ > 0) {                    // stereo micro pitch shift (recording effect)
            real up, down;
            detune_.process(y, up, down);
            l += detuneMix_ * up;
            r += detuneMix_ * down;
        }
        l *= gain;
        r *= gain;
        if (fxOn_) {                             // the soft clip follows the effect chain
            outL_[i] = float(l);
            outR_[i] = float(r);
            continue;
        }
        outL_[i] = clip ? softClip(float(l)) : float(l);
        outR_[i] = clip ? softClip(float(r)) : float(r);
    }
    if (fxOn_) {
        // its limiter (from velvetkeys) peaks at 0.98 / fastTanh(0.9) = 1.34, so the output
        // stays bounded only with the soft clip after it
        fx_->process(outL_, outR_, m);
        if (clip)
            for (int i = 0; i < m; ++i) {
                outL_[i] = softClip(outL_[i]);
                outR_[i] = softClip(outR_[i]);
            }
    }
    if (vibTarget_ == 0 && vibDepth_ < real(1e-5)) vibDepth_ = 0;   // faded out: skip next block
}


void Engine::process(float* out, int nFrames) {
    ScopedFlushDenormals noDenormals;
    int done = 0;
    while (done < nFrames) {
        const int m = std::min(kBlock, nFrames - done);
        renderBlock(m);
        for (int i = 0; i < m; ++i) out[done + i] = 0.5f * (outL_[i] + outR_[i]);
        done += m;
    }
}

void Engine::processStereo(float* left, float* right, int nFrames) {
    ScopedFlushDenormals noDenormals;
    int done = 0;
    while (done < nFrames) {
        const int m = std::min(kBlock, nFrames - done);
        renderBlock(m);
        std::copy(outL_, outL_ + m, left + done);
        std::copy(outR_, outR_ + m, right + done);
        done += m;
    }
}

} // namespace pmp
