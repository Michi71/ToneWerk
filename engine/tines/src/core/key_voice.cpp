// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// key_voice.cpp - modal tine/tonebar, hammer contact, pickup, damper
#include "key_voice.h"

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
        transient_[m] = cfg.transient[m];
        vRe_[m]    = real(cfg.modes.phiStrike[m]);
        strikeExp_[m] = real(cfg.modeStrikeExp[m]);
        strikeGain_[m] = 1;
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
    hkCore_ = hkCoreStrike_ = real(cfg.hammerCoreK);
    dCore_ = real(cfg.hammerCoreDepth);
    kCore_ = real(cfg.hammerCoreKnee);
    inv4kCore_ = cfg.hammerCoreKnee > 0.0 ? real(0.25 / cfg.hammerCoreKnee) : real(0);
    hac_ = real(cfg.hammerCoreAlpha);
    hInvM_ = real(1.0 / cfg.hammerMass);
    hdt_ = real(hK);
    noContactLimit_ = int(0.003 * fs_);
    hammerAgeLimit_ = int(0.25 * fs_);
    sleepEnergy_ = real(cfg.sleepEnergy);
    pExp_ = cfg.pickupExponent;
    fastPickup_ = pExp_ == 1.5;
    hystQ_ = real(cfg.pickupHystQ);
    hystBeta_ = real(cfg.pickupHystBeta);
    hystGamma_ = real(cfg.pickupHystGamma);
    hystZ_ = hystZPrev_ = 0;
    unitPickup_ = pExp_ == 1.0;
    invW_ = real(1.0 / cfg.pickupWidth);
    x0_ = real(cfg.pickupOffset);
    // lateral motion (tonebar.lateralByKey): the pole sits y0 = c w off the swing plane, the law widens to
    // w' = w sqrt(1 - c^2) so that in the plane Phi = (1 - c^2)^1.5 (1 + ((x - x0) / w)^2)^-1.5 - the calibrated law;
    // the constant goes into the gain. Only the Rhodes law with p = 1.5 (fast path) has the second coordinate.
    nLat_ = 0;
    for (int m = 0; m < n_ && nLat_ < 2; ++m)
        if (cfg.phiTipY[m] != 0.0) { latIdx_[nLat_] = m; latPhi_[nLat_] = real(cfg.phiTipY[m]); ++nLat_; }
    lateral_ = nLat_ > 0 && fastPickup_;
    latComp_ = 1;
    y0_ = 0;
    if (lateral_) {
        const double c = std::min(std::max(cfg.pickupLateralOffset, 0.0), 0.95);
        invW_ = real(1.0 / (cfg.pickupWidth * std::sqrt(1.0 - c * c)));
        y0_ = real(c * cfg.pickupWidth);
        latComp_ = real(1.0 / ((1.0 - c * c) * std::sqrt(1.0 - c * c)));
    }
    setGain(cfg.gain);
    levelGlide_ = real(1.0 - std::exp(-16.0 / (0.003 * fs_)));   // per 16 samples, ~3 ms
    reset();
}

void KeyVoice::setGain(double g) {
    gain_ = real(-g * fs_) * latComp_;
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
    updateCoefficients();
    resetPickup(0);
    active_ = false;
}

// Damping of mode m for the current damper position and choke, as offsets from the free
// tine: dSig in the decay rate, dWd in the damped frequency (wd^2 = w0^2 - sig^2 rewritten
// as wd^2 = wdF^2 - dSig (sig + sigF), so neither offset suffers from cancellation).
PMP_INLINE KeyVoice::Shift KeyVoice::shift(int m) const {
    Shift s;
    const real sf = sigF_[m], wf = wdF_[m];
    s.dSig = damper_ * damperScale_ * sd_[m] + choke_;
    s.sig  = sf + s.dSig;
    if (s.dSig == 0) {                                      // damper lifted: the free tine
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
        const real iw = real(1) / s.wd;
        cTip_[m]    = phiTip_[m] * iw;
        cStrike_[m] = vRe_[m] * iw;
        vIm_[m]     = vRe_[m] * s.sig * iw;
    }
    for (int i = 0; i < nLat_; ++i) latC_[i] = latPhi_[i] / omegaD_[latIdx_[i]];
    // the sub-step coefficients are only needed while the hammer is in play
    if (hammerActive_) updateSubsteps();
    else substepStale_ = true;
}

void KeyVoice::updateSubsteps() {
    for (int m = 0; m < n_; ++m) {
        const Shift s = shift(m);
        real pr = fsqr_[m], pi = fsqi_[m];
        if (s.dSig != 0) shiftPole(fsqr_[m], fsqi_[m], -s.dSig * hK_, s.dWd * hK_, pr, pi);
        sqr_[m] = pr;
        sqi_[m] = pi;
        // ZOH input gain (P - 1) / (-sigma + j omega_d) * phi(strike)
        const real g = vRe_[m] * strikeGain_[m] / (s.sig * s.sig + s.wd * s.wd);
        sbr_[m] = (s.wd * pi - s.sig * pr) * g;
        sbi_[m] = -(s.sig * pi + s.wd * pr) * g;
    }
    substepStale_ = false;
}

