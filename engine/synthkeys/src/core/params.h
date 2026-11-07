// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// params.h - player and preset parameters of the hybrid instruments
//
// Every preset plays an embedded hybrid bank: a short PCM attack crossfades into computed
// modes, so the parameters shape the bank player (bank, damper ramp, resonance) and the
// output level. Values can be changed by name ("out.gain=0.8", ...), which is used by the
// render tool, the host and the analysis scripts.
#pragma once

#include <cstddef>
#include <string>

namespace pmp {

struct Params {
    // --- range -----------------------------------------------------------------------------
    float keysLo = 21.0f;              // lowest key of the instrument (MIDI); keys outside stay silent
    float keysHi = 108.0f;             // highest key

    // --- the voice of the preset ----------------------------------------------------------------
    float hybridBank = 0.0f;            // embedded bank: 0 splendid, 1 clavinet, 2 upright, 3 salamander
    float hybridSlowShare = 0.0f;       // share of a key's amplitude the damper felt does not reach (0 = off)
    float hybridSlowT60   = 2.5f;       // [s] decay of that share under the damper
    float hybridRampMs    = 60.0f;      // [ms] damper felt compression time (0: the bank's)
    // the resonance bank (the "harp"): the strings that ring along under the pedal and with
    // held keys; pmpiano's docs/PIANO_HYBRID_RESONANCE.md, the coupling chosen by ear there
    float resOn       = 0.0f;           // 1 = the strings ring along, 0 = off
    float resGain     = 0.0f;           // coupling: input weight of a mode at the key's f0
    float resModes    = 16.0f;          // modes per key in the harp
    float resTiltDb   = 0.0f;           // [dB per octave] of the resonance input weight above f0
    float resUndamped = 1.0f;           // 1 = keys without a damper ring along without the pedal too

    // --- output ----------------------------------------------------------------------------
    float masterGain = 1.0f;
    float trimDb     = 0.0f;           // [dB] level of the instrument against the others, apart from out.gain

    // --- numerics ---------------------------------------------------------------------------
    float maxVoices    = 0.0f;         // polyphony limit, 0 = all keys (embedded: CPU budget)

    Params();                          // base values plus the default instrument (the first preset, "splendid_hybrid")

    // Scope: a public parameter is what a player sets (host, pmp-render --set) - the output
    // level and the voice limit. The mapping of the instrument belongs to the presets and
    // the analysis tools and is internal: the C API and pmp-render --internal still set
    // them, a plain set() refuses them.
    static bool isPublic(const std::string& name);
    // "name=value", "name*=f", "name+=d"
    bool set(const std::string& assignment, std::string* error = nullptr, bool internalToo = true);
    // several assignments, one per line; '#' starts a comment
    bool setText(const std::string& text, std::string* error = nullptr, bool internalToo = true);
    std::string dump(bool internalToo = true) const;   // one per line; internalToo = false: the public parameters only

    // Instrument presets: defaults plus a parameter set per instrument.
    static int presetCount();
    static const char* presetId(int index);      // "splendid_hybrid", "clavinet", "upright", "salamander"
    static const char* presetName(int index);    // "Splendid Grand Piano" ...
    static int presetIndex(const std::string& id);   // -1 if unknown
    static bool hasPreset(const std::string& id);
    bool loadPreset(const std::string& id, std::string* error = nullptr);   // the base values plus the preset's text

private:
    explicit Params(std::nullptr_t);   // the base values alone, shared by every preset
};

} // namespace pmp
