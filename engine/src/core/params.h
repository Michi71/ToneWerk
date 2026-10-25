// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// params.h - physical parameters of the instrument
//
// All values are physical quantities (SI units unless noted). Values that vary over the
// keyboard are stored as piecewise-linear curves over the MIDI key number.
// Parameters can be changed by name ("pickup.width@2=0.9", "hammer.mass*=1.2", ...),
// which is used by the render tool, the host and the Python calibration scripts.
#pragma once

#include <initializer_list>
#include <string>
#include <utility>

namespace pmp {

// The adjustments a Rhodes technician makes at the instrument (the "voicing"): hammer tip,
// tine gauge, pickup distance and position, tonebar tuning, damper. Each one acts on top of
// the calibrated preset, so all-neutral is exactly the preset as calibrated and the A/B
// accuracy against the recordings is preserved. Percent values scale the calibrated per-key
// curve, cent values shift it. The pickup offset is also a live control (Engine::setVoicing,
// mod wheel CC1); the geometric controls (tine gauge, tonebar detune) need a prepare().
struct Voicing {
    float hammerHardness = 0.0f;   // [%] tip hardness: +100 = half the contact time (brighter)
    float hammerMass     = 0.0f;   // [%] hammer weight
    float strikePoint    = 0.0f;   // [%] contact point along the tine
    float tineGauge      = 0.0f;   // [%] tine diameter (stiffness; the designer re-tunes)
    float pickupGap      = 0.0f;   // [%] field width (gap + pole size); wider = softer
    float pickupOffset   = 0.0f;   // pickup offset x0 / width (added to the preset)
    float tonebarDetune  = 0.0f;   // [cent] added to the preset's tonebar detune
    float damperRelease  = 0.0f;   // [%] decay time with the damper on
    // the hybrid grand (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P8): on the poles of the bank at load time, the states stay
    float decay          = 0.0f;   // [%] +100 = every mode's T60 doubled, -50 = halved (2^(x/100))
    float unison         = 0.0f;   // [%] spread of the modes of one partial against each other (chorus): -100 = none, +100 = doubled
    float stretch        = 0.0f;   // [cent per octave] tuning stretch about C4 on the poles (the attack is not retuned: keep it small)
    // hammerHardness on the hybrid grand: +100 = the timbre (layers, filter, states, loop) of 32 velocity steps harder, the level stays
};

// Piecewise-linear function of the MIDI key number, constant beyond the end points.
struct Curve {
    static constexpr int kMaxPoints = 73;
    int   n = 0;
    float key[kMaxPoints] = {};
    float val[kMaxPoints] = {};

    Curve() = default;
    Curve(std::initializer_list<std::pair<float, float>> points);

    float operator()(float note) const;
    std::string toString() const;               // "28:1.2,64:0.9,100:0.6"
    bool parse(const std::string& text);         // inverse of toString()
};

struct Params {
    // --- tuning -----------------------------------------------------------------------
    float a4 = 440.0f;                 // [Hz]

    // --- voicing: the controls of the instrument, on top of the calibrated preset ----------
    // (see Voicing above). All neutral: the preset sounds exactly as calibrated.
    Voicing voicing;

    // --- tine: spring-steel cantilever with tuning spring ------------------------------
    float tineDiameter = 2.0e-3f;      // [m]  (assumption, no measurement available)
    float tineYoungs   = 2.0e11f;      // [Pa]
    float tineDensity  = 7850.0f;      // [kg/m^3]
    float springMu     = 0.30f;        // tuning spring mass / tine mass (B6: 0.25..0.4)
    float springPos    = 0.93f;        // spring position / tine length
    Curve springMuByKey;              // per-key override of the spring mass ratio (empty: springMu)
    Curve springPosByKey;             // per-key override of the spring position (empty: springPos)
    float lossHf       = 0.05f;        // extra decay of higher modes: sigma += lossHf*(f^e - f1^e)
    float lossExp      = 0.7f;         // exponent e of the loss law
    Curve lossHfByKey;                // per-key lossHf (the wrapped bass carries its highs)
    Curve lossExpByKey;               // per-key exponent (empty: tine.lossExp)
    float lossHf2      = 4.0e-6f;      // quadratic part (thermoelastic/air): + lossHf2*(f^2 - f1^2)
    Curve lossHf2ByKey;              // per-key override of quadratic high-mode loss
    Curve t60;                         // [s] 60 dB decay of the fundamental (from reference)

