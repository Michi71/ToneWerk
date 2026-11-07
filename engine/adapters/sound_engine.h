// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// sound_engine.h - one interface to the three sound engines of ToneWerk (engine/UPSTREAM.md)
//
// The Rhodes engine, the reed engine (Wurlitzer, Pianet) and the hybrid engine all call their engine pmp::Engine.
// Each is built as its own library with the namespace renamed (pmp_tines, pmp_reeds, pmp_hybrid), and the plugin reaches
// them only through this header, which names none of them. All three render mono.
#pragma once

#include <memory>
#include <string>

namespace tw::engines
{
enum class Family
{
    Tines,    // the Rhodes engine: physical models of the Rhodes
    Reeds,    // the reed engine: physical models of the reed pianos
    Hybrid    // the hybrid engine: a sampled attack that crossfades into computed modes
};

// The voicing controls on top of the calibrated preset; all 0 is the preset as calibrated. The physical models take the
// technician's controls, the hybrid voices hardness and the controls on their poles. A control the family has not is ignored.
struct Voicing
{
    float hardness = 0;      // [%] all: +100 = brighter (half the contact time; the hybrid voice: 32 velocity steps)
    float hammerMass = 0;    // [%] Tines, Reeds: hammer weight (Pianet: adhesion of the pad)
    float strikePoint = 0;   // [%] Tines, Reeds: contact point along the tine or reed
    float gauge = 0;         // [%] Tines, Reeds: tine diameter or reed thickness (re-tuned)
    float pickupGap = 0;     // [%] Tines, Reeds: field or slot width, wider = softer
    float pickupOffset = 0;  // [x0/width] Tines, Reeds: pickup offset added to the preset ("bark")
    float tonebarDetune = 0; // [cent] Tines: tonebar tuning
    float damperRelease = 0; // [%] Tines, Reeds: decay time with the damper on
    float decay = 0;         // [%] Hybrid: T60 of every mode x 2^(decay/100)
    float unison = 0;        // [%] Hybrid: spread of the modes of one partial
    float stretch = 0;       // [cent/octave] Hybrid: stretch about C4
};

struct Setup
{
    std::string preset;    // the engine's preset id
    Voicing voicing;
    double trimDb = 0.0;   // level on top of the preset
    bool strikeScatter = false; // the engine's own scatter of the hammer speed (off: the plugin's Humanize does it)
};

class SoundEngine
{
public:
    virtual ~SoundEngine() = default;

    // the rate the engine computes at for a host rate: the physical models at their calibration rate (44.1 kHz), the hybrid
    // voices at the host rate (they resample their banks themselves)
    virtual double rateFor (double hostRate) const = 0;
    // prepares the engine for a rate (rateFor); not real-time safe
    virtual void prepare (double rate) = 0;

    // real time
    virtual void noteOn (int note, int velocity) = 0;
    virtual void noteOff (int note) = 0;
    virtual void controller (int cc, int value) = 0;
    virtual void allNotesOff() = 0;
    virtual void process (float* out, int frames) = 0; // mono, overwritten, at the prepared rate

    // the instrument's keys (MIDI notes); outside them the engine stays silent
    virtual int lowestKey() const = 0;
    virtual int highestKey() const = 0;
};

// Builds the engine of a preset with its voicing; the preset is prepared at the engine's calibration rate. nullptr and the
// reason in error when the family has no such preset or a value is refused. Not real-time safe.
std::unique_ptr<SoundEngine> createEngine (Family family, const Setup& setup, std::string* error = nullptr);

// the presets of a family, in the engine's order
int presetCount (Family family);
std::string presetId (Family family, int index);
} // namespace tw::engines
