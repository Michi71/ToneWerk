// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// key_voice.h - one key of the instrument as a persistent physical system
//
//   hammer (Hunt-Crossley contact) -> tine/tonebar normal modes -> pickup -> voltage
//
// The normal modes come from the tine designer and are integrated exactly
// (zero-order-hold discretisation of each damped oscillator). The hammer interacts with
// the modes through the mode shapes at the strike point; the pickup sees the tine tip.
// A new strike hits the tine in whatever state it is (no retrigger). Under a polyphony
// limit the engine may choke a voice: it decays within a few ms and then sleeps.
//
// The real-time path uses the type `real` (config.h): double on desktop hosts, float on
// microcontrollers. The formulations below are chosen to be accurate in float:
// - modes advance in delta form z += (P - 1) z, where P - 1 keeps its relative precision
//   even for poles within 1e-6 of the unit circle (long-ringing bass modes);
// - the pickup voltage uses the flux difference Phi(x1) - Phi(x0) rearranged so that no
//   two nearly equal numbers are subtracted;
// - damper and choke change the poles relative to those of the free tine, which are
//   computed once in double: P = P_free e^(dz h) with a short series for e^(dz h) - 1.
//   The damper ramp therefore needs no libm call and no double arithmetic.
#pragma once

#include <complex>
#include <cstdint>

#include "config.h"
#include "tine_designer.h"

namespace pmp {

struct KeyConfig {
    TineModes modes;
    double modeStrikeExp[kMaxModes] = {}; // extra velocity exponent of a mode's hammer input:
                                          // gain = (v/100)^exp (0: linear; the upper tonebar)
    double sigmaFree[kMaxModes]   = {};  // decay rate without damper [1/s]
    double sigmaDamper[kMaxModes] = {};  // additional decay rate with damper engaged [1/s]
    bool   transient[kMaxModes]   = {};  // a tonebar mode that dies within the attack: left out of the
                                         // energy that decides sleeping and voice stealing
    double damperRampSec = 0.01;
    // the part of a mode's tip motion across the swing plane (tonebar.lateralByKey): the pickup sees it as a second
    // coordinate, Phi = (1 + s^2 + u^2)^-1.5, u = (y - y0) / w' (docs/ATTACK_WINDOWS.md 10.7); all zero: the plane only
    double phiTipY[kMaxModes]     = {};
    double pickupLateralOffset    = 0.5; // y0 / w

    double hammerMass   = 0.004;         // [kg]
    double hammerK      = 1e8;           // contact stiffness [N/m^alpha]
    double hammerAlpha  = 2.0;
    double hammerLambda = 0.4;           // [s/m]
    int    hammerSubsteps = 2;
    double hammerSpeedHardening = 0.0;   // [%] extra contact hardness above 2 m/s
    double hammerCoreDepth = 0.0;        // [m] compression at which the hard core engages (0: no core)
    double hammerCoreK     = 0.0;        // core stiffness [N/m^coreAlpha]
    double hammerCoreAlpha = 1.5;
    double hammerCoreKnee  = 0.0;        // half width of the core's knee [m] (0: the step)

    double pickupWidth    = 1e-3;        // [m]
    double pickupOffset   = -0.4e-3;     // pole centre relative to tine rest position [m]
    double pickupExponent = 1.5;
    double pickupHystQ = 0.0;          // flux added by the hysteretic magnetization (0: off)
    double pickupHystBeta = 0.5;       // Bouc-Wen loop shape
    double pickupHystGamma = 0.5;

    double gain = 1.0;                   // output scaling (auto-voicing)
    double sleepEnergy = 1e-16;          // [J]
};

class KeyVoice {
public:
    // Takes what the voice needs from cfg; it keeps no reference to it.
    void configure(const KeyConfig& cfg, double fs);
    void reset();                        // silence, damper engaged