    // --- the pinned string (Clavinet D6, docs/CLAVINET.md) -------------------------------
    // tine.string switches the mode designer from the cantilever (Rhodes tine, Wurlitzer
    // reed) to a string pinned at both ends: the tangent traps it against the metal stud for
    // the duration of the note. The voice is read at the pickup position (the comb of the
    // magnetic transducer), struck at the tangent (hammer.strike); the yarn damping is uniform
    // (decay.release).
    float tineString    = 0.0f;        // 1 = string instead of the cantilever
    float stringTension = 150.0f;      // [N]
    float stringMass    = 3.0e-3f;     // [kg/m] (with winding: the wrapped value)
    float stringCore    = 0.9e-3f;     // [m] core diameter, for the bending stiffness
    float stringYoungs  = 2.0e11f;     // [Pa]
    float stringPickupPos = -1.0f;     // pickup / speaking length (from the tangent end)
    float stringPickupMeters = -1.0f;  // [m] pickup distance from the far end (the bridge sits
                                       // at a constant 4 cm, per the schematic); converted with
                                       // the designed string length. Meters win over the ratio.
    float stringPickup2Meters = -1.0f; // [m] second pickup from the far end; the summed selector
                                       // settings (A+D, B+D) read both combs
    float stringPickup2Gain   = 1.0f;  // its share, negative = anti-phase
    float stringReleaseSemitones = 0.0f; // pitch drop at key release: the tangent lifts and the
                                       // whole string speaks (the D6: 3 semitones); 0 = off
    float stringB         = -1.0f;     // measured inharmonicity B (<= 0: from the core stiffness)
    Curve stringMassByKey;             // kg/m per key (wound bass, plain treble)
    Curve stringPickupPosByKey;        // pickup position per key as a ratio (the fine fit)
    Curve stringPickupMetersByKey;     // pickup distance per key [m] (center pickup slant)
    Curve stringPickup2MetersByKey;    // second pickup distance per key [m] (the fine fit)
    Curve stringBByKey;                // measured B per key (empty: string.b, then the core)
    float stringDeadModes = 0.0f;     // whole-string modes behind the speaking ladder (0: off)
    float stringDeadGain  = 1.0f;     // excitation share of the whole-string modes
    float stringDeadT60   = 1.0f;     // [s] their decay (the yarn lies on that part)

    // --- the B selector set of the D6 pickup switch (clav.ab swaps a whole set) -----------
    // The D6 is one instrument: the right rockers pick which coil pair speaks. The A set
    // lives in the parameters above (the calibration against samples/clav_a); the B set
    // is its twin (calibrated against samples/clav_b) and clav.ab swaps between them,
    // clav.cd moves from the solo positions (C) to the parallel ones (D).
    Curve stringPickupMetersByKeyB;   // the B twin of string.pickupMetersByKey
    Curve stringPickup2MetersByKeyB;  // the B twin of string.pickup2MetersByKey
    float stringPickup2GainB = 0.0f;  // the B twin of string.pickup2Gain (0: no second comb)
    float pickupResHzB = 0.0f;        // the B twin of pickup.resHz (0: follow the A set)
    float pickupResQB  = 1.0f;        // its width
    float pickupResDbB = 0.0f;        // its height
    float clickHpHzB   = -1.0f;       // the B twin of click.hpHz (<0: follow the A set)
    Curve decayT60B;                  // the B twin of decay.t60 (empty: follow the A set)
    float clavAb = 0.0f;              // 0 = the A coil pair (C+A / D+A), 1 = the B pair
    float clavCd = 0.0f;              // 0 = C (solo, as calibrated), 1 = D (both pairs in
                                      // parallel: A+D in phase, B+D out of phase, per the
                                      // restored schematic)

