// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// key_voice.h - one key of the instrument as a persistent physical system
//
//   hammer (Hunt-Crossley contact) -> tine/tonebar normal modes -> pickup -> voltage
//
// The same chain serves the Wurlitzer: a reed is a cantilever with a tuning weight at its tip
// (no tonebar), and its electrostatic pickup is a capacitor, whose capacitance 1 / (gap - x)
// takes the place of the magnetic flux (KeyConfig::pickupType).
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
    double sigmaFree[kMaxModes]   = {};  // decay rate without damper [1/s]
    double sigmaDamper[kMaxModes] = {};  // additional decay rate with damper engaged [1/s]
    double damperRampSec = 0.01;

    double hammerMass   = 0.004;         // [kg]
    double hammerK      = 1e8;           // contact stiffness [N/m^alpha]
    double hammerAlpha  = 2.0;
    double hammerLambda = 0.4;           // [s/m]
    int    hammerSubsteps = 2;
    double hammerSpeedHardening = 0.0;   // [%] extra contact hardness above 2 m/s

    double pickupWidth    = 1e-3;        // [m]
    double pickupOffset   = -0.4e-3;     // pole centre relative to tine rest position [m]
    double pickupExponent = 1.5;

    double gain = 1.0;                   // output scaling (auto-voicing)
    double sleepEnergy = 1e-16;          // [J]

    // 0: electromagnetic pickup (Rhodes), flux (1 + s^2)^-p; 1: electrostatic pickup (Wurlitzer),
    // one plate, capacitance 1 / (1 - s); 2: the reed in the slot of the comb between two plates,
    // 1 / (1 - s) + 1 / (1 + s) ~ 1 / (1 - s^2). In all s = (x - x0) / w.
    int pickupType = 0;
    // 4: the reed in the slot between the fingers, capacitance from a table (pickup_tables.h): pickupSection 0..3 (reeds 1-14,
    // 15-20, 21-42, 43-64), the output through a one-pole high pass at pickupHpfHz (0 = the plain difference of the law)
    int pickupSection = 0;
    double pickupHpfHz = 0.0;
    // the fundamental builds up over some ms at the strike (the recordings, bass keys): a one-pole high pass at pickupOnsetHz
    // glides down with the time constant pickupOnsetMs from the strike on (0 = off)
    double pickupOnsetHz = 0.0, pickupOnsetMs = 15.0;
    // the pitch drops by reedGlide cents per mm of swing of the fundamental mode at the tip, in mm of the pickup law
    // ((x - x0) / w): slip at the clamp, linear in the amplitude; every mode moves by the same ratio (0 = off)
    double reedGlide = 0.0;
};

struct PickupLaw;

class KeyVoice {
public:
    // Takes what the voice needs from cfg; it keeps no reference to it.
    void configure(const KeyConfig& cfg, double fs);
    void reset();                        // silence, damper engaged

    // hammerVelocity [m/s]; level scales the output of this strike (velocity -> level curve,
    // vel.gainDb); on a sounding voice it glides there within ~3 ms
    // contactScale multiplies the contact stiffness of this strike (strike variation; 1 = as configured)
    void strike(double hammerVelocity, double level = 1.0, double contactScale = 1.0);
    void setOnsetVel(double p) { onsetVel_ = real(p); }
    void setDamperScale(double scale);   // velocity-dependent felt effectiveness
    void setDamper(bool engaged);
    // The tangent lifts off and the whole string speaks: the pitch drops by
    // string.releaseSemitones (docs/CLAVINET.md 2.1). The free poles are rebuilt at the
    // scaled frequency - amplitude and phase stay, the voice keeps rotating, just slower.
    void setRetune(double semitones);
    // Half pedal: engage the damper only to a fraction of its full damping. The
    // fraction snaps to a 16-sample ramp grid (from the fixed-point engine, removed
    // 2026-10-20; kept, the presets are calibrated on it); the rail motion ramps there.
    void setDamperFraction(real f);
    void setPickupOffset(double x0);     // [m]
    void setGain(double g);
    void setMechScale(double s);         // coupling of this key into the mech modes (linear, 1 = as the gain)
    void setForceLevelled(bool on) { forceLevelled_ = on; }   // force output with the strike's level (vel.gainDb) or the raw force
    void setSleepEnergy(double e) { sleepEnergy_ = real(e); }

