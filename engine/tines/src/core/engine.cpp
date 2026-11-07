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
    spec.pickupPosRel  = p.pickupAxialByKey.n ? double(p.pickupAxialByKey(key)) : double(p.pickupAxial);
    spec.tonebarSplit  = p.tonebarSplit;
    spec.tonebarDetune = (p.tonebarDetuneByKey.n ? double(p.tonebarDetuneByKey(key)) : double(p.tonebarDetune))
                         + v.tonebarDetune;
    spec.tonebarMass   = p.tonebarMassByKey.n ? double(p.tonebarMassByKey(key)) : double(p.tonebarMass);
    spec.tonebarGyr    = p.tonebarGyr;
    spec.elements      = int(p.femElements + 0.5f);
    const double fsInt = fs_ * os_;
    const int maxModes = std::min(std::max(int(p.maxModes + 0.5f), 1), kMaxModes);
    c.modes = designTine(spec, std::min(0.45 * fsInt, 20000.0), maxModes);

    // losses: measured fundamental decay, higher modes decay faster, tonebar a bit slower
    const double f1 = spec.f1;
    const double sigma1 = t60ToSigma(std::max(double(p.t60(key)), 0.05));
    const double barT60 = p.tonebarT60ByKey.n ? double(p.tonebarT60ByKey(key)) : double(p.tonebarT60);
    const double sigmaBar = barT60 > 0.0 ? t60ToSigma(std::max(barT60, 0.005)) : sigma1 * p.tonebarLoss;
    for (int m = 0; m < c.modes.count; ++m) {
        const double f = c.modes.freq[m];
        const double hf2 = p.lossHf2ByKey.n ? p.lossHf2ByKey(key) : p.lossHf2;
        const double lossHf = p.lossHfByKey.n ? double(p.lossHfByKey(key)) : double(p.lossHf);
        const double lossExp = p.lossExpByKey.n ? double(p.lossExpByKey(key)) : double(p.lossExp);
        const double s1 = sigma1;
        double sigT = s1 + lossHf * (std::pow(f, lossExp) - std::pow(f1, lossExp)) +
                      hf2 * (f * f - f1 * f1);
        sigT = std::max(sigT, 0.5 * s1);
        const double share = std::min(std::max(c.modes.tonebarShare[m], 0.0), 1.0);
        // tonebar.t60 (the measured tonebar, dead within tens of milliseconds): its damping
        // belongs to the tonebar-dominant modes alone; the tine modes keep their measured
        // decay, as the instrument does (10 s against 0.05 s). Without it, the old rule mixes
        // the two decays by the modal mass share (the slow tonebar partner of the doublet).
        if (barT60 > 0.0 && spec.tonebarMass > 0.0) {
            c.sigmaFree[m] = share > 0.5 ? sigmaBar : sigT;
            c.transient[m] = share > 0.5;
        } else {
            c.sigmaFree[m] = (1.0 - share) * sigT + share * sigmaBar;
        }
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
    // the measured tonebar (tonebar.t60): the felt holds the tine, and the tonebar-dominant
    // mode swings through it - in the note-off recordings its line falls with H1 at the damper
    // (docs/ATTACK_WINDOWS.md 10). Its own shape at the felt is tiny (a heavy clamp mass), so it
    // takes at least the fundamental's damper damping.
    for (int m = 0; m < c.modes.count; ++m)
        if (c.transient[m]) c.sigmaDamper[m] = std::max(c.sigmaDamper[m], c.sigmaDamper[f0]);
    // the tonebar-dominant mode moves the tine across the swing plane by the share lat (the bass and middle
    // of the recordings, docs/ATTACK_WINDOWS.md 10.7): its tip shape splits into the plane and across it
    const double lat = p.tonebarLateralByKey.n ? std::min(std::max(double(p.tonebarLateralByKey(key)), 0.0), 1.0) : 0.0;
    c.pickupLateralOffset = p.pickupLateralOffset;
    c.pickupHystQ = p.pickupHystQ;
    c.pickupHystBeta = p.pickupHystBeta;
    c.pickupHystGamma = p.pickupHystGamma;
    if (lat > 0.0)
        for (int m = 0; m < c.modes.count; ++m)
            if (c.transient[m]) {
                c.phiTipY[m] = lat * c.modes.phiTip[m];
                c.modes.phiTip[m] *= std::sqrt(1.0 - lat * lat);
            }
    // the tonebar's two planes (tonebar.fastShareByKey, docs/ATTACK_WINDOWS.md 24): a copy of the tonebar-dominant
    // mode at the same frequency takes the share s of its amplitude at the pickup (both shapes scaled by sqrt s, the
    // original by sqrt (1 - s)) and decays with its own T60; the felt damps both like the fundamental
    const double fastShare = p.tonebarFastShareByKey.n ? std::min(std::max(double(p.tonebarFastShareByKey(key)), 0.0), 1.0) : 0.0;
    const double fastT60 = p.tonebarFastT60ByKey.n ? double(p.tonebarFastT60ByKey(key)) : 0.0;
    if (fastShare > 0.0 && fastT60 > 0.0) {
        const double a = std::sqrt(fastShare), b = std::sqrt(1.0 - fastShare);
        const int n0 = c.modes.count;
        for (int m = 0; m < n0 && c.modes.count < kMaxModes; ++m) {
            if (!c.transient[m]) continue;
            const int k = c.modes.count++;
            c.modes.freq[k] = c.modes.freq[m];
            c.modes.tonebarShare[k] = c.modes.tonebarShare[m];
            c.modes.phiStrike[k] = a * c.modes.phiStrike[m];  c.modes.phiStrike[m] *= b;
            c.modes.phiTip[k]    = a * c.modes.phiTip[m];     c.modes.phiTip[m]    *= b;
            c.modes.phiDamper[k] = a * c.modes.phiDamper[m];  c.modes.phiDamper[m] *= b;
            c.phiTipY[k]         = a * c.phiTipY[m];          c.phiTipY[m]         *= b;
            c.sigmaFree[k]   = t60ToSigma(std::max(fastT60, 0.005));
            c.sigmaDamper[k] = c.sigmaDamper[m];
            c.transient[k]   = true;
        }
    }
    // the click (tine.mode2FastShareByKey, docs/ATTACK_WINDOWS.md 28): tine mode 2 (the first tine mode at 5-10 f1)
    // splits like the tonebar, the copy decays fast and counts as transient (it dies within the attack)
    const double m2Share = p.tineMode2FastShareByKey.n ? std::min(std::max(double(p.tineMode2FastShareByKey(key)), 0.0), 1.0) : 0.0;
    const double m2T60 = p.tineMode2FastT60ByKey.n ? double(p.tineMode2FastT60ByKey(key)) : 0.0;
    if (m2Share > 0.0 && m2T60 > 0.0 && c.modes.count < kMaxModes) {
        const double f1 = c.modes.freq[c.modes.fundamental];
        int m2 = -1;
        for (int m = 0; m < c.modes.count; ++m)
            if (!c.transient[m] && c.modes.freq[m] > 5.0 * f1 && c.modes.freq[m] < 10.0 * f1
                && (m2 < 0 || c.modes.freq[m] < c.modes.freq[m2])) m2 = m;
        if (m2 >= 0) {
            const double a = std::sqrt(m2Share), b = std::sqrt(1.0 - m2Share);
            const int k = c.modes.count++;
            c.modes.freq[k] = c.modes.freq[m2];
            c.modes.tonebarShare[k] = c.modes.tonebarShare[m2];
            c.modes.phiStrike[k] = a * c.modes.phiStrike[m2];  c.modes.phiStrike[m2] *= b;
            c.modes.phiTip[k]    = a * c.modes.phiTip[m2];     c.modes.phiTip[m2]    *= b;
            c.modes.phiDamper[k] = a * c.modes.phiDamper[m2];  c.modes.phiDamper[m2] *= b;
            c.phiTipY[k]         = a * c.phiTipY[m2];          c.phiTipY[m2]         *= b;
            c.sigmaFree[k]   = t60ToSigma(std::max(m2T60, 0.002));
            c.sigmaDamper[k] = c.sigmaDamper[m2];
            c.transient[k]   = true;
        }
    }
    // the upper tonebar mode (docs/ATTACK_WINDOWS.md 5): a second detuned resonator above f1, struck with
    // the tine and dead within the attack; the short-lived lines of the recordings in 0-20 ms
    const double upDb = p.tonebarUpperDbByKey.n ? double(p.tonebarUpperDbByKey(key)) : double(p.tonebarUpperDb);
    const double upExp = p.tonebarUpperVelExpByKey.n ? double(p.tonebarUpperVelExpByKey(key))
                          : double(p.tonebarUpperVelExp);
    if (upDb > -60.0 && c.modes.count + 2 <= kMaxModes) {
        const int f0 = c.modes.fundamental;
        const double f1 = c.modes.freq[f0];
        const double ratio = p.tonebarUpperRatioByKey.n ? double(p.tonebarUpperRatioByKey(key))
                              : (p.tonebarUpperRatio > 0.0f ? double(p.tonebarUpperRatio)
                                 : 1.53 * std::pow(f1 / 329.6276, -0.14));
        const double fU = f1 * std::min(std::max(ratio, 1.10), 1.60);
        const double level = dbToGain(upDb);
        const double fast = std::min(std::max(double(p.tonebarUpperFastShare), 0.0), 1.0);
        const double share[2] = {fast, 1.0 - fast};
        const double t60[2] = {double(p.tonebarUpperT60), double(p.tonebarUpperSlowT60)};
        for (int q = 0; q < 2; ++q) {
            if (share[q] <= 0.0) continue;
            const int k = c.modes.count++;
            const double a = std::sqrt(level * share[q]);
            c.modes.freq[k] = fU;
            c.modes.tonebarShare[k] = 1.0;
            c.modes.phiStrike[k] = a * c.modes.phiStrike[f0];
            c.modes.phiTip[k]    = a * c.modes.phiTip[f0];
            c.modes.phiDamper[k] = 0.0;                 // the tonebar's own rest, not the tine's damper
            c.sigmaFree[k]   = t60ToSigma(std::max(t60[q], 0.005));
            c.sigmaDamper[k] = c.sigmaDamper[f0];
            c.phiTipY[k]     = 0.0;
            c.modeStrikeExp[k] = upExp;
            c.transient[k]   = true;
        }
    }
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
    // the hard core under the tip: engages at the compression a blow of coreSpeed reaches against
    // the rigid wall (the conservative Hunt-Crossley maximum); its own stiffness from its contact
    // time at 1 m/s like the tip's
    const double coreSpeed = p.hammerCoreSpeed.n ? double(p.hammerCoreSpeed(key)) : 0.0;
    if (coreSpeed > 0.0) {
        const double b = std::max(double(p.hammerCoreAlpha), 1.0), nb = b + 1.0;
        const double ib = std::sqrt(kPi) * std::tgamma(1.0 + 1.0 / nb) / std::tgamma(0.5 + 1.0 / nb);
        const double coreMs = p.hammerCoreMs.n ? double(p.hammerCoreMs(key)) : 0.3;
        const double dmaxCore = std::max(coreMs, 0.01) * 1e-3 / (2.0 * ib);
        c.hammerCoreDepth = std::pow(n * c.hammerMass * coreSpeed * coreSpeed / (2.0 * c.hammerK), 1.0 / n);
        c.hammerCoreK     = nb * c.hammerMass / (2.0 * std::pow(dmaxCore, nb));
        c.hammerCoreAlpha = b;
        const double knee = p.hammerCoreKnee.n ? std::min(std::max(double(p.hammerCoreKnee(key)), 0.0), 0.95) : 0.0;
        c.hammerCoreKnee  = knee * c.hammerCoreDepth;
    } else {
        c.hammerCoreDepth = c.hammerCoreK = c.hammerCoreKnee = 0.0;
        c.hammerCoreAlpha = 1.5;
    }

    // pickup
    const double w = std::max(double(p.pickupWidth(key)) * gapScale, 0.01) * 1e-3;
    c.pickupWidth    = w;
    c.pickupOffset   = (p.pickupOffset(key) + v.pickupOffset + voicing_) * w;
    c.pickupExponent = p.pickupExponent;

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
    for (int k = 0; k < kNumKeys; ++k)        // the force law of the mech modes (a prepareKey() applies it too)
        voices_[k].setMechExponent(std::max(double(p.mechExpByKey.n ? p.mechExpByKey(float(kFirstKey + k)) : p.mechExp), 0.5));
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

}

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
    const int k = note - kFirstKey;
    const bool wasDown = keyDown_[k];
    keyDown_[k] = false;
    const real fraction = pedalDamperFraction();
    voices_[k].setDamperFraction(fraction);                // half pedal aware
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
    for (int a = 0; a < activeCount_;) {
        const int k = active_[a];
        voices_[k].process(bufInt_, mi, force);
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
}

// The output: the coupling capacitor behind the pickups, then the level (out.gain, volume, out.trimDb). No effects,
// no clip: the application behind the instrument does that.
void Engine::process(float* out, int nFrames) {
    ScopedFlushDenormals noDenormals;
    int done = 0;
    while (done < nFrames) {
        const int m = std::min(kBlock, nFrames - done);
        renderBlock(m);
        const real gain = real(params_.masterGain) * volume_ * trim_;
        for (int i = 0; i < m; ++i) out[done + i] = float(hpf_.process(bufOut_[i]) * gain);
        done += m;
    }
}

} // namespace pmp
