// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// params.cpp - defaults and name-based access for Params
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>

namespace pmp {
namespace {

// Decimal number ("2.5", "-.25", "3e-4") independent of the C locale: strtof would expect
// "2,5" once a host sets a German LC_NUMERIC. Returns the end of the number or nullptr.
const char* scanNumber(const char* p, float& out) {
    static const double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                    1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                    1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const char* s = p;
    const bool negative = *s == '-';
    if (*s == '+' || *s == '-') ++s;
    uint64_t mantissa = 0;
    int exp10 = 0, digits = 0;
    for (; *s >= '0' && *s <= '9'; ++s, ++digits) {
        if (mantissa < 100000000000000000ull) mantissa = mantissa * 10 + uint64_t(*s - '0');
        else ++exp10;
    }
    if (*s == '.') {
        for (++s; *s >= '0' && *s <= '9'; ++s, ++digits) {
            if (mantissa < 100000000000000000ull) {
                mantissa = mantissa * 10 + uint64_t(*s - '0');
                --exp10;
            }
        }
    }
    if (digits == 0) return nullptr;
    if (*s == 'e' || *s == 'E') {
        const char* e = s + 1;
        const bool negExp = *e == '-';
        if (*e == '+' || *e == '-') ++e;
        if (*e < '0' || *e > '9') return nullptr;
        int x = 0;
        for (; *e >= '0' && *e <= '9'; ++e) x = std::min(x * 10 + (*e - '0'), 400);
        exp10 += negExp ? -x : x;
        s = e;
    }
    const int k = exp10 < 0 ? -exp10 : exp10;
    const double scale = k <= 22 ? kPow10[k] : std::pow(10.0, k);
    const double v = exp10 < 0 ? double(mantissa) / scale : double(mantissa) * scale;
    out = float(negative ? -v : v);
    return std::isfinite(out) ? s : nullptr;       // "1e99" does not fit a float
}

// Shortest decimal (6 to 9 significant digits) that reads back to exactly v, so that a
// dump restores the parameters bit for bit (calibration worker processes).
std::string formatNumber(float v) {
    std::string text;
    for (int digits = 6; digits <= 9; ++digits) {
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s << std::setprecision(digits) << v;
        text = s.str();
        float back = 0.0f;
        const char* end = scanNumber(text.c_str(), back);
        if (end && *end == '\0' && back == v) break;
    }
    return text;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Instrument presets: assignments on top of the base values below. The first preset is the
// default instrument. Every preset plays an embedded hybrid bank; the parameters only shape
// the player (bank, damper, resonance) and the output.
// ---------------------------------------------------------------------------------------
namespace {

struct Preset {
    const char* id;
    const char* name;
    const char* text;
};

const Preset kPresets[] = {
    {"splendid_hybrid", "Splendid Grand Piano (Hybrid-Stimme)", R"(
# preset:splendid_hybrid - the splendid recordings as the hybrid voice of pmpiano plays
# them: a short PCM attack from the bank (fitted from this set by pmpiano's buildbank.py)
# that crossfades into 48 computed modes per key, the decay in the poles instead of in
# samples; the top octave above the highest mode carries a fitted high-band loop. Dampers,
# half pedal and double decay are computed, not faded, and the harp lets the free strings
# ring along (coupling by ear from pmpiano's grand, chosen against Pianoteq 2026-10-20).
# The bank is built at 22050 Hz; the engine carries it to the host rate (resampler).
keys.lo=21
keys.hi=108
hybrid.slowShare=0.03
hybrid.slowT60=2.5
hybrid.rampMs=60
res.on=1
res.gain=5e-6
)"},
    {"clavinet", "Hohner Clavinet D6", R"(
# preset:clavinet - the D6 as the hybrid voice: the PCM attack and the computed mode decay
# of pmpiano's clav bank (Realistic Clavinet, CC BY 3.0); no harp, a short damper (the
# lever), the bank's own loudness law.
keys.lo=28
keys.hi=100
hybrid.bank=1
hybrid.slowShare=0
hybrid.rampMs=0
res.on=0
out.trimDb=-12
)"},
    {"upright", "Upright Piano KW (Kawai)", R"(
# preset:upright - the Kawai upright of FreePats' Upright Piano KW (CC0) as the hybrid
# voice: PCM attack from the bank built from the SF2, then the computed mode decay; the
# grand's damper and a light soundboard resonance.
keys.lo=21
keys.hi=108
hybrid.bank=2
hybrid.slowShare=0.03
hybrid.slowT60=2.5
hybrid.rampMs=60
res.on=1
res.gain=3e-6
)"},
    {"salamander", "Salamander Grand Piano", R"(
# preset:salamander - Alexander Holm's Salamander V3 (Yamaha C5, CC BY 3.0) as the hybrid
# voice: 4 anchors blended from the 16 recorded velocity layers, 44.1 kHz; the grand's
# damper and soundboard resonance.
keys.lo=21
keys.hi=108
hybrid.bank=3
hybrid.slowShare=0.03
hybrid.slowT60=2.5
hybrid.rampMs=60
res.on=1
res.gain=5e-6
out.trimDb=-6
)"},
};

struct ScalarEntry { const char* name; float Params::*ptr; const char* help; };

const ScalarEntry kScalars[] = {
    {"keys.lo",      &Params::keysLo,     "lowest key of the instrument (MIDI)"},
    {"keys.hi",      &Params::keysHi,     "highest key"},
    {"hybrid.bank",      &Params::hybridBank,     "embedded hybrid bank: 0 splendid, 1 clavinet, 2 upright, 3 salamander"},
    {"hybrid.slowShare", &Params::hybridSlowShare, "share of a key's amplitude the damper felt does not reach (0 = off)"},
    {"hybrid.slowT60",   &Params::hybridSlowT60,   "s, decay of that share under the damper"},
    {"hybrid.rampMs",    &Params::hybridRampMs,    "ms, damper felt compression time (0: the bank's)"},
    {"res.on",           &Params::resOn,          "1 = the hybrid voice's resonance bank (strings ring along), 0 = off"},
    {"res.gain",         &Params::resGain,        "coupling of the resonance bank (input weight at the key's f0)"},
    {"res.modes",        &Params::resModes,       "modes per key in the resonance bank"},
    {"res.tiltDb",       &Params::resTiltDb,      "dB per octave of the resonance input weight above f0"},
    {"res.undamped",     &Params::resUndamped,    "1 = keys without a damper ring along without the pedal too"},
    {"out.gain",     &Params::masterGain, "linear"},
    {"out.trimDb",   &Params::trimDb,     "dB level trim of the instrument, apart from out.gain"},
    {"num.maxVoices", &Params::maxVoices, "polyphony limit, 0 = all keys"},
};

bool parseFloat(const std::string& s, float& out) {
    const char* end = scanNumber(s.c_str(), out);
    return end && *end == '\0';
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

} // namespace

Params::Params(std::nullptr_t) {}

Params::Params() : Params(nullptr) {
    std::string err;
    loadPreset(kPresets[0].id, &err);
}

// The public parameters: the output level and the voice limit - what a player sets (host,
// pmp-render --set). The instrument mapping belongs to the presets and the analysis tools:
// the C API and pmp-render --internal set them, a plain set() refuses them.
bool Params::isPublic(const std::string& name) {
    static const char* const kNames[] = {
        "out.gain", "out.trimDb",
        "num.maxVoices",                                                                          // the voice limit
    };
    for (const char* n : kNames)
        if (name == n) return true;
    return false;
}

bool Params::set(const std::string& assignment, std::string* error, bool internalToo) {
    const size_t eq = assignment.find('=');
    if (eq == std::string::npos || eq == 0) return fail(error, "expected name=value");

    std::string lhs = assignment.substr(0, eq);
    const std::string rhs = assignment.substr(eq + 1);
    char op = '=';
    if (lhs.back() == '*' || lhs.back() == '+') {
        op = lhs.back();
        lhs.pop_back();
    }
    if (!internalToo && !isPublic(lhs))
        return fail(error, lhs + " belongs to the instrument mapping and is internal (presets, the C API and pmp-render --internal set it)");

    for (const auto& e : kScalars) {
        if (lhs != e.name) continue;
        float v;
        if (!parseFloat(rhs, v)) return fail(error, "bad value for " + lhs);
        float& p = this->*e.ptr;
        p = (op == '*') ? p * v : (op == '+') ? p + v : v;
        return true;
    }
    return fail(error, "unknown parameter " + lhs);
}

bool Params::setText(const std::string& text, std::string* error, bool internalToo) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        const size_t b = line.find_last_not_of(" \t\r");
        if (!set(line.substr(a, b - a + 1), error, internalToo)) return false;
    }
    return true;
}

std::string Params::dump(bool internalToo) const {
    std::ostringstream s;
    for (const auto& e : kScalars)
        if (internalToo || isPublic(e.name)) s << e.name << "=" << formatNumber(this->*e.ptr) << "    # " << e.help << "\n";
    return s.str();
}

int Params::presetCount() { return int(sizeof(kPresets) / sizeof(kPresets[0])); }

const char* Params::presetId(int index) {
    return index >= 0 && index < presetCount() ? kPresets[index].id : "";
}

const char* Params::presetName(int index) {
    return index >= 0 && index < presetCount() ? kPresets[index].name : "";
}

int Params::presetIndex(const std::string& id) {
    for (int i = 0; i < presetCount(); ++i)
        if (id == kPresets[i].id) return i;
    return -1;
}

namespace {

const Preset* findPreset(const std::string& id) {
    for (const Preset& p : kPresets)
        if (id == p.id) return &p;
    return nullptr;
}

} // namespace

bool Params::hasPreset(const std::string& id) { return findPreset(id) != nullptr; }

bool Params::loadPreset(const std::string& id, std::string* error) {
    const Preset* p = findPreset(id);
    if (!p) return fail(error, "unknown instrument " + id);
    Params fresh(nullptr);
    if (!fresh.setText(p->text, error)) return false;
    *this = fresh;
    return true;
}

} // namespace pmp
