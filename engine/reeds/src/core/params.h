// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// params.h - physical parameters of the instrument
//
// All values are physical quantities (SI units unless noted). Values that vary over the
// keyboard are stored as piecewise-linear curves over the MIDI key number.
// Parameters can be changed by name ("pickup.width@2=0.9", "hammer.mass*=1.2", ...),
// which is used by the render tool, the host and the Python calibration scripts.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>

namespace pmp {

// The adjustments a technician makes at the instrument (the "voicing"): hammer or pad, reed
// gauge, pickup gap and position, damper. Each one acts on top of the calibrated preset, so
// all-neutral is exactly the preset as calibrated and the A/B accuracy against the recordings is
// preserved. Percent values scale the calibrated per-key curve. The pickup offset is also a live
// control (Engine::setVoicing, mod wheel CC1); the geometric controls (reed gauge) need a
// prepare().
struct Voicing {
    float hammerHardness = 0.0f;   // [%] Wurlitzer: felt hardness, +100 = half the contact time (brighter);
                                   //     Pianet: the pad lets go faster, +100 = half the tear time (brighter)
    float hammerMass     = 0.0f;   // [%] Wurlitzer: hammer weight; Pianet: adhesion of the pad (force at the tear)
    float strikePoint    = 0.0f;   // [%] contact point of hammer or pad along the reed
    float reedGauge      = 0.0f;   // [%] reed thickness (stiffness; the designer re-tunes by the length)
    float pickupGap      = 0.0f;   // [%] Wurlitzer: slot width; Pianet: field width (gap + pole size); wider = softer
    float pickupOffset   = 0.0f;   // pickup offset x0 / width (added to the preset)
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
    // --- tuning and range -------------------------------------------------------------
    float a4 = 440.0f;                 // [Hz]
    float keysLo = 29.0f;              // lowest key of the instrument (MIDI); keys outside the range stay silent
    float keysHi = 96.0f;              // highest key

    // --- voicing: the controls of the instrument, on top of the calibrated preset ----------
    // (see Voicing above). All neutral: the preset sounds exactly as calibrated.
    Voicing voicing;

    // --- reed: spring-steel strip clamped at its root, tuning weight at its tip -------------
    float reedThickness = 0.5e-3f;     // [m] in the swing direction
    float reedWidth     = 3.0e-3f;     // [m]
    float reedYoungs    = 2.0e11f;     // [Pa]
    float reedDensity   = 7850.0f;     // [kg/m^3]
    float tipMu         = 0.0f;        // tuning weight / reed mass (the Wurlitzer's solder; 0: none)
    float tipPos        = 0.98f;       // its position / reed length
    Curve tipMuByKey;                  // per-key override of the weight (empty: reed.tipMu)
    float lossHf       = 0.05f;        // extra decay of higher modes: sigma += lossHf*(f^e - f1^e)
    float lossExp      = 0.7f;         // exponent e of the loss law
    Curve lossHfByKey;                 // per-key lossHf (empty: reed.lossHf)
    Curve lossExpByKey;                // per-key exponent (empty: reed.lossExp)
    float lossHf2      = 4.0e-6f;      // quadratic part (thermoelastic/air): + lossHf2*(f^2 - f1^2)
    Curve lossHf2ByKey;                // per-key override of the quadratic high-mode loss
    Curve t60;                         // [s] 60 dB decay of the fundamental (from reference)
    Curve reedGlide;                   // per key [cents per mm]: the pitch drops with the swing of the fundamental mode at the tip, in
                                       // mm of the pickup law (swing / pickup.width); slip at the reed's clamp, linear in the
                                       // amplitude, so the note rises as it decays (Wurlitzer; empty/0: off)

    // --- excitation ---------------------------------------------------------------------
    float excitation = 0.0f;           // 0 the felt hammer (Wurlitzer), 1 the sticky pad plucks the reed (Pianet)
    Curve strikePos;                   // contact point of hammer or pad / reed length