    // --- tonebar (lumped resonator at the clamp) ---------------------------------------
    // Experimental: a weakly coupled tonebar mode is hardly damped by the damper and rings
    // on after note-off. Off by default until the tuning fork (tine + tonebar + stem) is
    // modelled properly (docs/PLAN.md, P2).
    float tonebarSplit  = 0.0f;        // [cent] mode split at zero detune (0 = rigid clamp)
    float tonebarDetune = 6.0f;        // [cent] tonebar tuning relative to the tine
    float tonebarLoss   = 0.8f;        // tonebar decay rate relative to the tine fundamental

    // --- hammer (Hunt-Crossley contact) ------------------------------------------------
    Curve strikePos;                   // contact point / tine length
    Curve hammerMass;                  // [kg]
    Curve contactMs;                   // [ms] contact time against a rigid wall at 1 m/s
    Curve hammerAlpha;                 // contact force exponent
    float hammerLambda   = 0.4f;       // [s/m] contact hysteresis
    float hammerSubsteps = 2.0f;       // integration sub-steps during contact (4: inaudible)
    float hammerSpeedHardening = 0.0f; // [%] extra tip hardness above 2 m/s, full at 3 m/s
    Curve hammerSpeedHardeningByKey;    // [%] per-key adjustment to the speed hardening

    // --- velocity -> hammer speed -------------------------------------------------------
    float velMin   = 0.02f;            // [m/s] at MIDI velocity 0
    float velMax   = 3.0f;             // [m/s] at MIDI velocity 127
    float velGamma = 1.33f;            // curve exponent (calibrated)
    // key action as measured: MIDI velocity -> hammer speed [m/s]; replaces the power law
    // above when it has points
    Curve velSpeed;
    // level of a strike over MIDI velocity [dB] (0 at the top): the timbre follows the hammer
    // speed, this curve adds the plugin's velocity -> level law (mk8); empty: none
    Curve velGainDb;
    // per key: extra level slope [dB per velocity step] around v127, i.e. more (positive) or
    // less (negative) dynamic range in a register; added to velGainDb as slope * (v - 127)
    Curve velGainSlope;

    // --- electromagnetic pickup ---------------------------------------------------------
    Curve pickupWidth;                 // [mm] field width w (~ gap + pole size)
    Curve pickupOnsetMul;              // per key: the fundamental of the key builds up over pickup.onsetMs: a high pass of this multiple of f1 glides down at the strike (empty/0: off)
    Curve reedGlide;                   // per key [cents per mm]: the pitch drops with the swing of the fundamental mode at the tip, in
                                       // mm of the pickup law (swing / pickup.width); slip at the reed's clamp, linear in the
                                       // amplitude, so the note rises as it decays (Wurlitzer, docs/WURLITZER.md; empty/0: off)
    float pickupOnsetMs = 15.0f;       // [ms] time constant of that glide
    float pickupOnsetVel = 0.0f;       // onset corner scales with the hammer speed:
                                       // corner *= (speed / 2 m/s)^p (0 = off). A harder
                                       // blow strikes a shorter, brighter pulse - the
                                       // velocity brightness rise the SGP layers show
    Curve pickupOffset;                // pole offset x0 / w ("voicing")
    float pickupExponent = 1.5f;       // flux ~ (1 + ((x-x0)/w)^2)^-p
    float pickupHpfHz = 0.0f;          // [Hz] type 4: corner of the one-pole high pass behind the capacitance law (0 = the plain difference)
    float pickupResHz = 0.0f;          // [Hz] resonance of the coil at the amp input (0: off) - the fixed colouring behind the law
    float pickupResQ = 1.0f;           // its width
    float pickupResDb = 0.0f;          // its height [dB]
    float pickupType = 0.0f;           // 0 electromagnetic (Rhodes); electrostatic (Wurlitzer): 1 one plate, 1 / (1 - s); 2 the slot, 1 / (1 - s^2); 3 the plates subtracted, s / (1 - s^2) (odd); s = (x-x0)/w

