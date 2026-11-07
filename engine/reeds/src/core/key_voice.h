// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// key_voice.h - one key of the instrument as a persistent physical system
//
//   excitation (hammer or pad) -> reed normal modes -> pickup -> voltage
//
// Two excitations: the Wurlitzer's felt hammer (Hunt-Crossley contact) strikes the reed; the
// Pianet's pad (sticky foam under the key, excitation = pluck) pulls the reed up as the key goes
// down, until the adhesion lets go and the reed swings free. Two pickups: the Wurlitzer reed
// swings in the slot of an electrostatic comb, whose capacitance takes the place of a magnetic
// flux (KeyConfig::pickupType 1-4); the Pianet T reed swings over a magnetic pickup (type 0).
//
// The normal modes come from the reed designer and are integrated exactly (zero-order-hold
// discretisation of each damped oscillator). The excitation acts on the modes through their
// shapes at the strike point; the pickup sees the reed at its read point. A new strike hits the
// reed in whatever state it is (no retrigger). Under a polyphony limit the engine may choke a
// voice: it decays within a few ms and then sleeps.
//
// The real-time path uses the type `real` (config.h): double on desktop hosts, float on
// microcontrollers. The formulations below are chosen to be accurate in float:
// - modes advance in delta form z += (P - 1) z, where P - 1 keeps its relative precision
//   even for poles within 1e-6 of the unit circle (long-ringing bass modes);
// - the pickup voltage uses the flux difference Phi(x1) - Phi(x0) rearranged so that no
//   two nearly equal numbers are subtracted;
// - damper and choke change the poles relative to those of the free reed, which are
//   computed once in double: P = P_free e^(dz h) with a short series for e^(dz h) - 1.
//   The damper ramp therefore needs no libm call and no double arithmetic.
#pragma once

#include <complex>
#include <cstdint>

#include "config.h"
#include "reed_designer.h"

namespace pmp {

struct KeyConfig {
    ReedModes modes;
    double sigmaFree[kMaxModes]   = {};  // decay rate without damper [1/s]
    double sigmaDamper[kMaxModes] = {};  // additional decay rate with damper engaged [1/s]
    double damperRampSec = 0.01;

    // 0: the hammer (Wurlitzer), 1: the pad plucks the reed (Pianet)
    int    excitation   = 0;
    double hammerMass   = 0.004;         // [kg]
    double hammerK      = 1e8;           // contact stiffness [N/m^alpha]
    double hammerAlpha  = 2.0;
    double hammerLambda = 0.4;           // [s/m]
    int    hammerSubsteps = 2;
    double hammerSpeedHardening = 0.0;   // [%] extra contact hardness above 2 m/s
    // the pad: the key lifts it at its speed, the foam (stiffness pluckK [N/m]) drags the reed along at the pad point
    // until its force reaches the adhesion pluckAdhesion [N] at 1 m/s times (speed / 1 m/s)^pluckVelExp; then the bond
    // tears within pluckReleaseSec and the reed swings free
    double pluckK = 100.0, pluckAdhesion = 0.1, pluckVelExp = 0.3, pluckReleaseSec = 0.3e-3;

    // 0: electromagnetic pickup (Pianet T), flux (1 + s^2)^-p; electrostatic pickup (Wurlitzer): 1 one plate,
    // capacitance 1 / (1 - s); 2: the reed in the slot of the comb between two plates, 1 / (1 - s) + 1 / (1 + s) ~
    // 1 / (1 - s^2); 3: the plates subtracted, s / (1 - s^2). In all s = (x - x0) / w.
    int    pickupType     = 0;
    double pickupWidth    = 1e-3;        // [m]
    double pickupOffset   = -0.4e-3;     // pole centre relative to the reed's rest position [m]
    double pickupExponent = 1.5;
    double pickupWarp     = 0.0;         // [1/mm] quadratic warp of the table's penetration axis (0: the 2D solve as is)
    double pickupPole = 0.0;             // magnetic (exponent 1.5): half-width of the pole face in pickup widths (0: a point)
    double pickupSpan = 0.0;             // table law: the fundamental's amplitude drops by this share across the comb (0: one cross-section)
    // 4: the reed in the slot between the fingers, capacitance from a table (pickup_tables.h): pickupSection 0..3 (reeds 1-14,
    // 15-20, 21-42, 43-64), the output through a one-pole high pass at pickupHpfHz (0 = the plain difference of the law)
    int    pickupSection = 0;
    double pickupHpfHz = 0.0;
    // the fundamental builds up over some ms at the strike (the recordings, bass keys): a high pass at pickupOnsetHz
    // glides down with the time constant pickupOnsetMs from the strike on (0 = off)
    double pickupOnsetHz = 0.0, pickupOnsetMs = 15.0;
    // the pitch drops by reedGlide cents per mm of swing of the fundamental mode at the tip, in mm of the pickup law
    // ((x - x0) / w): slip at the clamp, linear in the amplitude; every mode moves by the same ratio (0 = off)
    double reedGlide = 0.0;
    int    fastMode = -1;                // the fundamental's fast-decaying copy (decay.fastShareByKey), -1: none
    double fastLinear = 0.0;             // share of that copy read at the pickup's slope at rest instead of through its law