    void choke();                        // stolen: 60 dB in 20 ms, cut after 30 ms

    // Adds the pickup voltage of n samples (at the internal rate) to out and, if force is
    // given, the hammer contact force [N] times the key's output gain to force (it drives the
    // mechanical resonances of the engine).
    // clickForce (optional) receives the force of the hammer click: F (F / 10 N)^(clickExp - 1), the same gains
    void process(float* out, int n, float* force = nullptr, float* clickForce = nullptr);
    void setClickExponent(double e) { clickExp_ = real(e); }
    // Power amp saturation of this voice alone (amp.perVoice): tanh(drive x) / drive with unity small-signal
    // gain, before the voices are summed, so that chords do not saturate against each other. 0 = off.
    void setSaturation(double drive) { satDrive_ = real(drive); satInv_ = drive > 0 ? real(1.0 / drive) : real(1); }
    void setMechExponent(double e) { mechExp_ = real(e); }
    // The damper landing on the reed strikes it once: an impulse [N s] at the damper contact excites every mode with
    // its shape there; the felt (already engaged) damps the ring within tens of ms. 0 = off.
    void setDamperKnock(double impulse, double periods = 0.0) { knock_ = real(impulse); knockPeriods_ = real(periods); }
    void damperHit(real fraction);       // the key was released with the damper falling by `fraction` (0..1)
    void setClickScale(double s) { clickScale_ = real(s); }   // coupling of this key into the click (linear), apart from the mech modes

    bool   active() const { return active_; }
    bool   choking() const { return chokeLeft_ > 0; }
    bool   hammerActive() const { return hammerActive_; }
    double energy() const;               // modal energy [J]
    real   level() const;                // energy relative to the sleep threshold
    double tipDisplacement() const;      // [m]
    // P - 1 of mode m per sample or per hammer sub-step (tests; sub-step values are only
    // kept current while the hammer is active)
    std::complex<double> pole(int m, bool substep = false) const;

private:
    struct Shift { real dSig, dWd, sig, wd; };
    Shift shift(int m) const;            // damping of mode m relative to the free tine
    void updateCoefficients();           // after a change of damping
    void applyGlide();                   // poles at the pitch of the present swing (reedGlide), from the base poles
    void updateSubsteps();               // hammer sub-step coefficients
    void advanceDamper(int n);
    real stepFree();
    void stepFreeRun(int count, real* dx);
    void pickupRun(int count, const real* dx, real* d);   // the pickup over a run: d[j] = pickupDelta(dx[j]), vectorized over the samples  // count <= 16 free samples at once: the modes stay in registers over the run                     // one sample; returns the change of the tip position
    real stepHammer();
    real tip() const;                    // tip position from the modal state
    void resetPickup(real x);
    void syncPickup(real x);             // exact resynchronisation of the pickup state to the tip
    real pickupDelta(real dx);           // Phi(x + dx) - Phi(x), advances x
    real saturate(real v) const;         // the voice's amp (satDrive_ > 0), a rational tanh
    real lawEval(real u) const;          // table capacitance at penetration u [mm]

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
    alignas(16) real vRe_[kModeSlots] = {}, vIm_[kModeSlots] = {};    // strike velocity weights
    real sigma_[kMaxModes] = {}, omegaD_[kMaxModes] = {};  // current decay rate, frequency
    real bqr_[kMaxModes] = {}, bqi_[kMaxModes] = {};    // P - 1 per sample without the glide (reedGlide)
    real glide_ = 0;                                    // relative frequency drop per mm of swing (0 = off)
    bool substepStale_ = false;                         // sqr_ .. sbi_ need an update