    // --- Clavinet D6 tone switch banks (clav_tone.h, docs/CLAVINET.md 2.2) ---------------
    // The four rockers between the amp stages; with all four off the stage is transparent
    // (the original stays mute - our calibration reference lives in the transparent state)
    float clavSoft      = 0.0f;        // 1 = 30 kOhm || 0.1 uF in the path (cuts the top gently)
    float clavMedium    = 0.0f;        // 1 = 10 kOhm || 15 nF (darker)
    float clavTreble    = 0.0f;        // 1 = 2 H || 4.7 nF trap, lifts around 1.6 kHz
    float clavBrilliant = 0.0f;        // 1 = 0.6 H coil (opens the top)

    // --- damper ---------------------------------------------------------------------------
    float damperPos    = 0.45f;        // damper contact point / tine length
    Curve releaseT60;                  // [s] 60 dB decay of the fundamental with damper on
    float damperRampMs = 10.0f;        // [ms] felt compression time
    Curve damperHarpT60;               // [s] extra decay of the whole tine/tonebar/harp
                                       // assembly while the felt is pressed (empty: off)
    Curve damperVelDb;                 // [dB] damper effectiveness over MIDI velocity, added
                                       // at the strike (0/empty: velocity-independent)
    float damperPedalLost = 0.5f;      // CC64/127 below this keeps the dampers fully on
                                       // (Pianoteq Mark II: unchanged damping up to CC64 ~60)
    float damperPedalFull = 0.85f;     // CC64/127 above this lifts them fully off
    float damperSlowShare = 0.0f;      // hybrid grand: share of the string's amplitude the felt does not reach (0 = off)
    float damperSlowT60 = 2.0f;        // [s] its decay under the damper

    // --- mechanics: the hammer blow shakes the harp/action, the pickups see it ------------
    // Up to three damped structural modes, shared by all keys and driven by the hammer
    // contact force (a low "thump" in the attack, pronounced in the MK8, docs/PLAN.md 2.6).
    // The output is the mode velocity times the coupling; gain 0 switches a mode off.
    float mech1Hz = 160.0f, mech1T60 = 0.3f, mech1Gain = 0.0f;   // [Hz], [s], coupling
    float mech2Hz = 70.0f,  mech2T60 = 0.3f, mech2Gain = 0.0f;
    float mech3Hz = 600.0f, mech3T60 = 0.05f, mech3Gain = 0.0f;
    float mechExp = 1.0f;              // the mech modes follow F (F / 10 N)^(exp - 1) of the hammer force F (1 = proportional)
    Curve clickByKey;                  // [dB] per-key coupling of the force into the click (empty: 0 dB), apart from mech.gainByKey
    Curve mechExpByKey, clickExpByKey; // per-key exponents (empty: mech.exp / click.exp)
    float mechLevelled = 1.0f;         // 1: the force driving the mech modes and the click carries the strike's level
                                       // curve (vel.gainDb); 0: the raw hammer force (Wurlitzer)
    Curve mechByKey;                   // [dB] per-key coupling of the force into all three modes,
                                       // added to the gains above (empty: 0 dB for every key)

    // --- hammer click: the hard part of the blow, behind the amplifier and speaker ---------------------
    // White noise gated by the same hammer force (so it follows the strike velocity and the key coupling
    // mech.gainByKey), band limited to [clickHpHz, clickLpHz] and added after the tone controls and the
    // cabinet: what the speaker would take away is in the recording of a real instrument (Wurlitzer,
    // docs/WURLITZER.md). gain 0 = off.
    float clickGain  = 0.0f;
    float clickHpHz  = 1500.0f;
    float clickLpHz  = 12000.0f;
    float clickExp   = 1.0f;           // the click follows F (F / 10 N)^(exp - 1) of the hammer force F: a harder blow is sharper