    double gain = 1.0;                   // output scaling (auto-voicing)
    double sleepEnergy = 1e-16;          // [J]
};

struct PickupLaw;

class KeyVoice {
public:
    // Takes what the voice needs from cfg; it keeps no reference to it.
    void configure(const KeyConfig& cfg, double fs);
    void reset();                        // silence, damper engaged

    // speed [m/s]: the hammer's, or the key's that lifts the pad; level scales the output of this strike (velocity ->
    // level curve, vel.gainDb); on a sounding voice it glides there within ~3 ms.
    // contactScale multiplies the contact stiffness (hammer) or the adhesion (pad) of this strike (strike variation;
    // 1 = as configured)
    void strike(double speed, double level = 1.0, double contactScale = 1.0);
    void setDamperScale(double scale);   // velocity-dependent felt effectiveness
    void setDamper(bool engaged);
    // Half pedal: engage the damper only to a fraction of its full damping. The fraction snaps to a 16-sample ramp
    // grid; the rail motion ramps there.
    void setDamperFraction(real f);
    void setPickupOffset(double x0);     // [m]
    void setGain(double g);
    void setMechScale(double s);         // coupling of this key into the mech modes (linear, 1 = as the gain)
    void setSleepEnergy(double e) { sleepEnergy_ = real(e); }

    void choke();                        // stolen: 60 dB in 20 ms, cut after 30 ms

    // Adds the pickup voltage of n samples (at the internal rate) to out and, if force is given, the excitation force
    // [N] times the key's output gain and the strike's level to force (it drives the mechanical resonances of the
    // engine).
    void process(float* out, int n, float* force = nullptr);
    void setMechExponent(double e) { mechExp_ = real(e); }
    // The damper landing on the reed strikes it once: an impulse [N s] at the damper contact excites every mode with
    // its shape there; the felt (already engaged) damps the ring within tens of ms. 0 = off.
    void setDamperKnock(double impulse, double periods = 0.0) { knock_ = real(impulse); knockPeriods_ = real(periods); }
    // the key was released: the damper falls by `fraction` (0..1) at releaseSpeed (0..1, MIDI note-off / 127)
    void damperHit(real fraction, real releaseSpeed = real(0.5));