    // the free tine (damper lifted), computed in double by configure()
    real retuneSemi_ = real(0);                         // active retune [semitones], 0 = speaking length
    real fqr_[kMaxModes] = {}, fqi_[kMaxModes] = {};    // P - 1 per sample
    real fsqr_[kMaxModes] = {}, fsqi_[kMaxModes] = {};  // P - 1 per hammer sub-step
    real sigF_[kMaxModes] = {}, wdF_[kMaxModes] = {};   // decay rate, damped frequency
    real sd_[kMaxModes] = {};                           // sigmaDamper as real
    real phiTip_[kMaxModes] = {};                       // mode shape at the tip
    real phiDamp_[kMaxModes] = {};                      // mode shape at the damper contact
    real knock_ = 0;                                    // damper impulse [N s]
    real knockScale_ = 0;                               // impulse of the blow in flight
    real knockPeriods_ = 0;                             // duration of the blow in periods of the fundamental (0 = an impulse)
    int  knockLeft_ = 0, knockN_ = 0;                   // samples left of the blow, its length
    void advanceKnock();                                // adds the next 16 samples of the blow to the modes
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
    real hdt_ = 0;                                      // sub-step [s]
    real hammerForce_ = 0;                              // mean contact force of the last sample [N]
    real keyGain_ = 0;                                  // output gain of the key (auto-voicing)
    real mechScale_ = 1;                                // per-key coupling into the mech modes
    real forceGain_ = 0;                                // keyGain_ * mechScale_: gain of the force output
    bool forceLevelled_ = true;                         // the force output carries level_ (vel.gainDb)
    real clickExp_ = 1;                                 // exponent of the click's force law
    real satDrive_ = 0, satInv_ = 1;                    // per-voice saturation: linear drive, 1 / drive
    real mechExp_ = 1;                                  // exponent of the force law of the mech modes
    real clickScale_ = 1;                               // per-key coupling into the click
    real level_ = 1, levelTarget_ = 1, levelGlide_ = 0; // level of the strike (vel.gainDb)
    int  hammerAge_ = 0, noContact_ = 0;
    int  noContactLimit_ = 0, hammerAgeLimit_ = 0;      // backcheck after 3 ms, timeout 250 ms
    real sleepEnergy_ = 0;                              // sleep threshold [J]

    // pickup: s = (x - x0) / w, t = 1 + s^2, flux ~ t^-p; electrostatic: t = 1 - s, flux = 1 / t
    bool fastPickup_ = true;                            // p == 1.5
    bool unitPickup_ = false;                           // p == 1 (the clavinet): closed form, no libm
    bool electrostatic_ = false;                        // pickupType 1 or 2
    bool slot_ = false;                                 // pickupType 2 and 3: two plates, t = 1 - s^2
    bool diff_ = false;                                 // pickupType 3: the plates subtracted, odd in s
    bool table_ = false;                                // pickupType 4: capacitance from a table
    const PickupLaw* law_ = nullptr;                    // its section
    real lawSign_ = 1, lawPrev_ = 0, hpfW_ = 0, hpfA_ = 0, hpfB_ = 1;   // reed side, last law value, high pass state and coefficients
    bool hpfOn_ = false;
    real onsetHz0_ = 0, onsetFc_ = 0, onsetDecay_ = 1, onsetLp_ = 0, onsetLp2_ = 0, onsetFs_ = 44100;   // strike high pass (two poles): start, now, decay per 16 samples, states
    real onsetVel_ = 0;                                 // velocity coupling of the onset corner
    real onsetHp(real v);                // that high pass, applied to the pickup signal
    real x0_ = 0, invW_ = 1000;
    real xPrev_ = 0, sPrev_ = 0, tPrev_ = 1, rtPrev_ = 1;
    double pExp_ = 1.5, phiPrev_ = 1.0;                 // generic exponent (slow path)
    real gain_ = 0;                                     // -gain * fs  (u = -N dPhi/dt)

    bool active_ = false;
};

} // namespace pmp