    // --- the instrument's own character: static deviations per key ------------------------------
    // Every key of a real instrument is tuned and voiced a little differently. Each key gets a fixed
    // Gaussian offset from the seed (the same on every prepare, in every build): of its tuning
    // [cent] and of its loudness [dB, on top of auto-voicing]. 0 = off: exact equal temperament and
    // an exactly even loudness (docs/MK8_KEYS.md).
    float keysTuneCents = 0.0f;        // sigma of the tuning offset
    float keysLevelDb = 0.0f;          // sigma of the loudness offset
    float keysSeed = 1.0f;
    Curve keysTuneByKey;               // [cent] measured tuning offset per key, added to the seeded one (empty: 0)

    // --- release thump -------------------------------------------------------------------------
    // Letting go of a key lowers the damper onto the tine and the key onto its stop: the harp/action
    // rings with two damped modes, independent of the velocity (docs/MK8_RELEASE.md). gain = peak of
    // the mode's output relative to the voice level (voiceRms); 0 = off. The delay is the time from
    // the key release to the blow. The by-key curves are [dB] per key (empty: 0). No thump while the
    // damper pedal holds the dampers off.
    float rel1Hz = 65.0f,  rel1T60 = 0.46f, rel1Gain = 0.0f, rel1DelayMs = 21.0f;
    float rel2Hz = 140.0f, rel2T60 = 0.38f, rel2Gain = 0.0f, rel2DelayMs = 12.0f;
    Curve rel1ByKey, rel2ByKey;
    float humanRelease = 0.0f;         // sigma of ln(thump amplitude) per release (strike variation, 0 = off)

    // --- damper knock: the damper landing strikes the reed ---------------------------------------------------
    // An impulse at the damper contact, hammer-like in size (the key's hammer mass times this speed [m/s], by-key
    // curve in dB): the reed rings at its pitch for tens of ms in the felt (Wurlitzer, docs/WURLITZER.md). 0 = off.
    float damperKnock = 0.0f;
    float damperKnockPeriods = 0.0f;   // duration of the blow in periods of the reed's fundamental (0 = an impulse: a click in the bass)
    Curve damperKnockByKey;

    // --- pedal clunk ----------------------------------------------------------------------------
    // Pressing the sustain pedal (CC64 reaching 64) lifts the damper rail, letting go drops it back:
    // each rings the frame in a band of modes between 95 and 207 Hz (docs/MK8_PEDAL.md). The blow is a
    // short burst of noise from the seeded generator, so no two clunks are alike. gain = RMS of the
    // first 300 ms relative to the voice level (voiceRms); 0 = off. human.pedal scatters the size.
    float pedalDownGain = 0.0f, pedalUpGain = 0.0f;
    float humanPedal = 0.0f;           // sigma of ln(clunk amplitude) per pedal movement (0 = off)
    // hybrid grand (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P6): the felt landing on the string, a high-passed noise burst per
    // release of a damped key, delayed, scaled by the damper fraction; the amplitude of the noise relative to the voice level
    // (voiceRms); 0 = off. human.release scatters it like the release thump.
    float damperWooshGain = 0.0f, damperWooshHz = 6000.0f, damperWooshT60 = 0.06f, damperWooshDelayMs = 30.0f;

    // --- strike variation (round robin) -----------------------------------------------------
    // Every note-on draws two Gaussian factors from a seeded generator: the hammer speed and the
    // contact stiffness (state of the felt) are multiplied by exp(sigma * g). 0 = off: every
    // strike of a note is then the same, bit for bit (docs/MK8_VARIATION.md).
    float humanSpeed = 0.0f;           // sigma of ln(hammer speed)
    float humanContact = 0.0f;         // sigma of ln(contact stiffness)
    float humanSeed = 1.0f;            // seed of the generator (applied on the first prepare and when it changes)

