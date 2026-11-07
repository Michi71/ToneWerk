// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.cpp - keys, pedal, mechanical noises, output level and auto-voicing
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
    const double gaugeScale = std::min(std::max(1.0 + 0.01 * double(v.reedGauge), 0.2), 3.0);
    const double gapScale   = std::min(std::max(1.0 + 0.01 * double(v.pickupGap), 0.1), 10.0);
    const double dampScale  = std::max(1.0 + 0.01 * double(v.damperRelease), 0.02);

    ReedSpec spec;
    spec.f1            = midiToHz(note, p.a4);
    double tuneCents = p.keysTuneByKey.n ? double(p.keysTuneByKey(key)) : 0.0;     // measured offset of the instrument
    if (p.keysTuneCents != 0.0f)               // this key's own tuning: a fixed offset from the seed
        tuneCents += double(p.keysTuneCents) * keyGaussian(double(p.keysSeed), note, 1);
    if (tuneCents != 0.0) spec.f1 *= std::exp2(tuneCents / 1200.0);
    spec.thickness     = double(p.reedThickness) * gaugeScale;
    spec.width         = p.reedWidth;
    spec.youngs        = p.reedYoungs;
    spec.density       = p.reedDensity;
    spec.tipMu         = p.tipMuByKey.n ? double(p.tipMuByKey(key)) : double(p.tipMu);
    spec.tipPosRel     = p.tipPos;
    spec.strikePosRel  = std::min(std::max(double(p.strikePos(key)) * strikeScale, 0.02), 0.98);
    spec.damperPosRel  = p.damperPos;
    spec.pickupPosRel  = p.pickupAxial;
    spec.combLenRel    = p.pickupComb;
    spec.elements      = int(p.femElements + 0.5f);
    const double fsInt = fs_ * os_;
    const int maxModes = std::min(std::max(int(p.maxModes + 0.5f), 1), kMaxModes);
    c.modes = designReed(spec, std::min(0.45 * fsInt, 20000.0), maxModes);

    // losses: measured fundamental decay, higher modes decay faster
    const double f1 = spec.f1;
    const double sigma1 = t60ToSigma(std::max(double(p.t60(key)), 0.05));
    const double hf2 = p.lossHf2ByKey.n ? double(p.lossHf2ByKey(key)) : double(p.lossHf2);
    const double lossHf = p.lossHfByKey.n ? double(p.lossHfByKey(key)) : double(p.lossHf);
    const double lossExp = p.lossExpByKey.n ? double(p.lossExpByKey(key)) : double(p.lossExp);
    for (int m = 0; m < c.modes.count; ++m) {
        const double f = c.modes.freq[m];
        const double sig = sigma1 + lossHf * (std::pow(f, lossExp) - std::pow(f1, lossExp)) + hf2 * (f * f - f1 * f1);
        c.sigmaFree[m] = std::max(sig, 0.5 * sigma1);
    }

    // damper: extra decay ~ phi(damper)^2 per mode, scaled so that the fundamental decays with the release T60; the
    // pressed felt also damps every mode by a uniform term (damper.harpT60)
    const double harpT60 = p.damperHarpT60.n ? double(p.damperHarpT60(key)) : 0.0;
    const double harpSigma = harpT60 > 0.0 ? t60ToSigma(harpT60) : 0.0;
    const double dSig = std::max(t60ToSigma(std::max(double(p.releaseT60(key)) * dampScale, 0.01)) - c.sigmaFree[0] - harpSigma, 0.0);
    const double phiD = c.modes.phiDamper[0];
    const double cd = (phiD != 0.0) ? 2.0 * dSig / (phiD * phiD) : 0.0;
    for (int m = 0; m < c.modes.count; ++m)
        c.sigmaDamper[m] = 0.5 * cd * c.modes.phiDamper[m] * c.modes.phiDamper[m] + harpSigma;
    c.damperRampSec = p.damperRampMs * 1e-3;

    // the fundamental's two stages (decay.fastShareByKey, docs/REEDS.md 27): a copy of the fundamental mode at its
    // frequency takes the share s of its amplitude (every shape scaled by sqrt s, the original by sqrt (1 - s)) and
    // decays with its own T60; the damper takes both like the fundamental
    const double fastShare = p.fastShareByKey.n ? std::min(std::max(double(p.fastShareByKey(key)), 0.0), 1.0) : 0.0;
    const double fastT60 = p.fastT60ByKey.n ? double(p.fastT60ByKey(key)) : 0.0;
    if (fastShare > 0.0 && fastT60 > 0.0 && c.modes.count < kMaxModes) {
        const double a = std::sqrt(fastShare), b = std::sqrt(1.0 - fastShare);
        const int k = c.modes.count++;
        c.modes.freq[k] = c.modes.freq[0];
        c.modes.phiStrike[k] = a * c.modes.phiStrike[0];  c.modes.phiStrike[0] *= b;
        c.modes.phiTip[k]    = a * c.modes.phiTip[0];     c.modes.phiTip[0]    *= b;
        c.modes.phiDamper[k] = a * c.modes.phiDamper[0];  c.modes.phiDamper[0] *= b;
        c.sigmaFree[k]   = std::max(t60ToSigma(std::max(fastT60, 0.005)), c.sigmaFree[0]);
        c.sigmaDamper[k] = c.sigmaDamper[0];
        c.fastMode = k;
        c.fastLinear = std::min(std::max(double(p.fastLinear), 0.0), 1.0);
    }

    // hammer: stiffness from the contact time against a rigid wall at 1 m/s
    //   T = 2 dmax / v * I(a),   dmax = ((a+1) m v^2 / (2k))^(1/(a+1))
    c.excitation     = p.excitation >= 0.5f ? 1 : 0;
    c.hammerMass     = std::max(double(p.hammerMass(key)) * massScale, 1e-5);
    c.hammerAlpha    = std::max(double(p.hammerAlpha(key)), 1.0);
    c.hammerLambda   = p.hammerLambda;
    c.hammerSubsteps = std::max(1, int(p.hammerSubsteps + 0.5f));
    c.hammerSpeedHardening = p.hammerSpeedHardening + p.hammerSpeedHardeningByKey(key);
    const double a  = c.hammerAlpha, n = a + 1.0;
    const double ia = std::sqrt(kPi) * std::tgamma(1.0 + 1.0 / n) / std::tgamma(0.5 + 1.0 / n);
    const double dmax = std::max(double(p.contactMs(key)) * hardness, 0.01) * 1e-3 / (2.0 * ia);
    c.hammerK = n * c.hammerMass / (2.0 * std::pow(dmax, n));

    // pad: the static stiffness of the reed at the pad point from the modes (compliance sum phi^2 / omega^2), the
    // adhesion as the force that holds the reed deflected by pluck.tearMm there, the foam pluck.padRatio times stiffer
    double compliance = 0.0;
    for (int m = 0; m < c.modes.count; ++m) {
        const double w = 2.0 * kPi * c.modes.freq[m];
        compliance += c.modes.phiStrike[m] * c.modes.phiStrike[m] / (w * w);
    }
    const double kStatic = compliance > 0.0 ? 1.0 / compliance : 1.0;
    c.pluckAdhesion   = kStatic * std::max(double(p.pluckTearMm(key)), 1e-3) * 1e-3 * massScale;
    c.pluckK          = kStatic * std::max(double(p.pluckPadRatio), 0.05);
    c.pluckVelExp     = p.pluckVelExp;
    c.pluckReleaseSec = std::max(double(p.pluckReleaseMs(key)) * hardness, 0.005) * 1e-3;

    // pickup
    c.pickupType     = int(std::min(std::max(p.pickupType + 0.5f, 0.0f), 4.0f));
    c.pickupSection  = note <= 46 ? 0 : note <= 52 ? 1 : note <= 74 ? 2 : 3;       // reeds 1-14, 15-20, 21-42, 43-64 (MIDI 33..96)
    c.pickupHpfHz    = double(p.pickupHpfHz);
    c.pickupOnsetHz  = p.pickupOnsetMul.n ? double(p.pickupOnsetMul(key)) * midiToHz(note) : 0.0;
    c.pickupOnsetMs  = double(p.pickupOnsetMs);
    c.reedGlide      = p.reedGlide.n ? double(p.reedGlide(key)) : 0.0;
    const double w = std::max(double(p.pickupWidth(key)) * gapScale, 0.001) * 1e-3;
    c.pickupWidth    = w;
    c.pickupOffset   = (p.pickupOffset(key) + v.pickupOffset + voicing_) * w;
    c.pickupExponent = p.pickupExponent;
    c.pickupWarp     = double(p.pickupWarp);
    c.pickupPole     = double(p.pickupPole);
    c.pickupSpan     = c.modes.combDrop;

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
    for (Pending& q : pending_) q.left = -1;
    pendingCount_ = 0;
    activeCount_ = 0;
    sustain_ = false;
    pedalPos_ = 0;

    lpf_.lowpass(params_.lpfHz, params_.lpfQ, fsInt);
    lpf_.reset();
    decimator_.design(135, 90.0);
    hpf_.setup(params_.hpfHz, fs_);
    hpf_.reset();
    setupOutput();
}