    // hammerVelocity [m/s]; level scales the output of this strike (velocity -> level curve,
    // vel.gainDb); on a sounding voice it glides there within ~3 ms
    // contactScale multiplies the contact stiffness of this strike (strike variation; 1 = as configured)
    void strike(double hammerVelocity, double level = 1.0, double contactScale = 1.0);
    void setDamperScale(double scale);   // velocity-dependent felt effectiveness
    void setDamper(bool engaged);
    // Half pedal: engage the damper only to a fraction of its full damping. The
    // fraction snaps to a 16-sample ramp grid (from the fixed-point engine, removed
    // 2026-10-20; kept, the presets are calibrated on it); the rail motion ramps there.
    void setDamperFraction(real f);
    void setPickupOffset(double x0);     // [m]
    void setGain(double g);
    void setMechScale(double s);         // coupling of this key into the mech modes (linear, 1 = as the gain)
    void setSleepEnergy(double e) { sleepEnergy_ = real(e); }

    void choke();                        // stolen: 60 dB in 20 ms, cut after 30 ms

    // Adds the pickup voltage of n samples (at the internal rate) to out and, if force is
    // given, the hammer contact force [N] times the key's output gain and the strike's level to
    // force (it drives the mechanical resonances of the engine).
    void process(float* out, int n, float* force = nullptr);
    void setMechExponent(double e) { mechExp_ = real(e); }

    bool   active() const { return active_; }
    bool   choking() const { return chokeLeft_ > 0; }
    bool   hammerActive() const { return hammerActive_; }
    double energy() const;               // modal energy [J]
    real   level() const;                // energy relative to the sleep threshold
    double tipDisplacement() const;      // [m]
    // Integral of the hammer contact force [N s] since the strike (the analysis tools read the mean
    // force of a host frame as its difference; excitation_fit.py)
    double hammerImpulse() const { return double(impulse_); }
    // P - 1 of mode m per sample or per hammer sub-step (tests; sub-step values are only
    // kept current while the hammer is active)
    std::complex<double> pole(int m, bool substep = false) const;

private:
    struct Shift { real dSig, dWd, sig, wd; };
    Shift shift(int m) const;            // damping of mode m relative to the free tine
    void updateCoefficients();           // after a change of damping
    void updateSubsteps();               // hammer sub-step coefficients
    void advanceDamper(int n);
    real stepFree();
    void stepFreeRun(int count, real* dx);
    void pickupRun(int count, const real* dx, real* d);   // the pickup over a run: d[j] = pickupDelta(dx[j]), vectorized over the samples  // count <= 16 free samples at once: the modes stay in registers over the run                     // one sample; returns the change of the tip position
    real stepHammer();
    real tip() const;                    // tip position from the modal state
    real latY() const;                   // lateral tip position (tonebar.lateralByKey)
    void resetPickup(real x);
    void syncPickup(real x);             // exact resynchronisation of the pickup state to the tip
    real pickupDelta(real dx);           // Phi(x + dx) - Phi(x), advances x

    double fs_ = 88200.0, T_ = 1.0 / 88200.0;
    int    n_  = 0;                                     // modes
    int    n4_ = 0;                                     // ... rounded up to blocks of four (the vector loops; the padding is zero)
    int    K_  = 1;                                     // hammer sub-steps per sample

    // modal state z = (dq/dt + sigma q) + j omega_d q per mode (structure of arrays). The arrays of the per-sample loops
    // (stepFree, stepHammer, tip) run in blocks of four modes, one compiler vector each (docs/PROFILING.md section 3.1).
    static constexpr int kModeSlots = (kMaxModes + 3) & ~3;
    alignas(16) real zr_[kModeSlots] = {}, zi_[kModeSlots] = {};
    alignas(16) real qr_[kModeSlots] = {}, qi_[kModeSlots] = {};      // P - 1 per sample
    alignas(16) real sqr_[kModeSlots] = {}, sqi_[kModeSlots] = {};    // P - 1 per hammer sub-step
    alignas(16) real sbr_[kModeSlots] = {}, sbi_[kModeSlots] = {};    // sub-step input gain * phi(strike)
    alignas(16) real cTip_[kModeSlots] = {};                          // phi(tip) / omega_d
    alignas(16) real cStrike_[kModeSlots] = {};                       // phi(strike) / omega_d
    real strikeGain_[kMaxModes] = {};                     // (v/100)^modeStrikeExp of this strike
    real strikeExp_[kMaxModes] = {};                      // modeStrikeExp from the key config
    alignas(16) real vRe_[kModeSlots] = {}, vIm_[kModeSlots] = {};    // strike velocity weights
    real sigma_[kMaxModes] = {}, omegaD_[kMaxModes] = {};  // current decay rate, frequency
    bool substepStale_ = false;                         // sqr_ .. sbi_ need an update