    bool   active() const { return active_; }
    bool   choking() const { return chokeLeft_ > 0; }
    bool   hammerActive() const { return hammerActive_; }
    double energy() const;               // modal energy [J]
    real   level() const;                // energy relative to the sleep threshold
    double tipDisplacement() const;      // [m]
    // Integral of the excitation force [N s] since the strike (the analysis tools read the mean force of a host frame
    // as its difference)
    double hammerImpulse() const { return double(impulse_); }
    // P - 1 of mode m per sample or per hammer sub-step (tests; sub-step values are only
    // kept current while the hammer is active)
    std::complex<double> pole(int m, bool substep = false) const;

private:
    struct Shift { real dSig, dWd, sig, wd; };
    Shift shift(int m) const;            // damping of mode m relative to the free reed
    void updateCoefficients();           // after a change of damping
    void applyGlide();                   // poles at the pitch of the present swing (reedGlide), from the base poles
    void updateSubsteps();               // hammer sub-step coefficients
    void advanceDamper(int n);
    real stepFree();
    void stepFreeRun(int count, real* dx);   // count <= 16 free samples at once: the modes stay in registers over the run
    void pickupRun(int count, const real* dx, const real* dl, real* d);   // the pickup over a run: d[j] = pickupDelta(dx[j], dl[j])
    real stepHammer();                   // one sample in contact (hammer or pad); returns the change of the read position
    real tip() const;                    // read position from the modal state
    void resetPickup(real x);
    void syncPickup(real x);             // exact resynchronisation of the pickup state to the reed
    real pickupDelta(real dx, real dl = 0);   // Phi(x + dx) - Phi(x) + slope dl, advances x
    real linStep();                      // change of the fast copy's linear read position over the last sample
    void linRun(int count, real* dl);    // the same over the next count free samples (before stepFreeRun)
    real lawEval(real u) const;          // table capacitance at penetration u [mm]
    real tableLaw(real x) const;         // the table law at reed position x, averaged along the comb (pickupSpan)
    void tabulateSpan(double drop);      // the averaged law as this voice's own spline (configure)
    real onsetHp(real v);                // the strike high pass, applied to the pickup signal
    void knockStep();                    // adds one sample of the damper's blow to the modes
    real energySum() const;              // modal energy [J] in `real`
    void sleep();                        // silence below the threshold: the state cleared, inactive

    double fs_ = 88200.0, T_ = 1.0 / 88200.0;
    int    n_  = 0;                                     // modes
    int    n4_ = 0;                                     // ... rounded up to blocks of four (the vector loops; the padding is zero)
    int    K_  = 1;                                     // hammer sub-steps per sample

    // modal state z = (dq/dt + sigma q) + j omega_d q per mode (structure of arrays). The arrays of the per-sample loops
    // (stepFree, stepHammer, tip) run in blocks of four modes, one compiler vector each.
    static constexpr int kModeSlots = (kMaxModes + 3) & ~3;
    alignas(16) real zr_[kModeSlots] = {}, zi_[kModeSlots] = {};
    alignas(16) real qr_[kModeSlots] = {}, qi_[kModeSlots] = {};      // P - 1 per sample
    alignas(16) real sqr_[kModeSlots] = {}, sqi_[kModeSlots] = {};    // P - 1 per hammer sub-step
    alignas(16) real sbr_[kModeSlots] = {}, sbi_[kModeSlots] = {};    // sub-step input gain * phi(strike)
    alignas(16) real cTip_[kModeSlots] = {};                          // phi(read point) / omega_d
    alignas(16) real cStrike_[kModeSlots] = {};                       // phi(strike) / omega_d
    alignas(16) real vRe_[kModeSlots] = {}, vIm_[kModeSlots] = {};    // strike velocity weights
    real sigma_[kMaxModes] = {}, omegaD_[kMaxModes] = {};  // current decay rate, frequency
    real bqr_[kMaxModes] = {}, bqi_[kMaxModes] = {};    // P - 1 per sample without the glide (reedGlide)
    real glide_ = 0;                                    // relative frequency drop per mm of swing (0 = off)
    int  fastMode_ = -1;                                // the fundamental's fast copy (glide reads both)
    real phiLin_ = 0, cLin_ = 0;                        // its share read linearly: shape at the read point, / omega_d
    real linSlope_ = 0, linPrev_ = 0;                   // dPhi/dx at rest, the last linear read position
    bool substepStale_ = false;                         // sqr_ .. sbi_ need an update

    // the free reed (damper lifted), computed in double by configure()
    real fqr_[kMaxModes] = {}, fqi_[kMaxModes] = {};    // P - 1 per sample
    real fsqr_[kMaxModes] = {}, fsqi_[kMaxModes] = {};  // P - 1 per hammer sub-step
    real sigF_[kMaxModes] = {}, wdF_[kMaxModes] = {};   // decay rate, damped frequency
    real sd_[kMaxModes] = {};                           // sigmaDamper as real
    real phiTip_[kMaxModes] = {};                       // mode shape at the read point
    real phiDamp_[kMaxModes] = {};                      // mode shape at the damper contact
    real knock_ = 0;                                    // damper impulse [N s]
    real knockScale_ = 0;                               // impulse of the blow in flight
    real knockPeriods_ = 0;                             // duration of the blow in periods of the fundamental (0 = an impulse)
    int  knockLeft_ = 0, knockN_ = 0;                   // samples left of the blow, its length
    real knockC_ = 0, knockS_ = 0, knockRc_ = 1, knockRs_ = 0;   // phase of the blow's raised cosine, its turn per sample
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
    real ha_ = 2, hl_ = 0, hInvM_ = 0;                  // stiffness, exponent, hysteresis, 1/mass
    real hdt_ = 0;                                      // sub-step [s]
    real hammerForce_ = 0;                              // mean contact force of the last sample [N]
    real impulse_ = 0;                                  // integral of the excitation force since the strike [N s]
    int  hammerAge_ = 0, noContact_ = 0;
    int  noContactLimit_ = 0, hammerAgeLimit_ = 0;      // backcheck after 3 ms, timeout 250 ms