void Engine::applyMechScales() {
    // an empty curve is 0 dB for every key: the scale is exactly 1 and nothing changes
    for (int k = 0; k < kNumKeys; ++k) {
        voices_[k].setMechScale(std::pow(10.0, double(params_.mechByKey(float(kFirstKey + k))) / 20.0));
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

void Engine::setupOutput() {
    const Params& p = params_;
    for (int k = 0; k < kNumKeys; ++k) {      // the force law of the mech modes and the damper's blow (a prepareKey() applies them too)
        const float key = float(kFirstKey + k);
        voices_[k].setMechExponent(std::max(double(p.mechExpByKey.n ? p.mechExpByKey(key) : p.mechExp), 0.5));
        const double mass = keys_[k].excitation == 1 ? 1e-3 : keys_[k].hammerMass;    // the pad: 1 g
        voices_[k].setDamperKnock(p.damperKnock == 0.0f ? 0.0 : mass * double(p.damperKnock) *
                                  std::pow(10.0, double(p.damperKnockByKey(key)) / 20.0),
                                  double(p.damperKnockPeriods));
    }
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
    // the blow
    blowOn_ = p.blowGain > 0.0f;
    blowHp_.highpass(std::min(std::max(double(p.blowLoHz), 10.0), 0.4 * fs_), 0.707, fs_);
    blowLp_.lowpass(std::min(std::max(double(p.blowHiHz), 20.0), 0.45 * fs_), 0.707, fs_);
    // a 4th-order low pass (two Butterworth sections, Q 0.54 and 1.31): the Wurlitzer's knock falls ~25 dB per octave
    // above 300 Hz; at 12 dB the burst stood 15-25 dB over the soft notes' spectrum between the harmonics up to 4 kHz,
    // a rasp heard as distortion in C2-C4 (Michael 2026-11-01; docs/REEDS.md 26)
    blowLp4_ = p.blowLpOrder >= 3.0f;
    if (blowLp4_) {
        blowLp_.lowpass(std::min(std::max(double(p.blowHiHz), 20.0), 0.45 * fs_), 0.5411961, fs_);
        blowLp2_.lowpass(std::min(std::max(double(p.blowHiHz), 20.0), 0.45 * fs_), 1.3065630, fs_);
        // the treble's knock alone is broadband (-15 dB at 1 kHz): its keys take the 2nd-order path (blow.steepByKey)
        blowHpG_.highpass(std::min(std::max(double(p.blowLoHz), 10.0), 0.4 * fs_), 0.707, fs_);
        blowLpG_.lowpass(std::min(std::max(double(p.blowHiHz), 20.0), 0.45 * fs_), 0.707, fs_);
    }
    blowHpG_.reset();
    blowLpG_.reset();
    blowEnvG_ = 0;
    blowHp_.reset();
    blowLp_.reset();
    blowLp2_.reset();
    const double bt = std::max(double(p.blowT60), 0.002);
    blowDecay_ = real(std::pow(10.0, -3.0 / (bt * fs_)));
    blowRingMax_ = int(1.6 * bt * fs_) + int(0.01 * fs_);   // -96 dB, and the filters' tail
    blowNorm_ = real(p.blowGain) * real(p.voiceRms);
    blowEnv_ = 0;
    blowRing_ = 0;
    blowRng_ = 0x2545F4914F6CDD1DULL;

}

bool Engine::prepareKey(int note, bool autoVoiceKey) {
    if (!inRange(note)) return false;
    os_ = params_.oversampling >= 1.5f ? 2 : 1;
    const double fsInt = fs_ * os_;
    const int k = note - kFirstKey;
    KeyConfig* keys = ownKeys();
    keys[k] = makeKeyConfig(note);
    voices_[k].configure(keys[k], fsInt);
    voices_[k].setMechScale(std::pow(10.0, double(params_.mechByKey(float(note))) / 20.0));   // as applyMechScales()
    lpf_.lowpass(params_.lpfHz, params_.lpfQ, fsInt);
    lpf_.reset();
    decimator_.design(135, 90.0);
    hpf_.setup(params_.hpfHz, fs_);
    hpf_.reset();
    setupOutput();
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
        // the energy reference of the key (sleep threshold, the level that ranks voices for stealing): the reed's
        // energy 50 ms after a v100 strike, when the fast upper modes of the attack have gone - their share differs
        // from key to key, and the maximum during the contact would rank keys by it
        double eRef = 0.0;
        for (int i = 0; i < nTotal; i += 64) {
            const int m = std::min(64, nTotal - i);
            v.process(&buf[size_t(i)], m);
            if (i < nEnergy) eRef = v.energy();
        }
        double sum = 0.0;
        for (int i = n0; i < nTotal; ++i) sum += double(buf[size_t(i)]) * buf[size_t(i)];
        const double rms = std::sqrt(sum / (nTotal - n0));
        double gain = rms > 0.0
            ? params_.voiceRms / rms * dbToGain(params_.keyGainDb(float(kFirstKey + k))) : 1.0;
        if (params_.keysLevelDb != 0.0f)         // this key's own loudness: a fixed offset from the seed
            gain *= dbToGain(double(params_.keysLevelDb) * keyGaussian(double(params_.keysSeed), kFirstKey + k, 2));
        keys[k].gain = gain;
        keys[k].sleepEnergy = eRef * std::pow(10.0, -params_.sleepDb / 10.0);
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
    if (!playable(note)) return;
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    const int k = note - kFirstKey;
    if (!voices_[k].active() || voices_[k].choking()) makeRoom(k);   // a new voice
    keyDown_[k] = true;
    voices_[k].setDamper(false);
    const int v = std::min(velocity, 127);
    double db = params_.velGainDb.n > 0 ? double(params_.velGainDb(float(v))) : 0.0;
    if (params_.velGainSlope.n > 0) db += double(params_.velGainSlope(float(note))) * (v - 127);
    const double level = db != 0.0 ? std::pow(10.0, db / 20.0) : 1.0;
    if (params_.damperVelDb.n > 0)
        voices_[k].setDamperScale(std::pow(10.0,
            double(params_.damperVelDb(float(v))) / 20.0));
    double speed = hammerVelocity(velocity), contact = 1.0;
    if (params_.velSpeedCapByKey.n > 0) {
        const double cap = double(params_.velSpeedCapByKey(float(note)));
        if (cap > 0.0) speed /= std::pow(1.0 + std::pow(speed / cap, 4.0), 0.25);
    }
    if (params_.humanSpeed != 0.0f) speed *= std::exp(double(params_.humanSpeed) * gaussian());
    if (params_.humanContact != 0.0f) contact = std::exp(double(params_.humanContact) * gaussian());
    if (blowOn_) kickBlow(note, speed, level);
    const int lead = leadSamples(note, v);
    if (lead > 0) {                           // the knock sounds now, the hammer reaches the reed later
        if (pending_[k].left < 0) ++pendingCount_;
        pending_[k] = Pending{lead, speed, level, contact};
        return;
    }
    if (pending_[k].left >= 0) { pending_[k].left = -1; --pendingCount_; }   // a soft strike overtakes a pending one
    fireStrike(k, speed, level, contact);
}

void Engine::fireStrike(int k, double speed, double level, double contact) {
    if (!voices_[k].active() || voices_[k].choking()) makeRoom(k);
    voices_[k].strike(speed, level, contact);
    activate(k);
}

int Engine::leadSamples(int note, int velocity) const {
    const Params& p = params_;
    if (p.blowLeadMs.n == 0 || p.blowLead <= 0.0f) return 0;
    const double v0 = p.blowLeadVel0, v1 = std::max(double(p.blowLeadVel1), v0 + 1.0);
    const double t = std::min(std::max((double(velocity) - v0) / (v1 - v0), 0.0), 1.0);
    const double ms = std::max(double(p.blowLeadMs(float(note))), 0.0) * t * t * (3.0 - 2.0 * t) * double(p.blowLead);
    return int(ms * 1e-3 * fs_ + 0.5);
}

int Engine::nextPending() const {
    int next = kBlock;
    if (pendingCount_ > 0)
        for (const Pending& q : pending_)
            if (q.left >= 0) next = std::min(next, q.left);
    return next;
}

// A strike due after m host samples fires between two blocks, on its own sample: the output does not depend on the
// host's block size
void Engine::advancePending(int m) {
    if (pendingCount_ == 0) return;
    for (int k = 0; k < kNumKeys; ++k) {
        Pending& q = pending_[k];
        if (q.left < 0) continue;
        q.left -= m;
        if (q.left <= 0) {
            q.left = -1;
            --pendingCount_;
            fireStrike(k, q.speed, q.level, q.contact);
        }
    }
}

// The knock of the hammer on the reed bar: the burst of this strike joins the envelope of those still sounding
void Engine::kickBlow(int note, double speed, double level) {
    double amp = double(blowNorm_) * level * std::pow(std::max(speed, 1e-4), double(params_.blowExp));   // re 1 m/s
    if (params_.blowByKey.n > 0) amp *= std::pow(10.0, double(params_.blowByKey(float(note))) / 20.0);
    real steep = 1;
    if (blowLp4_ && params_.blowSteepByKey.n > 0)
        steep = std::clamp(real(params_.blowSteepByKey(float(note))), real(0), real(1));
    blowEnv_ += real(amp) * steep;
    blowEnvG_ += real(amp) * (real(1) - steep);
    blowRing_ = blowRingMax_;
}

real Engine::blowNoise() {
    blowRng_ ^= blowRng_ >> 12;
    blowRng_ ^= blowRng_ << 25;
    blowRng_ ^= blowRng_ >> 27;
    const uint64_t r = blowRng_ * 0x2545F4914F6CDD1DULL;
    const double s = double(r & 0xFFFF) + double((r >> 16) & 0xFFFF) + double((r >> 32) & 0xFFFF) + double(r >> 48);
    return real((s / 65536.0 - 2.0) * 1.7320508075688772);
}

// Ends on its own sample, not at the end of a host block: the output does not depend on the block size
void Engine::renderBlow(int m) {
    for (int i = 0; i < m && blowRing_ > 0; ++i) {
        const real n = blowNoise();
        const real b = blowLp_.process1(blowHp_.process1(blowEnv_ * n));
        bufOut_[i] += float(blowLp4_ ? blowLp2_.process1(b) + blowLpG_.process1(blowHpG_.process1(blowEnvG_ * n)) : b);
        blowEnv_ *= blowDecay_;
        blowEnvG_ *= blowDecay_;
        if (--blowRing_ == 0) {
            blowEnv_ = blowEnvG_ = 0;
            blowHp_.reset(); blowLp_.reset(); blowLp2_.reset(); blowHpG_.reset(); blowLpG_.reset();
        }
    }
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

void Engine::noteOff(int note, int velocity) {
    if (!inRange(note)) return;
    const int k = note - kFirstKey;
    const bool wasDown = keyDown_[k];
    keyDown_[k] = false;
    const real fraction = pedalDamperFraction();
    const real releaseSpeed = real(std::clamp(velocity, 0, 127)) / real(127);
    voices_[k].setDamperFraction(fraction);                // half pedal aware
    if (wasDown && fraction > 0) voices_[k].damperHit(fraction, releaseSpeed);
    if (relOn_ && wasDown && fraction > 0) scheduleRelease(k, fraction);
}

// The damper lands on the reed and the key on its stop: each release strikes the two thump modes once
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
        case 1:    // mod wheel: move the pickups towards the reeds' centre ("bark")
            setVoicing(0.4 * value / 127.0);
            break;
        case 7:
            volume_ = real(value / 127.0);
            break;
        case 64: {
            if (params_.damperPedal < 0.5f) break;    // no sustain pedal (Pianet: the pad is the damper)
            // half pedal: the position is continuous; the dampers of released keys
            // follow the rail with its lost motion (damper.pedalLost .. pedalFull)
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
            allSoundOff();
            break;
        case 123:
            allNotesOff();
            break;
        default:
            break;
    }
}

void Engine::allNotesOff() {
    sustain_ = false;
    pedalPos_ = 0;
    for (int k = 0; k < kNumKeys; ++k) {
        keyDown_[k] = false;
        voices_[k].setDamper(true);
    }
}

void Engine::allSoundOff() {
    allNotesOff();
    for (Pending& q : pending_) q.left = -1;
    pendingCount_ = 0;
    for (int k = 0; k < kNumKeys; ++k) {
        voices_[k].reset();
        listed_[k] = false;
    }
    activeCount_ = 0;
    resetOutput();
}

// Silences the filter tails and the noises too: after allSoundOff() the engine renders exactly
// like a freshly prepared one.
void Engine::resetOutput() {
    lpf_.reset();
    decimator_.reset();
    hpf_.reset();
    for (ForcedMode& m : mech_) m.reset();
    for (ForcedMode& m : rel_) m.reset();
    relCount_ = relRing_ = 0;
    pedRing_ = 0;
    pedEnv_ = 0;
    for (int w = 0; w < 2; ++w) { pedHp_[w].reset(); pedLp_[w].reset(); }
    blowRing_ = 0;
    blowEnv_ = 0;
    blowHp_.reset();
    blowLp_.reset();
    blowLp2_.reset();
    blowHpG_.reset();
    blowLpG_.reset();
    blowEnvG_ = 0;
    blowRng_ = 0x2545F4914F6CDD1DULL;
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
    const int mi = m * os_;
    std::fill(bufInt_, bufInt_ + mi, 0.0f);
    float* force = mechOn_ ? bufForce_ : nullptr;
    if (force) std::fill(bufForce_, bufForce_ + mi, 0.0f);
    // the voices are summed in key order: the order of the active list changes as voices fall asleep and (with the
    // lead, blow.leadMs) start between blocks, and a sum in its order rounded differently with the host's block size
    for (int k = 0; k < kNumKeys; ++k)
        if (listed_[k]) voices_[k].process(bufInt_, mi, force);
    for (int a = 0; a < activeCount_;) {
        const int k = active_[a];
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
    if (mechOn_) {
        // the force to the host rate (mean of each pair: enough for modes below ~1 kHz)
        for (int i = 0; i < m; ++i) {
            const real f = os_ == 2 ? real(0.5) * (real(bufForce_[2 * i]) + real(bufForce_[2 * i + 1]))
                                    : real(bufForce_[i]);
            real y = 0;
            for (int j = 0; j < kMechModes; ++j)
                if (mechGain_[j] != 0) y += mechGain_[j] * mech_[j].process(f);
            bufOut_[i] += float(y);
        }
    }

    if (relOn_ && (relCount_ > 0 || relRing_ > 0)) renderRelease(m);
    if (pedOn_ && pedRing_ > 0) renderPedal(m);
    if (blowOn_ && blowRing_ > 0) renderBlow(m);
}

// The output: the coupling capacitor behind the pickups, then the level (out.gain, volume, out.trimDb). No effects,
// no clip: the application behind the instrument does that.
void Engine::process(float* out, int nFrames) {
    ScopedFlushDenormals noDenormals;
    int done = 0;
    while (done < nFrames) {
        if (pendingCount_ > 0 && nextPending() == 0) advancePending(0);
        const int m = std::min({kBlock, nFrames - done, pendingCount_ > 0 ? nextPending() : kBlock});
        renderBlock(m);
        const real gain = real(params_.masterGain) * volume_ * trim_;
        for (int i = 0; i < m; ++i) out[done + i] = float(hpf_.process(bufOut_[i]) * gain);
        done += m;
        advancePending(m);
    }
}

} // namespace pmp