    // the free tine (damper lifted), computed in double by configure()
    real fqr_[kMaxModes] = {}, fqi_[kMaxModes] = {};    // P - 1 per sample
    real fsqr_[kMaxModes] = {}, fsqi_[kMaxModes] = {};  // P - 1 per hammer sub-step
    real sigF_[kMaxModes] = {}, wdF_[kMaxModes] = {};   // decay rate, damped frequency
    real sd_[kMaxModes] = {};                           // sigmaDamper as real
    real phiTip_[kMaxModes] = {};                       // mode shape at the tip
    bool transient_[kMaxModes] = {};                    // mode left out of energy() / level()
    real hT_ = 0, hK_ = 0;                              // sample, hammer sub-step [s]

    // damper, and the fast decay of a stolen voice
    real damper_ = 1, damperTarget_ = 1, damperScale_ = 1;
    real damperRate_ = 0;                               // damper travel per sample
    bool damperMoving_ = false;
    real choke_ = 0, chokeSigma_ = 0;                   // extra decay rate [1/s]
    int  chokeLeft_ = 0, chokeSamples_ = 1;             // samples until the voice is cut
    uint32_t frame_ = 0;                                // sample counter of this voice

    // hammer
    bool hammerActive_ = false, contact_ = false;
    real xh_ = 0, vh_ = 0;                              // position [m], speed [m/s]
    real hk_ = 0, hkStrike_ = 0, speedHardening_ = 0;
    real ha_ = 2, hl_ = 0, hInvM_ = 0;                 // stiffness, exponent, hysteresis, 1/mass
    real hkCore_ = 0, hkCoreStrike_ = 0;                // the hard core under the tip: stiffness,
    real dCore_ = 0, hac_ = real(1.5);                  // engagement depth [m] (0: none), exponent
    real kCore_ = 0, inv4kCore_ = 0;                    // half width of its knee [m] (0: the step), 1 / 4k
    real hdt_ = 0;                                      // sub-step [s]
    real hammerForce_ = 0;                              // mean contact force of the last sample [N]
    real impulse_ = 0;                                  // integral of the contact force since the strike [N s]
    real keyGain_ = 0;                                  // output gain of the key (auto-voicing)
    real mechScale_ = 1;                                // per-key coupling into the mech modes
    real forceGain_ = 0;                                // keyGain_ * mechScale_: gain of the force output
    real mechExp_ = 1;                                  // exponent of the force law of the mech modes
    real level_ = 1, levelTarget_ = 1, levelGlide_ = 0; // level of the strike (vel.gainDb)
    int  hammerAge_ = 0, noContact_ = 0;
    int  noContactLimit_ = 0, hammerAgeLimit_ = 0;      // backcheck after 3 ms, timeout 250 ms
    real sleepEnergy_ = 0;                              // sleep threshold [J]

    // pickup: s = (x - x0) / w, t = 1 + s^2, flux ~ t^-p
    bool fastPickup_ = true;                            // p == 1.5
    bool unitPickup_ = false;                           // p == 1: closed form, no libm
    real x0_ = 0, invW_ = 1000;
    real xPrev_ = 0, sPrev_ = 0, tPrev_ = 1, rtPrev_ = 1;
    real hystQ_ = 0, hystBeta_ = 0.5, hystGamma_ = 0.5;   // Bouc-Wen hysteresis of the flux
    real hystZ_ = 0, hystZPrev_ = 0;
    double pExp_ = 1.5, phiPrev_ = 1.0;                 // generic exponent (slow path)
    real gain_ = 0;                                     // -gain * fs  (u = -N dPhi/dt)
    // lateral motion (tonebar.lateralByKey): up to two modes carry it; the pickup law gets the second coordinate
    bool lateral_ = false;
    int  nLat_ = 0, latIdx_[2] = {};
    real latPhi_[2] = {}, latC_[2] = {};                // shape across the plane, / omega_d
    real y0_ = 0, uPrev_ = 0, latComp_ = 1;             // pole offset [m], last u, gain compensation of the wider law

    bool active_ = false;
};

} // namespace pmp