    // --- electrics / output ---------------------------------------------------------------
    float lpfHz      = 8000.0f;        // pickup/cable resonance (2nd order low pass)
    float lpfQ       = 0.8f;
    float hpfHz      = 20.0f;          // coupling capacitor (1st order high pass)
    Curve keyGainDb;                   // [dB] loudness balance on top of auto-voicing
    float masterGain = 1.0f;
    float trimDb     = 0.0f;           // [dB] level of the instrument against the others (the preset's), apart from out.gain (the player's volume)
    float voiceRms   = 0.08f;          // auto-voicing target RMS at velocity 100
    float softClip   = 1.0f;           // 1 = soft saturation near full scale, 0 = off (analysis)
    float hybridOn   = 0.0f;           // 1 = the hybrid grand (PCM attack + mode bank, piano_hybrid.h) instead of the physical model; needs a bank (Engine::setHybridBank), else the physical model plays and hybridState() says why
    float hybridBank = 0.0f;           // the bank slot of the hybrid voice: 0 = the grand (piano_hybrid.bank), 1 = the Mark I (mark1_hybrid.bank), 2 = the Mark II and the Suitcase (mark2_hybrid.bank), 3 = the Wurlitzer (wurli_hybrid.bank), 4 = the clavinet (clav_hybrid.bank), 5 = the Pianet (pianet_hybrid.bank), 6 = the Steinway (steinway_hybrid.bank), 7 = Grandpa's grand (grandpas_hybrid.bank), 8 = the CP80 (cp80_hybrid.bank)
    float hybridVelGain = 1.0f;        // 1 = vel.gainDb of the preset on top of the bank's loudness law (P4), 0 = the bank's law alone (the banks whose layers carry the recordings' dynamics)
    float hybridAmp  = 0.0f;           // 1 = the amp stage (amp.on: tone controls, power amp, cabinet) stays in the path on the hybrid voice (the Suitcase: its chain is designed, the Mark II bank carries none), 0 = bypassed there (the Wurlitzer: the recordings carry it)
    // the hybrid grand's resonance bank (pmp::HybridResonance, docs/PIANO_HYBRID_RESONANCE.md)
    float resOn       = 0.0f;          // 1 = the strings ring along (pedal, held keys, keys without a damper)
    float resGain     = 0.0f;          // coupling: input weight of a mode at the key's f0
    float resModes    = 16.0f;         // modes per key in the harp
    float resTiltDb   = 0.0f;          // [dB per octave] of the input weight above the key's f0
    float resUndamped = 1.0f;          // 1 = keys without a damper ring along without the pedal too

    // --- instrument chain after the pickups (all neutral = Stage piano, direct out) -------
    // Suitcase: preamp tone controls, stereo vibrato (auto-pan), power amp, speaker cabinet.
    float ampStage     = 1.0f;         // 1 = tone controls, power amp and cabinet are in the signal path, 0 = bypassed
    float toneBassDb   = 0.0f;         // [dB] preamp bass (low shelf at 150 Hz)
    float toneTrebleDb = 0.0f;         // [dB] preamp treble (high shelf at 3 kHz)
    float ampDrive     = 0.0f;         // [dB] power amp drive into saturation, 0 = clean
    float ampAsym      = 0.0f;         // asymmetry of the saturation (even harmonics), 0..0.5
    float ampPerVoice  = 0.0f;         // 1 = the saturation acts on every key before the sum (chords stay clean), 0 = on the sum
    float spkLowHz     = 0.0f;         // [Hz] cabinet resonance (2nd order high pass), 0 = off
    float spkLowQ      = 0.7f;
    float spkHighHz    = 0.0f;         // [Hz] cone roll-off (2nd order low pass), 0 = off
    float spkHighQ     = 0.7f;
    float vibRateHz    = 4.0f;         // [Hz] stereo vibrato rate
    float vibDepth     = 0.0f;         // 0 = off, 1 = full left/right panning
    float tremRateHz   = 5.6f;         // [Hz] amplifier tremolo (Wurlitzer 200A: the Twin-T oscillator, fixed there)
    float tremDepth    = 0.0f;         // 0 = off, 1 = gain swings to zero
    // stereo micro pitch shift as found in the Suitcase recordings (studio effect):
    // left +cents, right -cents, mixed to the dry signal
    float detuneCents  = 0.0f;         // [cent]
    float detuneMix    = 0.0f;         // level of the shifted copies (linear)
    float detuneWinMs  = 30.0f;        // [ms] grain window of the shifter (<= 40)