    // hammer (Hunt-Crossley contact)
    Curve hammerMass;                  // [kg]
    Curve contactMs;                   // [ms] contact time against a rigid wall at 1 m/s
    Curve hammerAlpha;                 // contact force exponent
    float hammerLambda   = 0.4f;       // [s/m] contact hysteresis
    float hammerSubsteps = 2.0f;       // integration sub-steps during contact (4: inaudible)
    float hammerSpeedHardening = 0.0f; // [%] extra felt hardness above 2 m/s, full at 3 m/s
    Curve hammerSpeedHardeningByKey;   // [%] per-key adjustment to the speed hardening

    // pad (Pianet): the key lifts the pad, the adhesion drags the reed along through the foam until the bond tears; the
    // adhesion is given as the static deflection of the reed at the pad it holds (pluck.tearMm), the foam's stiffness
    // relative to the reed's (pluck.padRatio); after the tear the force falls to zero within pluck.releaseMs
    Curve pluckTearMm;                 // [mm] per key: static deflection of the reed at the pad at which the bond tears (1 m/s)
    float pluckPadRatio = 3.0f;        // stiffness of the pad's foam / static stiffness of the reed at the pad
    float pluckVelExp = 0.3f;          // the adhesion grows with the key speed: (v / 1 m/s)^velExp
    Curve pluckReleaseMs;              // [ms] per key: time of the tear (shorter = brighter)

    // --- velocity -> hammer (or key) speed ------------------------------------------------
    float velMin   = 0.02f;            // [m/s] at MIDI velocity 0
    float velMax   = 3.0f;             // [m/s] at MIDI velocity 127
    float velGamma = 1.33f;            // curve exponent (calibrated)
    // key action as measured: MIDI velocity -> speed [m/s]; replaces the power law above when it has points
    Curve velSpeed;
    // level of a strike over MIDI velocity [dB] (0 at the top): the timbre follows the speed, this curve adds the
    // recordings' velocity -> level law; empty: none
    Curve velGainDb;
    // per key: extra level slope [dB per velocity step] around v127, i.e. more (positive) or
    // less (negative) dynamic range in a register; added to velGainDb as slope * (v - 127)
    Curve velGainSlope;
    // per key: soft limit of the speed [m/s], v / (1 + (v/cap)^4)^(1/4); empty or 0: none
    Curve velSpeedCapByKey;

    // --- pickup ---------------------------------------------------------------------------
    // 0 electromagnetic (Pianet T): flux ~ (1 + s^2)^-p; electrostatic (Wurlitzer): 1 one plate, 1 / (1 - s); 2 the
    // slot, 1 / (1 - s^2); 3 the plates subtracted, s / (1 - s^2) (odd); 4 the slot between the fingers, the
    // capacitance from the table of the 2D field solve (pickup_tables.h); s = (x - x0) / w
    float pickupType = 0.0f;
    Curve pickupWidth;                 // [mm] field width w (magnetic: ~ gap + pole size; electrostatic: the gap)
    Curve pickupOffset;                // pole or finger offset x0 / w ("voicing")
    float pickupExponent = 1.5f;       // magnetic: flux ~ (1 + ((x-x0)/w)^2)^-p
    float pickupWarp = 0.0f;           // [1/mm] quadratic warp of the table's penetration axis (0: the 2D solve as is)
    float pickupPole = 0.0f;           // magnetic, p = 1.5: half-width of the pole face in widths, the law averaged over it
    float pickupComb   = 0.0f;         // table law: length of the reed in the comb / reed length, the law averaged along it (0: one cross-section)
    float pickupAxial  = 1.0f;         // where the pickup reads the reed / reed length (1 = the tip)
    float pickupHpfHz = 0.0f;          // [Hz] type 4: corner of the one-pole high pass behind the capacitance law (0 = the plain difference)
    Curve pickupOnsetMul;              // per key: the fundamental builds up over pickup.onsetMs: a high pass of this multiple of f1
                                       // glides down at the strike (empty/0: off)
    float pickupOnsetMs = 15.0f;       // [ms] time constant of that glide

