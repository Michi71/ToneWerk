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
    float springMu     = 0.20f;        // tuning spring mass / tine mass and its position: the strike
    float springPos    = 0.86f;        // click of five sets shows the tine's modes 2-6 at 7.45, 22.5,
                                       // 40, 66, 96 f1 (docs/ATTACK_WINDOWS.md 12-13); 0.22 at 0.93 put
                                       // modes 4-6 at 44.6, 74.6, 111 (9-15 % too high)
    Curve springMuByKey;              // per-key override of the spring mass ratio (empty: springMu)
    Curve springPosByKey;             // per-key override of the spring position (empty: springPos)
    float lossHf       = 0.05f;        // extra decay of higher modes: sigma += lossHf*(f^e - f1^e)
    float lossExp      = 0.7f;         // exponent e of the loss law
    Curve lossHfByKey;                // per-key lossHf (the wrapped bass carries its highs)
    Curve lossExpByKey;               // per-key exponent (empty: tine.lossExp)
    float lossHf2      = 4.0e-6f;      // quadratic part (thermoelastic/air): + lossHf2*(f^2 - f1^2)
    Curve lossHf2ByKey;              // per-key override of quadratic high-mode loss
    Curve t60;                         // [s] 60 dB decay of the fundamental (from reference)

    // --- tonebar (lumped resonator at the clamp) ---------------------------------------
    // Experimental: a weakly coupled tonebar mode is hardly damped by the damper and rings
    // on after note-off. Off by default until the tuning fork (tine + tonebar + stem) is
    // modelled properly (docs/PLAN.md, P2).
    float tonebarSplit  = 0.0f;        // [cent] mode split at zero detune (0 = rigid clamp)
    float tonebarDetune = 6.0f;        // [cent] tonebar tuning relative to the tine
    float tonebarLoss   = 0.8f;        // tonebar decay rate relative to the tine fundamental
    // The measured tonebar (docs/ATTACK_WINDOWS.md 5): a resonator well above the tine
    // (2.7-7 semitones in the treble) that is struck with the tine and dies within tens of
    // milliseconds. tonebar.mass sets its mass directly (ratio to the tine mass; > 0 replaces
    // the split as the coupling), tonebar.t60 its own decay [s] (> 0 replaces tonebar.loss),
    // the ByKey curves override the scalars per key.
    float tonebarMass   = 0.0f;        // tonebar mass / tine mass (0 = coupling from the split)
    float tonebarT60    = 0.0f;        // [s] decay of the tonebar-dominant mode (0 = tonebar.loss)
    float tonebarGyr    = 0.0f;        // radius of gyration in tine lengths (> 0: the clamp rotates, the
                                       // tip follows with the lever of the tine; 0 = translation only)
    Curve tonebarDetuneByKey;          // [cent] per key (empty: tonebar.detune)
    Curve tonebarMassByKey;            // per key (empty: tonebar.mass)
    Curve tonebarT60ByKey;             // [s] per key (empty: tonebar.t60)
    Curve tonebarLateralByKey;         // share of the tonebar-dominant mode's tip motion across the swing plane, 0..1
    // The resonance below f1 decays in two stages in the recordings (T60 1.5-2 s at first, 4-4.5 s later,
    // docs/ATTACK_WINDOWS.md 24): the tonebar swings in two planes on its grommets, damped differently. The
    // tonebar-dominant mode splits into a fast and a slow part at the same frequency; fastShare is the fast
    // part's share of the amplitude at the pickup, fastT60 its decay (the slow part keeps tonebar.t60ByKey).
    Curve tonebarFastShareByKey;       // 0..1 (empty: 0, one mode)
    Curve tonebarFastT60ByKey;         // [s]
    // The upper tonebar mode (docs/ATTACK_WINDOWS.md 5): a second, detuned resonator 1.17-1.55 f1 above
    // the tine, struck with it and dead within the attack (T60 of the fast part, a weak slow rest). It is
    // the candidate for the short-lived lines the model lacks in 0-20 ms; off by default until measured.
    float tonebarUpperDb   = -120.0f;  // [dB] level at the pickup relative to H1 (<= -60: off)
    float tonebarUpperRatio = 0.0f;    // f/f1 (0: auto 1.49 at E4 .. 1.17 at A#6)
    float tonebarUpperT60  = 0.03f;    // [s] fast part
    float tonebarUpperSlowT60 = 0.20f; // [s] slow rest
    float tonebarUpperFastShare = 0.85f; // share of the fast part (0..1)
    Curve tonebarUpperDbByKey;         // [dB] per key (empty: tonebar.upperDb)
    Curve tonebarUpperRatioByKey;      // f/f1 per key (empty: auto from the key)
    float tonebarUpperVelExp = 0.0f;   // extra velocity exponent of the upper mode's strike input
    Curve tonebarUpperVelExpByKey;     // per key (empty: tonebar.upperVelExp)
    // The click of the recordings decays 1-2.6 dB/ms after the strike (T60 25-50 ms) and stands 30-46 dB over the
    // ring, the model's mode 2 decays like the bell (docs/ATTACK_WINDOWS.md 28): mode 2 splits like the tonebar,
    // a copy at its frequency with the share s of its amplitude at the pickup decays with fastT60, the bell keeps
    // 1 - s. Empty: one mode.
    Curve tineMode2FastShareByKey;     // 0..1
    Curve tineMode2FastT60ByKey;       // [s]
                                       // (docs/ATTACK_WINDOWS.md 10.7; empty: 0, the mode moves in the plane)

    // --- hammer (Hunt-Crossley contact) ------------------------------------------------
    Curve strikePos;                   // contact point / tine length
    Curve hammerMass;                  // [kg]
    Curve contactMs;                   // [ms] contact time against a rigid wall at 1 m/s
    Curve hammerAlpha;                 // contact force exponent
    float hammerLambda   = 0.4f;       // [s/m] contact hysteresis
    float hammerSubsteps = 2.0f;       // integration sub-steps during contact (4: inaudible)
    float hammerSpeedHardening = 0.0f; // [%] extra tip hardness above 2 m/s, full at 3 m/s
    Curve hammerSpeedHardeningByKey;    // [%] per-key adjustment to the speed hardening
    // The hard core under the tip (docs/ATTACK_WINDOWS.md 14): once the neoprene is compressed
    // to the depth a blow of hammerCoreSpeed [m/s] reaches, the core adds its own Hertz-like
    // contact k2 (delta - dc)^coreAlpha, k2 from its contact time hammerCoreMs against a rigid
    // wall at 1 m/s (like hammer.contact). Empty / 0 = no core (the pure Hunt-Crossley tip).
    Curve hammerCoreSpeed;             // [m/s] hammer speed at which the tip bottoms out (0: off)
    Curve hammerCoreMs;                // [ms] contact time of the core alone at 1 m/s (empty: 0.3)
    float hammerCoreAlpha = 1.5f;      // force exponent of the core (Hertz: 1.5)
    // The core sets in over a knee of +-coreKnee x the core depth around it (docs/ATTACK_WINDOWS.md 28): the
    // layer above it stiffens with the compression, a blow just short of the depth already feels part of it.
    // The excess e = delta - dc goes through (e + k)^2 / 4k for |e| < k; empty / 0 = at once (the step).
    Curve hammerCoreKnee;              // fraction of the core depth, 0..0.95

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
    // per key: soft limit of the hammer speed [m/s], v / (1 + (v/cap)^4)^(1/4): a heavy hammer does not reach
    // the speed the action gives a light one (mark8 bass, docs/ATTACK_WINDOWS.md 37); empty or 0: none
    Curve velSpeedCapByKey;

    // --- electromagnetic pickup ---------------------------------------------------------
    Curve pickupWidth;                 // [mm] field width w (~ gap + pole size)
    Curve pickupOffset;                // pole offset x0 / w ("voicing")
    float pickupExponent = 1.5f;       // flux ~ (1 + ((x-x0)/w)^2)^-p
    float pickupHystQ = 0.0f;          // flux from the hysteretic magnetization (0: off)
    float pickupHystBeta = 0.5f;       // Bouc-Wen loop shape
    float pickupHystGamma = 0.5f;
    float pickupAxial  = 1.0f;         // where the pickup reads the tine / tine length (1 = the tip)
    Curve pickupAxialByKey;            // per key (empty: pickup.axial)
    float pickupLateralOffset = 0.5f;  // pole off the swing plane / field width, seen by the lateral motion
                                       // (tonebar.lateralByKey); the law in the plane stays the calibrated one

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

    // --- mechanics: the hammer blow shakes the harp/action, the pickups see it ------------
    // Up to three damped structural modes, shared by all keys and driven by the hammer
    // contact force (a low "thump" in the attack, pronounced in the MK8, docs/PLAN.md 2.6).
    // The output is the mode velocity times the coupling; gain 0 switches a mode off.
    float mech1Hz = 160.0f, mech1T60 = 0.3f, mech1Gain = 0.0f;   // [Hz], [s], coupling
    float mech2Hz = 70.0f,  mech2T60 = 0.3f, mech2Gain = 0.0f;
    float mech3Hz = 600.0f, mech3T60 = 0.05f, mech3Gain = 0.0f;
    float mechExp = 1.0f;              // the mech modes follow F (F / 10 N)^(exp - 1) of the hammer force F (1 = proportional)
    Curve mechExpByKey;                // per-key exponent (empty: mech.exp)
    Curve mechByKey;                   // [dB] per-key coupling of the force into all three modes,
                                       // added to the gains above (empty: 0 dB for every key)

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

    // --- pedal clunk ----------------------------------------------------------------------------
    // Pressing the sustain pedal (CC64 reaching 64) lifts the damper rail, letting go drops it back:
    // each rings the frame in a band of modes between 95 and 207 Hz (docs/MK8_PEDAL.md). The blow is a
    // short burst of noise from the seeded generator, so no two clunks are alike. gain = RMS of the
    // first 300 ms relative to the voice level (voiceRms); 0 = off. human.pedal scatters the size.
    float pedalDownGain = 0.0f, pedalUpGain = 0.0f;
    float humanPedal = 0.0f;           // sigma of ln(clunk amplitude) per pedal movement (0 = off)

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

    // --- numerics ---------------------------------------------------------------------------
    float oversampling = 2.0f;         // 1 or 2
    float femElements  = 20.0f;        // beam elements of the tine model
    float maxModes     = 10.0f;        // modes per key (1..10); fewer = cheaper (embedded)
    float maxVoices    = 0.0f;         // polyphony limit, 0 = all keys (embedded: CPU budget)
    float sleepDb      = 120.0f;       // a key sleeps when its energy fell by this amount

    Params();                          // base values plus the default instrument (preset "mark1")

    // Scope: a public parameter is what a player sets (host, pmp-render --set) - the voicing
    // controls of the physical model and the instrument's output level. The raw model parameters
    // belong to the presets, the calibration and the analysis tools and are internal: the
    // C API and pmp-render --internal still set them, a plain set() refuses them.
    static bool isPublic(const std::string& name);
    // "name=value", "name*=f", "name+=d", "curve@i=value", "curve=28:1,64:2,100:3"; internalToo = false: public names only
    bool set(const std::string& assignment, std::string* error = nullptr, bool internalToo = true);
    // several assignments, one per line; '#' starts a comment
    bool setText(const std::string& text, std::string* error = nullptr, bool internalToo = true);
    std::string dump(bool internalToo = true) const;   // one per line; internalToo = false: the public parameters only

    // Instrument presets: defaults plus a calibrated parameter set per instrument.
    static int presetCount();
    static const char* presetId(int index);      // "mark1", "mark2", "mark5", "mark7", "mark8", "sparkletop"
    static const char* presetName(int index);    // "Rhodes Mark I" ...
    static int presetIndex(const std::string& id);   // -1 if unknown
    static bool hasPreset(const std::string& id);
    bool loadPreset(const std::string& id, std::string* error = nullptr);
};

} // namespace pmp