    // --- MK8 preamp and effect chain after the output stage (mk8_fx.h) --------------------
    // In the units of the Rhodes V8 Pro plugin, its factory values as defaults; off unless
    // fx.on = 1.
    float fxOn = 0.0f;
    float fxVolume = 53.0f;            // [%] preamp volume
    float fxDrive = 0.0f;              // [%]
    float fxEnvelope = 0.0f;           // [%] touch-wah (velvetkeys; no effect in the plugin)
    float fxBass = 0.0f;               // [dB] -15..15
    float fxMid = 0.0f;                // [dB] -15..15
    float fxMidFreq = 231.0f;          // [Hz] 100..2000
    float fxTreble = 0.0f;             // [dB] -15..15
    float fxPanOn = 0.0f;
    float fxPanWave = 0.0f;            // 0 square, 1 ramp, 2 triangle, 3 sine
    float fxPanRate = 1.09f;           // [Hz] 0.1..3800
    float fxPanDepth = 38.0f;          // [%]
    float fxCompOn = 0.0f;
    float fxCompAmount = 0.0f;         // [%]
    float fxCompMakeup = 14.0f;        // [dB] 0..20
    float fxChorusOn = 0.0f;
    float fxChorusRate = 2.0f;         // [Hz] 0.2..20
    float fxChorusDepth = 2.5f;        // [ms] 0..10
    float fxPhaserOn = 0.0f;
    float fxPhaserRate = 1.0f;         // [Hz] 0.1..10
    float fxPhaserDepth = 50.0f;       // [%]
    float fxDelayOn = 0.0f;
    float fxDelayTime = 0.5f;          // [s] 0..4
    float fxDelayFeedback = 50.0f;     // [%]
    float fxDelayMix = 50.0f;          // [%]
    float fxDelayPingPong = 1.0f;

    // --- numerics ---------------------------------------------------------------------------
    float oversampling = 2.0f;         // 1 or 2
    float femElements  = 20.0f;        // beam elements of the tine model
    float maxModes     = 10.0f;        // modes per key (1..10); fewer = cheaper (embedded)
    float maxVoices    = 0.0f;         // polyphony limit, 0 = all keys (embedded: CPU budget)
    float sleepDb      = 120.0f;       // a key sleeps when its energy fell by this amount

    Params();                          // base values plus the default instrument (preset "mark1")

    // Scope (2026-10-22, every instrument plays as a hybrid voice): a public parameter reaches the hybrid voices or the
    // output stage and is what a player sets (host, pmp-render --set). The rest belongs to the physical model - the
    // fallback without a bank, the calibration and the analysis tools - and is internal: presets, the C API and
    // pmp-render --internal still set it, a plain set() refuses it. README "Voicing" lists both sides.
    static bool isPublic(const std::string& name);
    // "name=value", "name*=f", "name+=d", "curve@i=value", "curve=28:1,64:2,100:3"; internalToo = false: public names only
    bool set(const std::string& assignment, std::string* error = nullptr, bool internalToo = true);
    // several assignments, one per line; '#' starts a comment
    bool setText(const std::string& text, std::string* error = nullptr);
    std::string dump(bool internalToo = true) const;   // one per line; internalToo = false: the public parameters only

    // Instrument presets: defaults plus a calibrated parameter set per instrument.
    static int presetCount();
    static const char* presetId(int index);      // "mark1", "mark2", "suitcase", "mk8"
    static const char* presetName(int index);    // "Rhodes Mark I" ...
    static int presetIndex(const std::string& id);   // -1 if unknown
    bool loadPreset(const std::string& id, std::string* error = nullptr);
};

} // namespace pmp