    // pad (excitation 1): moves with the key (xh_, vh_) and pulls through the foam until the force reaches the adhesion,
    // then the force falls to zero over pluckFall_ sub-steps (the tear)
    bool pluck_ = false;
    real pluckK_ = 0, pluckAdh_ = 0, pluckVelExp_ = 0, pluckFallSec_ = 0;
    real pluckLimit_ = 0;                               // adhesion of this strike [N]
    real pluckTorn_ = -1;                               // force at the tear (< 0: still bonded)
    int  pluckFall_ = 1, pluckStep_ = 0;
    real pluckFallW_ = 0;                               // 1 / pluckFall_
    real pluckForce(real ws);                           // the pad's force at reed position ws, advances the pad

    real keyGain_ = 0;                                  // output gain of the key (auto-voicing)
    real mechScale_ = 1;                                // per-key coupling into the mech modes
    real forceGain_ = 0;                                // keyGain_ * mechScale_: gain of the force output
    real mechExp_ = 1;                                  // exponent of the force law of the mech modes
    real level_ = 1, levelTarget_ = 1, levelGlide_ = 0; // level of the strike (vel.gainDb)
    real sleepEnergy_ = 0;                              // sleep threshold [J]

    // pickup: s = (x - x0) / w, t = 1 + s^2, flux ~ t^-p; electrostatic: t = 1 - s, flux = 1 / t
    bool fastPickup_ = true;                            // p == 1.5
    bool unitPickup_ = false;                           // p == 1: closed form, no libm
    bool electrostatic_ = false;                        // pickupType 1 to 3
    bool slot_ = false;                                 // pickupType 2 and 3: two plates, t = 1 - s^2
    bool diff_ = false;                                 // pickupType 3: the plates subtracted, odd in s
    bool table_ = false;                                // pickupType 4: capacitance from a table
    const PickupLaw* law_ = nullptr;                    // its section
    real lawSign_ = 1, lawPrev_ = 0, hpfW_ = 0, hpfA_ = 0, hpfB_ = 1;   // reed side, last law value, high pass state and coefficients
    bool hpfOn_ = false;
    real onsetHz0_ = 0, onsetFc_ = 0, onsetDecay_ = 1, onsetLp_ = 0, onsetLp2_ = 0, onsetFs_ = 44100;   // strike high pass (two poles): start, now, decay per 16 samples, states
    real x0_ = 0, invW_ = 1000;
    real warp_ = 0;                                     // [1/mm] quadratic warp of the penetration axis
    real xPrev_ = 0, sPrev_ = 0, tPrev_ = 1, rtPrev_ = 1;
    bool pole_ = false;                                 // the law averaged over the pole face (pickupPole)
    bool span_ = false;                                 // the table law averaged along the comb (pickupSpan)
    static constexpr int kSpanKnots = 768;
    float spanY_[kSpanKnots] = {}, spanM_[kSpanKnots] = {};   // the averaged law over u = sign x / w: values, second derivatives
    real spanU0_ = 0, spanDu_ = 1;                      // its first knot and spacing
    int spanN_ = 0;
    real poleA_ = 0, inv2a_ = 0, rpPrev_ = 1, rmPrev_ = 1;   // its half-width, 1 / 2a, sqrt(1 + (s +- a)^2)
    double pExp_ = 1.5, phiPrev_ = 1.0;                 // generic exponent (slow path)
    real gain_ = 0;                                     // -gain * fs  (u = -N dPhi/dt)

    bool active_ = false;
};

} // namespace pmp