std::complex<double> KeyVoice::pole(int m, bool substep) const {
    return substep ? std::complex<double>(sqr_[m], sqi_[m])
                   : std::complex<double>(qr_[m], qi_[m]);
}

void KeyVoice::strike(double hammerVelocity, double level, double contactScale) {
    levelTarget_ = real(level);
    if (!active_) level_ = levelTarget_;
    if (chokeLeft_ > 0) {                // struck again while being stolen: keep it
        choke_ = 0;
        chokeLeft_ = 0;
        updateCoefficients();
    }
    if (!active_) resetPickup(tip());
    active_ = true;
    hammerActive_ = true;
    contact_ = false;
    hammerAge_ = noContact_ = 0;
    impulse_ = 0;
    // the hammer arrives at the (possibly vibrating) tine with the given speed
    real ws = 0;
    for (int m = 0; m < n_; ++m) ws += cStrike_[m] * zi_[m];
    xh_ = ws;
    vh_ = std::max(real(hammerVelocity), real(1e-4));
    const real vn = std::min(std::max(vh_ / real(2.5), real(0.05)), real(4));   // 2.5 m/s ~ the reference strike
    for (int m = 0; m < n_; ++m)
        strikeGain_[m] = strikeExp_[m] != real(0) ? powr(vn, strikeExp_[m]) : real(1);
    substepStale_ = true;
    updateSubsteps();
    hkStrike_ = hk_ * real(contactScale);
    hkCoreStrike_ = hkCore_ * real(contactScale);        // the core is hard already: no speed hardening
    if (speedHardening_ != 0 && vh_ > real(2)) {
        const real t = std::min(vh_ - real(2), real(1));
        const real hardness = std::min(std::max(speedHardening_ * t * t, real(-200)), real(200));
        hkStrike_ = hk_ * powr(real(2), (ha_ + real(1)) * hardness / real(100));
    }
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
// damper: the tine keeps its position and speed, so the output stays continuous (no click).
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

real KeyVoice::latY() const {
    real y = 0;
    for (int i = 0; i < nLat_; ++i) y += latC_[i] * zi_[latIdx_[i]];
    return y;
}

void KeyVoice::syncPickup(real x) {
    const real s = (x - x0_) * invW_;
    xPrev_ = x;
    sPrev_ = s;
    tPrev_ = real(1) + s * s;
    if (lateral_) {
        uPrev_ = (latY() - y0_) * invW_;
        tPrev_ += uPrev_ * uPrev_;
    }
    rtPrev_ = std::sqrt(tPrev_);
}

void KeyVoice::resetPickup(real x) {
    syncPickup(x);
    hystZ_ = hystZPrev_ = 0;
    if (!fastPickup_ && !unitPickup_) phiPrev_ = std::exp(-pExp_ * std::log(double(tPrev_)));
}

PMP_INLINE real KeyVoice::pickupDelta(real dx) {
    const real x = xPrev_ + dx;
    const real s = (x - x0_) * invW_;
    real t = real(1) + s * s;
    real d;
    if (fastPickup_) {
        // Phi = t^-1.5:  Phi(t) - Phi(t0) = (t0 - t)(t0 + sqrt(t0 t) + t) /
        //                                   ((sqrt(t0) + sqrt(t)) t0^1.5 t^1.5)
        // with t0 - t = -(dx / w)(s0 + s) instead of a difference of nearly equal values
        real dt = -(dx * invW_) * (sPrev_ + s);
        if (lateral_) {                        // the second coordinate: t = 1 + s^2 + u^2
            const real u = (latY() - y0_) * invW_;
            t += u * u;
            dt -= (u - uPrev_) * (uPrev_ + u);
            uPrev_ = u;
        }
        const real rt = std::sqrt(t);
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
    if (hystQ_ != real(0)) {
        // Bouc-Wen on the normalised tip displacement: a rate-independent magnetic memory.
        const real ds = dx * invW_;
        hystZ_ += ds - hystBeta_ * std::abs(ds) * hystZ_ - hystGamma_ * ds * std::abs(hystZ_);
        d += hystQ_ * (hystZ_ - hystZPrev_);
        hystZPrev_ = hystZ_;
    }
    xPrev_ = x;
    sPrev_ = s;
    tPrev_ = t;
    return d;
}

// The pickup over a run of up to 16 samples whose tip displacements are known (stepFreeRun): the samples are independent
// once the positions are summed up, so square root and division of the Rhodes pickup run over arrays of 16 and vectorize -
// four per instruction instead of a dependent chain per sample (docs/PROFILING.md section 3.2). Same formulas as pickupDelta.
void KeyVoice::pickupRun(int count, const real* dx, real* d) {
    if (!fastPickup_ || hystQ_ != real(0)) {              // the other exponents / the hysteresis: sample by sample
        for (int j = 0; j < count; ++j) d[j] = pickupDelta(dx[j]);
        return;
    }
    real s[17], t[17], rt[17];
    s[0] = sPrev_; t[0] = tPrev_; rt[0] = rtPrev_;
    real x = xPrev_;
    for (int j = 0; j < count; ++j) { x += dx[j]; s[j + 1] = (x - x0_) * invW_; }
    for (int j = 1; j <= count; ++j) { t[j] = real(1) + s[j] * s[j]; rt[j] = std::sqrt(t[j]); }
    for (int j = 1; j <= count; ++j) {
        const real dt = -(dx[j - 1] * invW_) * (s[j - 1] + s[j]);
        d[j - 1] = dt * (t[j - 1] + rt[j - 1] * rt[j] + t[j]) / ((rt[j - 1] + rt[j]) * (t[j - 1] * rt[j - 1]) * (t[j] * rt[j]));
    }
    xPrev_ = x; sPrev_ = s[count]; tPrev_ = t[count]; rtPrev_ = rt[count];
}

double KeyVoice::energy() const {
    real e = 0;
    for (int m = 0; m < n_; ++m)
        if (!transient_[m]) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
    return 0.5 * double(e);
}

// comparable across keys: auto-voicing sets the sleep threshold 120 dB below the energy of
// a velocity-100 strike, and the output gains make those strikes equally loud. The strike's
// level (vel.gainDb) scales the output, not the modes: with a flat vel.gamma it carries the
// dynamics, so the audible level includes it
real KeyVoice::level() const {
    real e = 0;
    for (int m = 0; m < n_; ++m)
        if (!transient_[m]) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
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
        const real delta = xh_ - ws;                 // felt/neoprene compression
        real F = 0;
        if (delta > 0) {
            F = hkStrike_ * powr(delta, ha_);
            // the tip bottoms out on its hard core: a second, Hertz-like contact on top of the
            // neoprene's (docs/ATTACK_WINDOWS.md 14) - the short, hard part of a loud blow
            // a knee of +-k around the depth (hammer.coreKnee): (e + k)^2 / 4k, C1 into e at e = k; k = 0 is the step
            if (dCore_ > 0) {
                const real e = delta - dCore_;
                if (e > kCore_) F += hkCoreStrike_ * powr(e, hac_);
                else if (e > -kCore_) { const real q = e + kCore_; F += hkCoreStrike_ * powr(q * q * inv4kCore_, hac_); }
            }
            F *= real(1) + hl_ * (vh_ - vs);
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
        vh_ -= F * hInvM_ * dt;                      // symplectic Euler for the hammer
        xh_ += vh_ * dt;
        fSum += F;
    }
    hammerForce_ = fSum / real(K);
    impulse_ += hammerForce_ * hT_;
    if (touched) { contact_ = true; noContact_ = 0; }
    else if (contact_) ++noContact_;
    ++hammerAge_;
    // the hammer is caught (backcheck) a few ms after it left the tine
    if ((contact_ && noContact_ > noContactLimit_) || hammerAge_ > hammerAgeLimit_)
        hammerActive_ = false;
    return dx;
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
            if (damperMoving_) advanceDamper(16);
            if (level_ != levelTarget_) {
                level_ += levelGlide_ * (levelTarget_ - level_);
                if (std::fabs(level_ - levelTarget_) < real(1e-6) * levelTarget_) level_ = levelTarget_;
            }
            syncPickup(tip());
        }
        const real g = gain_ * level_;
        int k = 0;
        while (k < chunk && hammerActive_) {
            const real dx = stepHammer();
            out[i + k] += float(g * pickupDelta(dx));
            if (force) {
                const real fg = forceGain_ * level_;
                const real f = mechExp_ == real(1) || hammerForce_ <= 0
                    ? hammerForce_ : hammerForce_ * powr(hammerForce_ * real(0.1), mechExp_ - real(1));
                force[i + k] += float(fg * f);
            }
            ++k;
        }
        if (k < chunk && lateral_) {          // the second coordinate needs the lateral position of every sample
            for (; k < chunk; ++k)
                out[i + k] += float(g * pickupDelta(stepFree()));
        }
        if (k < chunk) {
            real dx[16], d[16];
            stepFreeRun(chunk - k, dx);
            pickupRun(chunk - k, dx, d);
            for (int j = 0; k < chunk; ++k, ++j)
                out[i + k] += float(g * d[j]);
        }
        frame_ += uint32_t(chunk);
        i += chunk;
    }
    if (chokeLeft_ > 0 && (chokeLeft_ -= n) <= 0) {   // stolen voice: 90 dB down, cut it
        reset();
        return;
    }
    real e = 0;
    for (int m = 0; m < n_; ++m) e += zr_[m] * zr_[m] + zi_[m] * zi_[m];
    if (!hammerActive_ && real(0.5) * e < sleepEnergy_) {
        for (int m = 0; m < n_; ++m) zr_[m] = zi_[m] = 0;
        resetPickup(0);
        active_ = false;
    }
}

} // namespace pmp