    // --- damper ---------------------------------------------------------------------------
    float damperPos    = 0.8f;         // damper contact point / reed length
    Curve releaseT60;                  // [s] 60 dB decay of the fundamental with damper on
    // The Wurlitzer's fundamental decays in two stages (8-18 dB in the first 0.2 s, then with decay.t60, at every
    // velocity; docs/REEDS.md 27): a copy of the fundamental mode at its frequency takes the share s of its amplitude
    // and decays with fastT60, the mode keeps 1 - s (as pmtines' tonebar). Empty: one mode.
    Curve fastShareByKey;              // 0..1
    Curve fastT60ByKey;                // [s]
    float fastLinear = 0.0f;           // share of the fast part read at the pickup's slope at rest, 0..1 (docs/REEDS.md 29)
    float damperRampMs = 10.0f;        // [ms] felt compression time
    Curve damperHarpT60;               // [s] extra decay of every mode while the felt is pressed (empty: off)
    Curve damperVelDb;                 // [dB] damper effectiveness over MIDI velocity, added
                                       // at the strike (0/empty: velocity-independent)
    float damperPedal = 1.0f;          // 1: the instrument has a sustain pedal (CC64 lifts the dampers); 0: CC64 is ignored
    float damperPedalLost = 0.5f;      // CC64/127 below this keeps the dampers fully on
    float damperPedalFull = 0.85f;     // CC64/127 above this lifts them fully off
    // the damper landing on the reed strikes it: an impulse [N s] of damper.knock x the hammer mass (or 1 g for the pad)
    // x 1 m/s, spread over damper.knockPeriods periods of the fundamental (0 = an impulse), by key in dB; 0 = off
    float damperKnock = 0.0f;
    float damperKnockPeriods = 0.0f;
    Curve damperKnockByKey;

    // --- mechanics: the blow shakes the frame/action, the pickups see it ------------------
    // Up to three damped structural modes, shared by all keys and driven by the excitation force.
    // The output is the mode velocity times the coupling; gain 0 switches a mode off.
    float mech1Hz = 160.0f, mech1T60 = 0.3f, mech1Gain = 0.0f;   // [Hz], [s], coupling
    float mech2Hz = 70.0f,  mech2T60 = 0.3f, mech2Gain = 0.0f;
    float mech3Hz = 600.0f, mech3T60 = 0.05f, mech3Gain = 0.0f;
    float mechExp = 1.0f;              // the mech modes follow F (F / 10 N)^(exp - 1) of the force F (1 = proportional)
    Curve mechExpByKey;                // per-key exponent (empty: mech.exp)
    Curve mechByKey;                   // [dB] per-key coupling of the force into all three modes,
                                       // added to the gains above (empty: 0 dB for every key)

    // --- the blow: the hammer's knock on the reed bar, broadband and short ---------------------------
    // The attack of the WörliTzer's recordings carries a carpet over 80 Hz-2 kHz between the lines, ~26 dB under the
    // tone in the first 50 ms and gone within ~100 ms (docs/REEDS.md 12): a noise burst per strike, band-limited
    // (2nd-order high and low pass), decaying with blow.t60. gain = the noise's RMS before the filters relative to the
    // voice level (voiceRms) for a blow at 1 m/s, times (speed / 1 m/s)^blow.exp, the strike's level and
    // blow.gainByKey [dB]. Its own noise generator (the strike variation keeps its sequence). 0 = off.
    float blowGain = 0.0f;
    float blowT60 = 0.05f;             // [s]
    float blowLoHz = 80.0f, blowHiHz = 2000.0f;
    float blowExp = 1.0f;
    float blowLpOrder = 2.0f;          // order of the burst's low pass: 2 (12 dB/oct) or 4 (24 dB/oct)
    Curve blowByKey;                   // [dB] per key (empty: 0)
    Curve blowSteepByKey;              // share of the burst through the 4th-order low pass per key, 0..1 (empty: 1);
                                       // the rest takes a 2nd-order one (blow.lpOrder 4 only)
    // The lead: in the WörliTzer's loud treble the knock comes before the reed sounds (median 6-8 ms from v84 on, up to
    // 17 ms around F#5-B5; docs/REEDS.md 18) - the key bottoms out on the keybed while the hammer is still in flight.
    // At note-on the knock sounds, the hammer strikes the reed blow.leadMs(key) x ramp(velocity) x blow.lead later; the
    // ramp rises from blow.leadVel0 to blow.leadVel1 (smoothstep). blow.lead is the player's switch: 0 = no lead, no
    // added latency.
    Curve blowLeadMs;                  // [ms] per key at full ramp (empty: none)
    float blowLeadVel0 = 70.0f, blowLeadVel1 = 90.0f;
    float blowLead = 1.0f;

