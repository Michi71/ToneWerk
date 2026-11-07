// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// key_voice.cpp - modal reed, hammer contact or pad, pickup, damper
#include "key_voice.h"
#include "pickup_tables.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <complex>

#include "fastmath.h"

// The delta form of the modes and the flux difference depend on the order of the
// floating-point operations. Reassociation (-ffast-math, -fassociative-math) moves the float
// build from 87 to 59 dB signal-to-difference against double, so refuse it.
#if defined(__FAST_MATH__) || defined(__ASSOCIATIVE_MATH__)
#error "key_voice.cpp needs IEEE arithmetic: compile it with -fno-fast-math"
#endif

namespace pmp {
namespace {

// P - 1 = e^a (cos b + j sin b) - 1 with a = -sigma h, b = omega_d h, evaluated in double
// without cancellation: real part expm1(a) cos b - 2 sin^2(b/2)
std::complex<double> poleMinusOne(double sigma, double omegaD, double h) {
    const double a = -sigma * h, b = omegaD * h, sh = std::sin(0.5 * b);
    return {std::expm1(a) * std::cos(b) - 2.0 * sh * sh, std::exp(a) * std::sin(b)};
}

// e^(x + jy) - 1 with full relative precision: Taylor series for |z| <= 1/16 (argument
// halved as often as needed), then e^(2z) - 1 = E (E + 2). Only + - * on real.
// four modes in one compiler vector (a NEON register in float, two in double): the per-sample loops of the voice run over
// blocks of four with partial sums per lane, summed across the lanes at the end - another rounding than the ordered sum
#if defined(__GNUC__) || defined(__clang__)
#define PMP_VEC4 1
typedef real v4r __attribute__((vector_size(4 * sizeof(real))));
PMP_INLINE v4r ld4(const real* p) { v4r v; std::memcpy(&v, p, sizeof v); return v; }
PMP_INLINE void st4(real* p, v4r v) { std::memcpy(p, &v, sizeof v); }
PMP_INLINE real hsum4(v4r v) { return (v[0] + v[1]) + (v[2] + v[3]); }
#endif

PMP_INLINE void expm1c(real x, real y, real& er, real& ei) {
    int halvings = 0;
    while (std::fabs(x) + std::fabs(y) > real(0.0625) && halvings < 64) {
        x *= real(0.5);
        y *= real(0.5);
        ++halvings;
    }
    // z (1 + z/2 (1 + z/3 (1 + ... z/N))); truncation below 1e-9 (float) or 1e-17 relative
    constexpr int N = sizeof(real) < sizeof(double) ? 5 : 9;
    static constexpr real kInv[] = {0, 1, real(1.0 / 2), real(1.0 / 3), real(1.0 / 4),
                                    real(1.0 / 5), real(1.0 / 6), real(1.0 / 7),
                                    real(1.0 / 8), real(1.0 / 9)};
    real sr = 1, si = 0;
    for (int n = N; n >= 2; --n) {
        const real c = kInv[n];
        const real tr = (x * sr - y * si) * c, ti = (x * si + y * sr) * c;
        sr = real(1) + tr;
        si = ti;
    }
    er = x * sr - y * si;
    ei = x * si + y * sr;
    for (; halvings > 0; --halvings) {
        const real r = er * (er + real(2)) - ei * ei;
        ei = real(2) * ei * (er + real(1));
        er = r;
    }
}

// P - 1 = (P_free - 1) + P_free (e^(x + jy) - 1): the damped pole from the free one
PMP_INLINE void shiftPole(real br, real bi, real x, real y, real& pr, real& pi) {
    real er, ei;
    expm1c(x, y, er, ei);
    pr = br + er + (br * er - bi * ei);
    pi = bi + ei + (br * ei + bi * er);
}

} // namespace

void KeyVoice::configure(const KeyConfig& cfg, double fs) {
    fs_ = fs;
    T_ = 1.0 / fs;
    n_ = std::min(cfg.modes.count, kMaxModes);
    n4_ = (n_ + 3) & ~3;
    for (int m = n_; m < kModeSlots; ++m) {                  // the padding of the vector loops stays silent
        zr_[m] = zi_[m] = qr_[m] = qi_[m] = sqr_[m] = sqi_[m] = sbr_[m] = sbi_[m] = 0;
        cTip_[m] = cStrike_[m] = vRe_[m] = vIm_[m] = 0;
    }
    K_ = std::max(1, cfg.hammerSubsteps);
    const double hK = T_ / K_;
    for (int m = 0; m < n_; ++m) {
        const double w0  = 2.0 * kPi * cfg.modes.freq[m];
        const double sig = cfg.sigmaFree[m];
        const double wd  = std::sqrt(std::max(w0 * w0 - sig * sig, 1e-4 * w0 * w0));
        const std::complex<double> p1 = poleMinusOne(sig, wd, T_), pk = poleMinusOne(sig, wd, hK);
        fqr_[m]  = real(p1.real());  fqi_[m]  = real(p1.imag());
        fsqr_[m] = real(pk.real());  fsqi_[m] = real(pk.imag());
        sigF_[m] = sigma_[m] = real(sig);
        wdF_[m]  = omegaD_[m] = real(wd);
        sd_[m]     = real(cfg.sigmaDamper[m]);
        phiTip_[m] = real(cfg.modes.phiTip[m]);
        phiDamp_[m] = real(cfg.modes.phiDamper[m]);
        vRe_[m]    = real(cfg.modes.phiStrike[m]);
    }
    hT_ = real(T_);
    hK_ = real(hK);
    damperRate_ = real(1.0 / (std::max(cfg.damperRampSec, 1e-4) * fs_));
    chokeSigma_ = real(t60ToSigma(0.02));
    chokeSamples_ = std::max(1, int(0.03 * fs_));
    hk_ = hkStrike_ = real(cfg.hammerK);
    speedHardening_ = real(cfg.hammerSpeedHardening);
    ha_ = real(cfg.hammerAlpha);
    hl_ = real(cfg.hammerLambda);
    hInvM_ = real(1.0 / cfg.hammerMass);
    hdt_ = real(hK);
    pluck_ = cfg.excitation == 1;
    pluckK_ = real(cfg.pluckK);
    pluckAdh_ = real(cfg.pluckAdhesion);
    pluckVelExp_ = real(cfg.pluckVelExp);
    pluckFallSec_ = real(cfg.pluckReleaseSec);
    noContactLimit_ = int(0.003 * fs_);
    hammerAgeLimit_ = int(0.25 * fs_);
    sleepEnergy_ = real(cfg.sleepEnergy);
    pExp_ = cfg.pickupExponent;
    electrostatic_ = cfg.pickupType >= 1 && cfg.pickupType <= 3;
    slot_ = cfg.pickupType >= 2;
    diff_ = cfg.pickupType == 3;
    onsetHz0_ = real(cfg.pickupOnsetHz);
    onsetFs_ = real(fs);
    onsetDecay_ = cfg.pickupOnsetMs > 0.0 ? real(std::exp(-16.0 / (cfg.pickupOnsetMs * 1e-3 * fs))) : real(0);
    onsetFc_ = 0;
    table_ = cfg.pickupType == 4;
    if (table_) {
        law_ = &kPickupLaws[std::min(std::max(cfg.pickupSection, 0), 3)];
        lawSign_ = cfg.pickupSection < 2 ? real(1) : real(-1);       // bass reeds swing into the band on the first stroke, the treble away
        hpfOn_ = cfg.pickupHpfHz > 0.0;
        const double g0 = 3.14159265358979323846 * cfg.pickupHpfHz / fs;     // trapezoid: a differentiator inside a one-pole lag
        hpfA_ = real((1.0 - g0) / (1.0 + g0));
        hpfB_ = real(1.0 / (1.0 + g0));
    }
    fastPickup_ = pExp_ == 1.5;
    pole_ = fastPickup_ && !electrostatic_ && !table_ && cfg.pickupPole > 0.0;
    poleA_ = pole_ ? real(cfg.pickupPole) : real(0);
    inv2a_ = pole_ ? real(0.5 / cfg.pickupPole) : real(0);
    unitPickup_ = pExp_ == 1.0;
    invW_ = real(1.0 / cfg.pickupWidth);
    x0_ = real(cfg.pickupOffset);
    warp_ = real(cfg.pickupWarp);
    // the 2D field solve is one cross-section; the comb reads a stretch of the reed whose amplitude drops towards the
    // root by pickupSpan (pickup.comb through the fundamental's shape): the law averaged over it, the amplitude
    // scaled on 4 Gauss-Legendre points (docs/REEDS.md 31)
    span_ = table_ && cfg.pickupSpan > 0.0;
    if (span_) tabulateSpan(std::min(cfg.pickupSpan, 0.9));
    glide_ = real(cfg.reedGlide * 0.69314718055994531 / 1200.0);     // cents -> relative frequency
    fastMode_ = cfg.fastMode < n_ ? cfg.fastMode : -1;
    // the fast copy's share read at the slope of the law at rest (docs/REEDS.md 29): in the set the harmonics do not
    // follow the fundamental's fast drop, so that part does not swing through the comb
    phiLin_ = 0;
    linSlope_ = 0;
    if (fastMode_ > 0 && cfg.fastLinear > 0.0) {
        const real l = real(std::min(cfg.fastLinear, 1.0));
        phiLin_ = l * phiTip_[fastMode_];
        phiTip_[fastMode_] *= real(1) - l;
        const real h = real(1e-3) * real(cfg.pickupWidth);
        if (table_) {
            linSlope_ = (tableLaw(h) - tableLaw(-h)) / (real(2) * h);
        } else {
            const bool hpf = hpfOn_;
            hpfOn_ = false;
            resetPickup(-h);
            linSlope_ = pickupDelta(real(2) * h) / (real(2) * h);
            hpfOn_ = hpf;
        }
    }
    setGain(cfg.gain);
    levelGlide_ = real(1.0 - std::exp(-16.0 / (0.003 * fs_)));   // per 16 samples, ~3 ms
    reset();
}

void KeyVoice::setGain(double g) {
    gain_ = real(-g * fs_);
    keyGain_ = real(g);
    forceGain_ = keyGain_ * mechScale_;
}

void KeyVoice::setMechScale(double s) {
    mechScale_ = real(s);
    forceGain_ = keyGain_ * mechScale_;
}

void KeyVoice::reset() {
    for (int m = 0; m < n_; ++m) zr_[m] = zi_[m] = 0;
    damper_ = damperTarget_ = 1;
    damperMoving_ = false;
    choke_ = 0;
    chokeLeft_ = 0;
    hammerActive_ = contact_ = false;
    knockLeft_ = 0;
    updateCoefficients();
    resetPickup(0);
    onsetFc_ = onsetLp_ = onsetLp2_ = 0;
    active_ = false;
}

// Damping of mode m for the current damper position and choke, as offsets from the free
// reed: dSig in the decay rate, dWd in the damped frequency (wd^2 = w0^2 - sig^2 rewritten
// as wd^2 = wdF^2 - dSig (sig + sigF), so neither offset suffers from cancellation).
PMP_INLINE KeyVoice::Shift KeyVoice::shift(int m) const {
    Shift s;
    const real sf = sigF_[m], wf = wdF_[m];
    s.dSig = damper_ * damperScale_ * sd_[m] + choke_;
    s.sig  = sf + s.dSig;
    if (s.dSig == 0) {                                      // damper lifted: the free reed
        s.wd  = wf;
        s.dWd = 0;
        return s;
    }
    const real wd2 = wf * wf - s.dSig * (s.sig + sf);
    const real wMin2 = real(1e-4) * (sf * sf + wf * wf);     // as in configure()
    if (wd2 >= wMin2) {
        s.wd  = std::sqrt(wd2);
        s.dWd = -s.dSig * (s.sig + sf) / (s.wd + wf);
    } else {                                                // overdamped (choked bass mode)
        s.wd  = std::sqrt(wMin2);
        s.dWd = s.wd - wf;
    }
    return s;
}

void KeyVoice::updateCoefficients() {
    for (int m = 0; m < n_; ++m) {
        const Shift s = shift(m);

        // keep the physical state (q, dq/dt) when the damping changes:
        // zr = dq/dt + sigma q, zi = omega_d q
        const real q = zi_[m] / omegaD_[m];
        zr_[m] += (s.sig - sigma_[m]) * q;
        zi_[m]  = s.wd * q;
        sigma_[m]  = s.sig;
        omegaD_[m] = s.wd;

        if (s.dSig == 0) {
            qr_[m] = fqr_[m];
            qi_[m] = fqi_[m];
        } else {
            shiftPole(fqr_[m], fqi_[m], -s.dSig * hT_, s.dWd * hT_, qr_[m], qi_[m]);
        }
        bqr_[m] = qr_[m];
        bqi_[m] = qi_[m];
        const real iw = real(1) / s.wd;
        cTip_[m]    = phiTip_[m] * iw;
        if (m == fastMode_) cLin_ = phiLin_ * iw;
        cStrike_[m] = vRe_[m] * iw;
        vIm_[m]     = vRe_[m] * s.sig * iw;
    }
    // the sub-step coefficients are only needed while the hammer is in play
    if (hammerActive_) updateSubsteps();
    else substepStale_ = true;
}

// Slip at the clamp lengthens the reed at large swing: the frequency drops in proportion to the swing of the
// fundamental mode at the tip, |phi_tip / omega_d| |z|, in mm of the pickup law. Every mode turns by the same
// ratio d: P = P_base e^(j omega_d T d), with |omega_d T d| < 1e-4 to second order (error ~1e-17 per sample).
// Runs every 16 samples while the hammer is off the reed; d is recomputed from the base poles, nothing accumulates.
void KeyVoice::applyGlide() {
    // the fundamental's swing at the tip: the mode and its fast copy (same frequency, in phase from the strike)
    real xr = zr_[0] * cTip_[0], xi = zi_[0] * cTip_[0];
    if (fastMode_ > 0) {
        const real c = cTip_[fastMode_] + cLin_;
        xr += zr_[fastMode_] * c;
        xi += zi_[fastMode_] * c;
    }
    const real a = std::sqrt(xr * xr + xi * xi) * invW_;
    const real d = -glide_ * a;
    for (int m = 0; m < n_; ++m) {
        const real e = omegaD_[m] * hT_ * d, h = real(0.5) * e * e;
        const real br = bqr_[m], bi = bqi_[m];
        qr_[m] = br - h * (real(1) + br) - bi * e;
        qi_[m] = bi - h * bi + (real(1) + br) * e;
    }
}

void KeyVoice::updateSubsteps() {
    for (int m = 0; m < n_; ++m) {
        const Shift s = shift(m);
        real pr = fsqr_[m], pi = fsqi_[m];
        if (s.dSig != 0) shiftPole(fsqr_[m], fsqi_[m], -s.dSig * hK_, s.dWd * hK_, pr, pi);
        sqr_[m] = pr;
        sqi_[m] = pi;
        // ZOH input gain (P - 1) / (-sigma + j omega_d) * phi(strike)
        const real g = vRe_[m] / (s.sig * s.sig + s.wd * s.wd);
        sbr_[m] = (s.wd * pi - s.sig * pr) * g;
        sbi_[m] = -(s.sig * pi + s.wd * pr) * g;
    }
    substepStale_ = false;
}

std::complex<double> KeyVoice::pole(int m, bool substep) const {
    return substep ? std::complex<double>(sqr_[m], sqi_[m])
                   : std::complex<double>(qr_[m], qi_[m]);
}

void KeyVoice::strike(double speed, double level, double contactScale) {
    levelTarget_ = real(level);
    if (!active_) level_ = levelTarget_;
    if (chokeLeft_ > 0) {                // struck again while being stolen: keep it
        choke_ = 0;
        chokeLeft_ = 0;
        updateCoefficients();
    }
    if (!active_) {                      // a voice from silence starts its own timeline: every strike the same
        resetPickup(tip());
        frame_ = 0;
    }
    active_ = true;
    hammerActive_ = true;
    onsetFc_ = onsetHz0_;                // the strike high pass starts here and glides down
    if (substepStale_) updateSubsteps();
    contact_ = false;
    hammerAge_ = noContact_ = 0;
    impulse_ = 0;
    // the hammer arrives at the (possibly vibrating) reed with the given speed; the pad rests on it and goes up with
    // the key
    real ws = 0;
    for (int m = 0; m < n_; ++m) ws += cStrike_[m] * zi_[m];
    xh_ = ws;
    vh_ = std::max(real(speed), real(1e-4));
    if (pluck_) {
        // a faster key tears the bond at a larger force (the adhesive is viscoelastic)
        pluckLimit_ = pluckAdh_ * real(contactScale) * powr(vh_, pluckVelExp_);
        pluckTorn_ = -1;
        pluckFall_ = std::max(1, int(double(pluckFallSec_) / double(hdt_) + 0.5));
        pluckFallW_ = real(1.0 / pluckFall_);
        pluckStep_ = 0;
        return;
    }
    hkStrike_ = hk_ * real(contactScale);
    if (speedHardening_ != 0 && vh_ > real(2)) {
        const real t = std::min(vh_ - real(2), real(1));
        const real hardness = std::min(std::max(speedHardening_ * t * t, real(-200)), real(200));
        hkStrike_ = hk_ * powr(real(2), (ha_ + real(1)) * hardness / real(100));
    }
}

void KeyVoice::damperHit(real fraction, real releaseSpeed) {
    if (knock_ == 0 || fraction <= 0) return;
    // The felt lands where the reed is and at the key's release speed: a swinging reed and a fast release give a
    // stronger blow. `swing` is the displacement at the damper point in pickup widths (clamped at 2); the factors are
    // 1 at the MIDI default release (64) and a mid swing, so the calibrated knock keeps its level.
    real x = 0;
    for (int m = 0; m < n_; ++m) x += phiDamp_[m] * (zi_[m] / omegaD_[m]);
    const real swing = std::min(std::fabs(x * invW_), real(2));
    const real speed = std::clamp(releaseSpeed, real(0), real(1));
    const real releaseScale = (real(0.75) + real(0.5) * speed) * (real(0.5) + real(0.5) * swing);
    if (!active_) {                      // a silent reed rings from the blow too
        resetPickup(tip());
        level_ = levelTarget_ = real(1);
        active_ = true;
    }
    const double f1 = n_ > 0 ? double(wdF_[0]) / (2.0 * kPi) : 0.0;
    knockN_ = (knockPeriods_ > 0 && f1 > 0) ? std::max(2, int(double(knockPeriods_) / f1 * fs_ + 0.5)) : 0;
    knockScale_ = knock_ * fraction * releaseScale;
    if (knockN_ == 0) {                  // an impulse
        for (int m = 0; m < n_; ++m) zr_[m] += phiDamp_[m] * knockScale_;
        return;
    }
    knockLeft_ = knockN_;                // the blow follows sample by sample (knockStep)
    knockC_ = real(std::cos(kPi / double(knockN_)));   // cos and sin of the raised cosine's phase at the first sample's centre,
    knockS_ = real(std::sin(kPi / double(knockN_)));   // turned on by a rotation (no libm per sample)
    knockRc_ = real(std::cos(2.0 * kPi / double(knockN_)));
    knockRs_ = real(std::sin(2.0 * kPi / double(knockN_)));
}

// A felt, not a needle: the same impulse spread over a raised cosine of knockN_ samples, which leaves the high modes
// (and so a click) out. It is added to the modal velocity at every sample: added in groups of 16 samples it was a
// staircase whose steps (2.76 kHz at 44.1 kHz) struck the reed's high modes - a click at every release of the Pianet,
// 2-10 kHz at -14 dB re the tone in the bass, none in the recordings (docs/REEDS.md 24).
PMP_INLINE void KeyVoice::knockStep() {
    const real w = (real(1) - knockC_) * knockScale_ / real(knockN_);
    for (int m = 0; m < n_; ++m) zr_[m] += phiDamp_[m] * w;
    const real c = knockC_ * knockRc_ - knockS_ * knockRs_;
    knockS_ = knockS_ * knockRc_ + knockC_ * knockRs_;
    knockC_ = c;
    --knockLeft_;
}

void KeyVoice::setDamperScale(double scale) {
    const real s = real(scale);
    if (s == damperScale_) return;
    damperScale_ = s;
    updateCoefficients();      // keeps the state, retunes the damped poles
}

void KeyVoice::setDamper(bool engaged) {
    damperTarget_ = engaged ? real(1) : real(0);
    if (!engaged && damper_ != 0) {    // the damper is lifted before the hammer arrives
        damper_ = 0;
        updateCoefficients();
    }
    damperMoving_ = damper_ != damperTarget_;
}

void KeyVoice::setDamperFraction(real f) {
    f = std::clamp(f, real(0), real(1));
    // a 16-sample grid (it kept the removed fixed-point engine bit-identical; the presets
    // are calibrated on it): the target is rounded to the grid first
    const real step = real(16) * damperRate_;
    damperTarget_ = std::min(std::floor(f / step + real(0.5)) * step, real(1));
    damperMoving_ = damper_ != damperTarget_;   // the rail ramps there, no snap
}

// The extra damping goes through the same state-preserving coefficient update as the
// damper: the reed keeps its position and speed, so the output stays continuous (no click).
void KeyVoice::choke() {
    if (!active_ || chokeLeft_ > 0) return;
    hammerActive_ = false;
    choke_ = chokeSigma_;
    chokeLeft_ = chokeSamples_;
    updateCoefficients();
}

void KeyVoice::setPickupOffset(double x0) {
    x0_ = real(x0);
    resetPickup(xPrev_);               // moving the pickup must not produce a voltage step
}

real KeyVoice::tip() const {
#if defined(PMP_VEC4)
    v4r x = {};
    for (int b = 0; b < n4_; b += 4) x += ld4(cTip_ + b) * ld4(zi_ + b);
    return hsum4(x);
#else
    real x = 0;
    for (int m = 0; m < n_; ++m) x += cTip_[m] * zi_[m];
    return x;
#endif
}

// The reed reaching the plate: the comb stops it a little before the pole of 1 / (1 - s)
constexpr real kPlateStop = real(0.98);

void KeyVoice::syncPickup(real x) {
    linPrev_ = fastMode_ > 0 ? cLin_ * zi_[fastMode_] : real(0);
    const real s = (x - x0_) * invW_;
    xPrev_ = x;
    if (table_) {
        lawPrev_ = tableLaw(x);
        return;
    }
    if (electrostatic_) {
        sPrev_ = std::min(std::max(s, -kPlateStop), kPlateStop);
        tPrev_ = slot_ ? real(1) - sPrev_ * sPrev_ : real(1) - sPrev_;
        return;
    }
    sPrev_ = s;
    tPrev_ = real(1) + s * s;
    rtPrev_ = std::sqrt(tPrev_);
    if (pole_) {
        rpPrev_ = std::sqrt(real(1) + (s + poleA_) * (s + poleA_));
        rmPrev_ = std::sqrt(real(1) + (s - poleA_) * (s - poleA_));
    }
}

void KeyVoice::resetPickup(real x) {
    syncPickup(x);
    hpfW_ = 0;
    if (!fastPickup_ && !unitPickup_ && !electrostatic_ && !table_) phiPrev_ = std::exp(-pExp_ * std::log(double(tPrev_)));
}

PMP_INLINE real KeyVoice::linStep() {
    if (linSlope_ == 0) return 0;
    const real x = cLin_ * zi_[fastMode_];
    const real d = x - linPrev_;
    linPrev_ = x;
    return d;
}

// the fast copy over a free run, from the state before stepFreeRun: the same recursion as its lane there
PMP_INLINE void KeyVoice::linRun(int count, real* dl) {
    if (linSlope_ == 0) {
        for (int s = 0; s < count; ++s) dl[s] = 0;
        return;
    }
    real zr = zr_[fastMode_], zi = zi_[fastMode_];
    const real qr = qr_[fastMode_], qi = qi_[fastMode_];
    for (int s = 0; s < count; ++s) {
        const real dr = zr * qr - zi * qi;
        const real di = zr * qi + zi * qr;
        zr += dr;
        zi += di;
        const real x = cLin_ * zi;
        dl[s] = x - linPrev_;
        linPrev_ = x;
    }
}

PMP_INLINE real KeyVoice::pickupDelta(real dx, real dl) {
    const real x = xPrev_ + dx;
    if (table_) {
        const real law = tableLaw(x);
        const real d = law - lawPrev_ + linSlope_ * dl;
        xPrev_ = x;
        lawPrev_ = law;
        if (!hpfOn_) return d;
        hpfW_ = hpfA_ * hpfW_ - hpfB_ * d;
        return hpfW_;
    }
    if (electrostatic_) {
        // Phi = 1 / t, t = 1 - s:  Phi(t) - Phi(t0) = (s - s0) / (t t0), no cancellation;
        // slot, t = 1 - s^2:       (s - s0) (s + s0) / (t t0)
        // differential (the two plates subtracted), Phi = s / t, t = 1 - s^2:  (s - s0) (1 + s s0) / (t t0)
        const real s = std::min(std::max((x - x0_) * invW_, -kPlateStop), kPlateStop);
        const real t = slot_ ? real(1) - s * s : real(1) - s;
        const real d = diff_ ? (s - sPrev_) * (real(1) + s * sPrev_) / (t * tPrev_)
                     : slot_ ? (s - sPrev_) * (s + sPrev_) / (t * tPrev_) : (s - sPrev_) / (t * tPrev_);
        xPrev_ = x;
        sPrev_ = s;
        tPrev_ = t;
        return d + linSlope_ * dl;
    }
    const real s = (x - x0_) * invW_;
    const real t = real(1) + s * s;
    real d;
    if (pole_) {
        // the point law (1 + s^2)^-1.5 = F'(s), F = s / sqrt(1 + s^2), averaged over the pole face s - a .. s + a:
        // Phi = (F(s + a) - F(s - a)) / 2a, and F(u) - F(u0) = (u - u0)(1 + r r0 - u u0) / ((r + r0) r r0) - the
        // numerator is >= 1, no cancellation
        const real ds = dx * invW_;
        const real up = s + poleA_, up0 = sPrev_ + poleA_, um = s - poleA_, um0 = sPrev_ - poleA_;
        const real rp = std::sqrt(real(1) + up * up), rm = std::sqrt(real(1) + um * um);
        const real fp = (real(1) + rp * rpPrev_ - up * up0) / ((rp + rpPrev_) * rp * rpPrev_);
        const real fm = (real(1) + rm * rmPrev_ - um * um0) / ((rm + rmPrev_) * rm * rmPrev_);
        d = ds * (fp - fm) * inv2a_;
        rpPrev_ = rp;
        rmPrev_ = rm;
    } else if (fastPickup_) {
        // Phi = t^-1.5:  Phi(t) - Phi(t0) = (t0 - t)(t0 + sqrt(t0 t) + t) /
        //                                   ((sqrt(t0) + sqrt(t)) t0^1.5 t^1.5)
        // with t0 - t = -(dx / w)(s0 + s) instead of a difference of nearly equal values
        const real rt = std::sqrt(t);
        const real dt = -(dx * invW_) * (sPrev_ + s);
        d = dt * (tPrev_ + rtPrev_ * rt + t) / ((rtPrev_ + rt) * (tPrev_ * rtPrev_) * (t * rt));
        rtPrev_ = rt;
    } else if (unitPickup_) {
        // Phi = 1 / t:  Phi(t) - Phi(t0) = (t0 - t) / (t t0), again with t0 - t = -(dx / w)(s0 + s): no libm
        d = -(dx * invW_) * (sPrev_ + s) / (t * tPrev_);
    } else {                                   // any other exponent: libm per sample (no preset uses it)
        const double phi = std::exp(-pExp_ * std::log(double(t)));
        d = real(phi - phiPrev_);
        phiPrev_ = phi;
    }
    xPrev_ = x;
    sPrev_ = s;
    tPrev_ = t;
    return d + linSlope_ * dl;
}

// The pickup over a run of up to 16 samples whose tip displacements are known (stepFreeRun): the samples are independent
// once the positions are summed up, so square root and division of the magnetic pickup run over arrays of 16 and vectorize -
// four per instruction instead of a dependent chain per sample. Same formulas as pickupDelta.
void KeyVoice::pickupRun(int count, const real* dx, const real* dl, real* d) {
    if (!fastPickup_ || electrostatic_ || table_ || pole_) {   // the spline of the Wurlitzer gathers from a table: faster sample by sample
        for (int j = 0; j < count; ++j) d[j] = pickupDelta(dx[j], dl[j]);
        return;
    }
    real s[17], t[17], rt[17];
    s[0] = sPrev_; t[0] = tPrev_; rt[0] = rtPrev_;
    real x = xPrev_;
    for (int j = 0; j < count; ++j) { x += dx[j]; s[j + 1] = (x - x0_) * invW_; }
    for (int j = 1; j <= count; ++j) { t[j] = real(1) + s[j] * s[j]; rt[j] = std::sqrt(t[j]); }
    for (int j = 1; j <= count; ++j) {
        const real dt = -(dx[j - 1] * invW_) * (s[j - 1] + s[j]);
        d[j - 1] = dt * (t[j - 1] + rt[j - 1] * rt[j] + t[j]) / ((rt[j - 1] + rt[j]) * (t[j - 1] * rt[j - 1]) * (t[j] * rt[j]))
                 + linSlope_ * dl[j - 1];
    }
    xPrev_ = x; sPrev_ = s[count]; tPrev_ = t[count]; rtPrev_ = rt[count];
}

double KeyVoice::energy() const {
    real e = 0;
    for (int m = 0; m < n_; ++m) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
    return 0.5 * double(e);
}

// comparable across keys: auto-voicing sets the sleep threshold 120 dB below the energy of
// a velocity-100 strike, and the output gains make those strikes equally loud
real KeyVoice::level() const {
    real e = 0;
    for (int m = 0; m < n_; ++m) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
    return real(0.5) * e * level_ * level_ / std::max(sleepEnergy_, real(1e-30));
}

double KeyVoice::tipDisplacement() const {
    double x = 0.0;
    for (int m = 0; m < n_; ++m) x += double(cTip_[m]) * zi_[m];
    return x;
}

void KeyVoice::advanceDamper(int n) {
    const real step = real(n) * damperRate_;
    if (damper_ < damperTarget_) damper_ = std::min(damper_ + step, damperTarget_);
    else                         damper_ = std::max(damper_ - step, damperTarget_);
    damperMoving_ = damper_ != damperTarget_;
    updateCoefficients();
}

// The free decay over a run of up to 16 samples (the housekeeping grid of process()): a block of four modes stays in
// registers over the run, the tip displacement of every sample is summed lane-wise like in stepFree() - the same
// operations in the same order, so the result equals stepFree() sample by sample, with a quarter of the loads and stores.
PMP_INLINE void KeyVoice::stepFreeRun(int count, real* dx) {
#if defined(PMP_VEC4)
    v4r acc[16];
    for (int s = 0; s < count; ++s) acc[s] = v4r{};
    for (int b = 0; b < n4_; b += 4) {
        v4r zr = ld4(zr_ + b), zi = ld4(zi_ + b);
        const v4r qr = ld4(qr_ + b), qi = ld4(qi_ + b), ct = ld4(cTip_ + b);
        for (int s = 0; s < count; ++s) {
            const v4r dr = zr * qr - zi * qi;
            const v4r di = zr * qi + zi * qr;
            zr += dr;
            zi += di;
            acc[s] += ct * di;
        }
        st4(zr_ + b, zr);
        st4(zi_ + b, zi);
    }
    for (int s = 0; s < count; ++s) dx[s] = hsum4(acc[s]);
#else
    for (int s = 0; s < count; ++s) dx[s] = stepFree();
#endif
}

PMP_INLINE real KeyVoice::stepFree() {
#if defined(PMP_VEC4)
    v4r dx = {};
    for (int b = 0; b < n4_; b += 4) {
        const v4r zr = ld4(zr_ + b), zi = ld4(zi_ + b), qr = ld4(qr_ + b), qi = ld4(qi_ + b);
        const v4r dr = zr * qr - zi * qi;
        const v4r di = zr * qi + zi * qr;
        st4(zr_ + b, zr + dr);
        st4(zi_ + b, zi + di);
        dx += ld4(cTip_ + b) * di;
    }
    return hsum4(dx);
#else
    real dx = 0;
    for (int m = 0; m < n_; ++m) {
        const real dr = zr_[m] * qr_[m] - zi_[m] * qi_[m];
        const real di = zr_[m] * qi_[m] + zi_[m] * qr_[m];
        zr_[m] += dr;
        zi_[m] += di;
        dx += cTip_[m] * di;
    }
    return dx;
#endif
}

// The pad's force: while bonded, the foam between the rising pad and the reed pulls with pluckK (x_pad - x_reed), both
// ways (the adhesive holds), until the pull reaches the adhesion; then the bond tears and the force falls to zero within
// pluckFall_ sub-steps (a smoothstep, no libm). The key moves at its speed, unaffected by the reed (a hand on a lever).
PMP_INLINE real KeyVoice::pluckForce(real ws) {
    if (pluckTorn_ < 0) {
        xh_ += vh_ * hdt_;
        const real F = pluckK_ * (xh_ - ws);
        if (F < pluckLimit_ && hammerAge_ < hammerAgeLimit_) return F;
        pluckTorn_ = F;
        pluckStep_ = 0;
    }
    if (pluckStep_ >= pluckFall_) return 0;
    const real t = (real(pluckStep_++) + real(0.5)) * pluckFallW_;
    return pluckTorn_ * (real(1) - t * t * (real(3) - real(2) * t));
}

real KeyVoice::stepHammer() {
    const int K = K_;
    const real dt = hdt_;
    real dx = 0, fSum = 0;
    bool touched = false;
    for (int k = 0; k < K; ++k) {
        real ws = 0, vs = 0;
#if defined(PMP_VEC4)
        {
            v4r w = {}, v = {};
            for (int b = 0; b < n4_; b += 4) {
                const v4r zr = ld4(zr_ + b), zi = ld4(zi_ + b);
                w += ld4(cStrike_ + b) * zi;
                v += ld4(vRe_ + b) * zr - ld4(vIm_ + b) * zi;
            }
            ws = hsum4(w);
            vs = hsum4(v);
        }
#else
        for (int m = 0; m < n_; ++m) {
            ws += cStrike_[m] * zi_[m];
            vs += vRe_[m] * zr_[m] - vIm_[m] * zi_[m];
        }
#endif
        const real delta = xh_ - ws;                 // felt compression
        real F = 0;
        if (pluck_) {
            F = pluckForce(ws);
            touched = true;
        } else if (delta > 0) {
            F = hkStrike_ * powr(delta, ha_) * (real(1) + hl_ * (vh_ - vs));
            if (F < 0) F = 0;
            touched = true;
        }
#if defined(PMP_VEC4)
        {
            v4r d = {};
            for (int b = 0; b < n4_; b += 4) {
                const v4r zr = ld4(zr_ + b), zi = ld4(zi_ + b), sqr = ld4(sqr_ + b), sqi = ld4(sqi_ + b);
                const v4r dr = zr * sqr - zi * sqi + ld4(sbr_ + b) * F;
                const v4r di = zr * sqi + zi * sqr + ld4(sbi_ + b) * F;
                st4(zr_ + b, zr + dr);
                st4(zi_ + b, zi + di);
                d += ld4(cTip_ + b) * di;
            }
            dx += hsum4(d);
        }
#else
        for (int m = 0; m < n_; ++m) {
            const real dr = zr_[m] * sqr_[m] - zi_[m] * sqi_[m] + sbr_[m] * F;
            const real di = zr_[m] * sqi_[m] + zi_[m] * sqr_[m] + sbi_[m] * F;
            zr_[m] += dr;
            zi_[m] += di;
            dx += cTip_[m] * di;
        }
#endif
        if (!pluck_) {                               // symplectic Euler for the hammer
            vh_ -= F * hInvM_ * dt;
            xh_ += vh_ * dt;
        }
        fSum += F;
    }
    hammerForce_ = fSum / real(K);
    impulse_ += fSum * dt;
    if (pluck_) {                                    // the pad has let go: the reed swings free
        ++hammerAge_;
        if (pluckTorn_ >= 0 && pluckStep_ >= pluckFall_) hammerActive_ = false;
        return dx;
    }
    if (touched) { contact_ = true; noContact_ = 0; }
    else if (contact_) ++noContact_;
    ++hammerAge_;
    // the hammer is caught (backcheck) a few ms after it left the reed
    if ((contact_ && noContact_ > noContactLimit_) || hammerAge_ > hammerAgeLimit_)
        hammerActive_ = false;
    return dx;
}

namespace {
// natural cubic spline over uniform knots, flat outside them (the capacitance law and its average along the comb)
PMP_INLINE real splineEval(const float* y, const float* m, int n, real u0, real du, real u) {
    real p = (u - u0) / du;
    const real top = real(n - 1) - real(1e-4);
    // NaN fails both bounds, and int(NaN) is INT_MIN on x86 (0 on ARM): a reed that ran away (extreme voicing) read far
    // outside the table and crashed. Written so that NaN lands on the first knot.
    p = p >= real(0) ? (p > top ? top : p) : real(0);
    const int i = int(p);
    const real t = p - real(i), a = real(1) - t;
    const real h2 = du * du * (real(1) / real(6));
    return a * real(y[i]) + t * real(y[i + 1]) +
           ((a * a * a - a) * real(m[i]) + (t * t * t - t) * real(m[i + 1])) * h2;
}
}

// the capacitance law: natural cubic spline over uniform knots, flat outside the table. pickup.warp bends the
// penetration axis (u + warp u^2) to correct the 2D solve's notch spacing; 0 is the table as it is.
PMP_INLINE real KeyVoice::lawEval(real u) const {
    const PickupLaw& L = *law_;
    const real uu = u + warp_ * u * u;
    return splineEval(L.y, L.m, L.n, real(L.u0), real(L.du), uu);
}

// The law averaged along the comb: the amplitude falls from the read point towards the root by `drop` (linear in the
// shape near the tip), sampled on 4 Gauss-Legendre points and tabulated over u = sign x / w at the table's spacing, its
// natural spline solved here: one spline per sample in the audio path, as without the comb
void KeyVoice::tabulateSpan(double drop) {
    static const double t[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
    static const double w[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    double s[4];
    for (int j = 0; j < 4; ++j) s[j] = 1.0 - 0.5 * drop * (1.0 + t[j]);
    const PickupLaw& L = *law_;
    const double c = double(x0_) * double(invW_), lo = double(L.u0), hi = lo + double(L.du) * (L.n - 1);
    // every scale's argument s u - c leaves the table on the same side outside this range: the average is flat there
    double a = 1e30, b = -1e30;
    for (int j = 0; j < 4; ++j) {
        a = std::min(a, (lo + c) / s[j]);
        b = std::max(b, (hi + c) / s[j]);
    }
    const int n = std::min(kSpanKnots, int(std::ceil((b - a) / double(L.du))) + 1);
    const double du = (b - a) / (n - 1);
    for (int i = 0; i < n; ++i) {
        double v = 0.0;
        for (int j = 0; j < 4; ++j) v += 0.5 * w[j] * double(lawEval(real(s[j] * (a + i * du) - c)));
        spanY_[i] = float(v);
    }
    // natural spline: m[i-1] + 4 m[i] + m[i+1] = 6 (y[i+1] - 2 y[i] + y[i-1]) / du^2, m = 0 at both ends (Thomas)
    double cp[kSpanKnots], dp[kSpanKnots];
    cp[0] = 0.0;
    dp[0] = 0.0;
    for (int i = 1; i < n - 1; ++i) {
        const double r = 6.0 * (double(spanY_[i + 1]) - 2.0 * double(spanY_[i]) + double(spanY_[i - 1])) / (du * du);
        const double den = 4.0 - cp[i - 1];
        cp[i] = 1.0 / den;
        dp[i] = (r - dp[i - 1]) / den;
    }
    spanM_[n - 1] = 0.0f;
    double next = 0.0;
    for (int i = n - 2; i >= 1; --i) {
        next = dp[i] - cp[i] * next;
        spanM_[i] = float(next);
    }
    spanM_[0] = 0.0f;
    spanU0_ = real(a);
    spanDu_ = real(du);
    spanN_ = n;
}

PMP_INLINE real KeyVoice::tableLaw(real x) const {
    const real u = lawSign_ * x * invW_;
    if (!span_) return lawEval(u - x0_ * invW_);
    return splineEval(spanY_, spanM_, spanN_, spanU0_, spanDu_, u);
}

// the one-pole high pass of the strike (onsetFc_ glides down every 16 samples, see process()): no libm
PMP_INLINE real KeyVoice::onsetHp(real v) {
    if (onsetFc_ <= real(1e-3)) return v;
    const real x = real(6.283185307179586) * onsetFc_ / onsetFs_;
    const real a = x * (real(1) - real(0.5) * x);
    onsetLp_ += a * (v - onsetLp_);
    const real w = v - onsetLp_;
    onsetLp2_ += a * (w - onsetLp2_);
    return w - onsetLp2_;
}

void KeyVoice::process(float* out, int n, float* force) {
    if (!active_) return;
    int i = 0;
    while (i < n) {
        // housekeeping every 16 samples of this voice's own timeline, independent of the
        // host block size: damper ramp and exact resynchronisation of the tip position
        const int phase = int(frame_ & 15u);
        const int chunk = std::min(n - i, 16 - phase);
        if (phase == 0) {
            // asleep below the threshold: checked on the voice's own grid, so the moment does not depend on the host's
            // block size (a fast damper silences a reed within one host block)
            if (!hammerActive_ && knockLeft_ == 0 && frame_ != 0 && chokeLeft_ <= 0 && energySum() < sleepEnergy_) {
                sleep();
                return;
            }
            if (damperMoving_) advanceDamper(16);
            if (level_ != levelTarget_) {
                level_ += levelGlide_ * (levelTarget_ - level_);
                if (std::fabs(level_ - levelTarget_) < real(1e-6) * levelTarget_) level_ = levelTarget_;
            }
            syncPickup(tip());
            // the strike high pass glides out: its states fade with its corner, or the low pass inside it would hold
            // the level of the moment as an offset and drop it as a step when the filter switches off
            if (onsetFc_ > real(1e-3)) {
                onsetFc_ *= onsetDecay_;
                onsetLp_ *= onsetDecay_;
                onsetLp2_ *= onsetDecay_;
            } else {
                onsetLp_ = onsetLp2_ = 0;
            }
            if (glide_ != 0 && !hammerActive_) applyGlide();
        }
        const real g = gain_ * level_;
        int k = 0;
        while (k < chunk && hammerActive_) {
            if (knockLeft_ > 0) knockStep();
            const real dx = stepHammer();
            out[i + k] += float(onsetHp(g * pickupDelta(dx, linStep())));
            if (force) {
                const real f = mechExp_ == real(1) || hammerForce_ <= 0
                    ? hammerForce_ : hammerForce_ * powr(hammerForce_ * real(0.1), mechExp_ - real(1));
                force[i + k] += float(forceGain_ * level_ * f);
            }
            ++k;
        }
        for (; k < chunk && knockLeft_ > 0; ++k) {     // the release's blow in flight: sample by sample
            knockStep();
            const real dx = stepFree();
            out[i + k] += float(onsetHp(g * pickupDelta(dx, linStep())));
        }
        if (k < chunk) {
            real dx[16], dl[16], d[16];
            linRun(chunk - k, dl);
            stepFreeRun(chunk - k, dx);
            pickupRun(chunk - k, dx, dl, d);
            for (int j = 0; k < chunk; ++k, ++j)
                out[i + k] += float(onsetHp(g * d[j]));
        }
        frame_ += uint32_t(chunk);
        i += chunk;
    }
    if (chokeLeft_ > 0 && (chokeLeft_ -= n) <= 0) {   // stolen voice: 90 dB down, cut it
        reset();
        return;
    }
}

real KeyVoice::energySum() const {
    real e = 0;
    for (int m = 0; m < n_; ++m) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
    return real(0.5) * e;
}

void KeyVoice::sleep() {
    for (int m = 0; m < n_; ++m) zr_[m] = zi_[m] = 0;
    resetPickup(0);
    onsetFc_ = onsetLp_ = onsetLp2_ = 0;
    active_ = false;
}

} // namespace pmp