    // --- the instrument's own character: static deviations per key ------------------------------
    // Every key of a real instrument is tuned and voiced a little differently. Each key gets a fixed
    // Gaussian offset from the seed (the same on every prepare, in every build): of its tuning
    // [cent] and of its loudness [dB, on top of auto-voicing]. 0 = off.
    float keysTuneCents = 0.0f;        // sigma of the tuning offset
    float keysLevelDb = 0.0f;          // sigma of the loudness offset
    float keysSeed = 1.0f;
    Curve keysTuneByKey;               // [cent] measured tuning offset per key, added to the seeded one (empty: 0)

    // --- release thump -------------------------------------------------------------------------
    // Letting go of a key lowers the damper onto the reed and the key onto its stop: the frame/action
    // rings with two damped modes, independent of the velocity. gain = peak of the mode's output
    // relative to the voice level (voiceRms); 0 = off. The delay is the time from the key release to
    // the blow. The by-key curves are [dB] per key (empty: 0). No thump while the damper pedal holds
    // the dampers off.
    float rel1Hz = 65.0f,  rel1T60 = 0.46f, rel1Gain = 0.0f, rel1DelayMs = 21.0f;
    float rel2Hz = 140.0f, rel2T60 = 0.38f, rel2Gain = 0.0f, rel2DelayMs = 12.0f;
    Curve rel1ByKey, rel2ByKey;
    float humanRelease = 0.0f;         // sigma of ln(thump amplitude) per release (0 = off)

    // --- pedal clunk ----------------------------------------------------------------------------
    // Pressing the sustain pedal (CC64 reaching 64) lifts the damper rail, letting go drops it back:
    // each rings the frame in a band of modes. The blow is a short burst of noise from the seeded
    // generator, so no two clunks are alike. gain = RMS of the first 300 ms relative to the voice
    // level (voiceRms); 0 = off. human.pedal scatters the size.
    float pedalDownGain = 0.0f, pedalUpGain = 0.0f;
    float humanPedal = 0.0f;           // sigma of ln(clunk amplitude) per pedal movement (0 = off)

    // --- strike variation (round robin) -----------------------------------------------------
    // Every note-on draws two Gaussian factors from a seeded generator: the speed and the contact
    // stiffness (hammer) or adhesion (pad) are multiplied by exp(sigma * g). 0 = off: every strike
    // of a note is then the same, bit for bit.
    float humanSpeed = 0.0f;           // sigma of ln(speed)
    float humanContact = 0.0f;         // sigma of ln(contact stiffness or adhesion)
    float humanSeed = 1.0f;            // seed of the generator (applied on the first prepare and when it changes)

    // --- electrics / output ---------------------------------------------------------------
    float lpfHz      = 12000.0f;       // pickup/cable resonance (2nd order low pass)
    float lpfQ       = 0.707f;
    float hpfHz      = 20.0f;          // coupling capacitor (1st order high pass)
    Curve keyGainDb;                   // [dB] loudness balance on top of auto-voicing
    float masterGain = 1.0f;
    float trimDb     = 0.0f;           // [dB] level of the instrument against the others (the preset's), apart from out.gain (the player's volume)
    float voiceRms   = 0.08f;          // auto-voicing target RMS at velocity 100

    // --- numerics ---------------------------------------------------------------------------
    float oversampling = 2.0f;         // 1 or 2
    float femElements  = 20.0f;        // beam elements of the reed model
    float maxModes     = 8.0f;         // modes per key (1..16); fewer = cheaper (embedded)
    float maxVoices    = 0.0f;         // polyphony limit, 0 = all keys (embedded: CPU budget)
    float sleepDb      = 120.0f;       // a key sleeps when its energy fell by this amount

    Params();                          // base values plus the default instrument (the first preset, "wurli")

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
    static const char* presetId(int index);      // "wurli", "pianet"
    static const char* presetName(int index);    // "Wurlitzer 200A" ...
    static int presetIndex(const std::string& id);   // -1 if unknown
    static bool hasPreset(const std::string& id);
    bool loadPreset(const std::string& id, std::string* error = nullptr);   // the base values plus the preset's text

private:
    explicit Params(std::nullptr_t);   // the base values alone, shared by every preset
};

} // namespace pmp
