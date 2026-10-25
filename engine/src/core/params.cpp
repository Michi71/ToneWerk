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
// Curve
// ---------------------------------------------------------------------------------------
Curve::Curve(std::initializer_list<std::pair<float, float>> points) {
    for (const auto& p : points) {
        if (n >= kMaxPoints) break;
        key[n] = p.first;
        val[n] = p.second;
        ++n;
    }
}

float Curve::operator()(float note) const {
    if (n <= 0) return 0.0f;
    if (note <= key[0]) return val[0];
    for (int i = 1; i < n; ++i) {
        if (note <= key[i]) {
            const float t = (note - key[i - 1]) / (key[i] - key[i - 1]);
            return val[i - 1] + t * (val[i] - val[i - 1]);
        }
    }
    return val[n - 1];
}

std::string Curve::toString() const {
    std::string s;
    for (int i = 0; i < n; ++i) s += (i ? "," : "") + formatNumber(key[i]) + ":" + formatNumber(val[i]);
    return s;
}

bool Curve::parse(const std::string& text) {
    Curve c;
    const char* p = text.c_str();
    if (!*p) {                                   // "name=" clears the curve
        *this = c;
        return true;
    }
    while (*p) {
        float k, v;
        const char* end = scanNumber(p, k);
        if (!end || *end != ':') return false;
        p = end + 1;
        end = scanNumber(p, v);
        if (!end || c.n >= kMaxPoints) return false;
        if (c.n > 0 && k <= c.key[c.n - 1]) return false;   // keys must increase
        c.key[c.n] = k;
        c.val[c.n] = v;
        ++c.n;
        p = end;
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    *this = c;                                   // empty text: no points (curve unused)
    return true;
}

// ---------------------------------------------------------------------------------------
// Instrument presets: assignments on top of the base values below, calibrated with
// tools/analysis/calibrate.py against samples/<instrument>. The first preset is the default
// instrument: Params() applies it, so every other preset starts from a complete Mark I.
// ---------------------------------------------------------------------------------------
namespace {

struct Preset {
    const char* id;
    const char* name;
    const char* text;
    const char* base = nullptr;   // a preset this one derives from (the suitcase: the mark2 core plus its chain)
    const char* own = nullptr;    // appended only when the preset itself is loaded, not when its text serves as the engine's
                                  // defaults (the mark1 text is the base of every preset: its hybrid voice stays out of the base)
};

const Preset kPresets[] = {
    {"mark1", "Rhodes Mark I (Stage)", R"(
# preset:mark1 - calibrated with tools/analysis/calibrate.py against samples/rhodes_mark_1
decay.t60=28:30.3,29:32.3,31:37.6,33:39.8,35:29.5,36:34.1,38:31.9,40:27.1,41:24.6,43:24.9,45:21,47:23.4,48:25.9,50:22.6,52:20.9,53:22,55:21.6,57:19.5,59:20.9,60:19.7,62:15.8,64:14.8,65:14.3,67:15,69:11.8,71:13.5,72:10.8,74:9.76,76:4.93,77:2.74,79:3.06,81:9.45,83:7.69,84:7.18,86:8.5,88:5.05,89:4.37,91:6.68,93:5.09,95:2.71,96:2.16,98:2.6,100:0.911
pickup.width=28:0.3531,29:0.3896,31:0.2865,33:0.3805,35:0.3713,36:0.4771,38:0.4557,40:0.4276,41:0.4174,43:0.4399,45:0.4574,47:0.613,48:0.7091,50:0.5256,52:0.6069,53:0.633,55:0.6218,57:0.7994,59:0.6444,60:0.8881,62:0.9468,64:1.203,65:0.8395,67:1.119,69:0.9734,71:1.101,72:1.058,74:1.356,76:1.176,77:1.156,79:1.831,81:3.544,83:5.566,84:2.431,86:5.687,88:5.237,89:4.472,91:5.019,93:6,95:4.723,96:2.383,98:6,100:3.475
pickup.offset=28:-0.5758,29:-0.4122,31:-1.042,33:-1.202,35:-0.7952,36:-0.6505,38:-0.9022,40:-0.4444,41:-0.6766,43:-0.6091,45:-0.5195,47:-0.6423,48:-0.5324,50:-0.5346,52:-0.2998,53:-0.3627,55:-0.392,57:-0.4526,59:-0.4319,60:-0.4492,62:-0.4868,64:-0.2683,65:-0.4049,67:-0.2746,69:-0.4121,71:-0.407,72:-0.4287,74:-0.4413,76:-0.35,77:-0.3687,79:-0.4838,81:-0.5516,83:-0.3241,84:-0.3751,86:-0.467,88:-0.3604,89:-0.6843,91:-0.6664,93:-0.6429,95:-0.6889,96:-0.6583,98:-0.6366,100:-0.414
vel.gamma=1.424
human.speed=0.05
human.contact=0.1
rel1.gain=0.006
rel2.gain=0.01
rel1.delayMs=0
rel2.delayMs=0
rel1.gainByKey=71:6.95,82:0,95:-7.32
human.release=0.3
pedal.downGain=0.0588
pedal.upGain=0.0609
human.pedal=0.25
keys.tuneCents=0
keys.levelDb=0.6
keys.seed=50
keys.tuneByKey=28:2.40,29:-0.39,30:-0.67,31:-3.64,32:-0.01,33:-0.44,34:-0.74,35:0.43,36:0.58,37:-1.46,38:-1.22,39:-0.65,40:2.53,41:1.44,42:0.95,43:-0.61,44:0.77,45:-0.57,46:-1.79,47:0.20,48:-1.85,49:-1.54,50:-0.90,51:1.01,52:-0.57,53:0.28,54:2.16,55:-0.86,56:-0.04,57:-1.50,58:-0.94,59:-0.69,60:-1.45,61:1.50,62:-2.08,63:-0.42,64:-1.67,65:0.68,66:1.00,67:0.52,68:2.13,69:-0.38,70:0.86,71:0.84,72:-0.17,73:0.10,74:-0.57,75:1.01,76:-0.10,77:-0.09,78:-0.88,79:0.77,80:-0.24,81:0.22,82:-0.79,83:0.18,84:0.88,85:-0.28,86:0.05,87:-0.67,88:0.67,89:-0.58,90:-0.26,91:1.23,92:-0.52,93:3.23,94:0.12,95:-2.57,96:-0.56,97:0.09,98:4.05,99:0.41,100:1.93
# the effect section of the MK8 (MK8_COMBINED_FX.md), carried by every instrument and OFF: fx.on=1 (MIDI CC/NRPN 10) switches it on
fx.on=0
fx.drive=25
fx.bass=3
fx.mid=-3
fx.midFreq=500
fx.treble=3
fx.panOn=1
fx.panWave=3
fx.panRate=1
fx.panDepth=50
fx.compOn=1
fx.compAmount=40
fx.compMakeup=4
fx.chorusOn=0
fx.chorusRate=2
fx.chorusDepth=1
fx.phaserOn=0
fx.phaserRate=1
fx.phaserDepth=40
fx.delayOn=1
fx.delayTime=0.25
fx.delayFeedback=25
fx.delayMix=25
fx.delayPingPong=1
# the amplifier section of a stage piano (docs/MK8_AMP.md): tone controls, a power amp with a little tube-like
# saturation and a guitar-style cabinet, designed values (no data); OFF, amp.on=1 (MIDI CC/NRPN 11) brings it in
amp.on=0
tone.bass=1
tone.treble=0
amp.drive=6
amp.asym=0.15
spk.lowHz=70
spk.highHz=6500
)", nullptr, R"(
# hybrid voice (docs/SAMPLELIB_BANKS.md): with banks/mark1_hybrid.bank loaded (bank version 5, 44.1 kHz, from Matt's Fender
# Rhodes of the free libraries, second bank slot) the Mark I plays the hybrid voice; without the bank, or at another host rate
# than the bank's, the physical model above. The recordings carry their dynamics: the bank plays their power law, its level is
# fitted to the physical model (rhodes_hybrid_level.py --instrument lib_mark1 --fit). These lines belong to the preset only,
# not to the engine's defaults the other presets build on.
hybrid.on=1
hybrid.bank=1
)"},
    {"mark2", "Rhodes Mark II (Stage)", R"(
# preset:mark2 - calibrated with tools/analysis/calibrate.py against samples/rhodes_mark_2
# hybrid voice (docs/SAMPLELIB_BANKS.md): with banks/mark2_hybrid.bank loaded (bank version 5, 44.1 kHz, from mRhodes of the
# free libraries, third bank slot) the Mark II plays the hybrid voice; without the bank, or at another host rate than the
# bank's, the physical model below. The recordings carry their dynamics: the bank plays their power law, its level is fitted
# to the physical model (rhodes_hybrid_level.py --instrument lib_mark2 --fit).
hybrid.on=1
hybrid.bank=2
decay.t60=38:20.1,41:30.7,43:30.4,45:30,47:31.7,48:32.9,50:27.6,52:24.6,53:31.8,55:25.4,57:22.2,59:23.3,60:15.3,62:20.4,64:13.6,65:12.4,67:18.1,69:18,71:16.9,72:10.3,74:12.4,76:7.27,77:5.18,79:8.93,81:15.2,83:8.62,84:6.83,86:4.4,88:3.38,89:3.5,91:4.02,93:2.83,95:5.45,96:3.92
pickup.width=31:0.259,33:0.3297,35:0.6434,36:0.5036,38:0.322,40:0.4794,41:0.3663,43:0.4262,45:0.6375,47:0.5089,48:0.512,50:0.5492,52:0.6125,53:0.6115,55:0.6025,57:0.7886,59:0.9385,60:0.6383,62:1.049,64:0.5988,65:0.6019,67:1.05,69:1.695,71:1.414,72:1.389,74:1.772,76:1.833,77:1.505,79:1.881,81:2.464,83:3.004,84:6,86:5.485,88:6,89:5.488,91:6,93:6,95:6,96:6
pickup.offset=31:-0.1016,33:-0.2263,35:-0.2213,36:-0.1123,38:-0.5923,40:-0.2231,41:-0.6412,43:-0.529,45:-0.3799,47:-0.5904,48:-0.4125,50:-0.2042,52:-0.4574,53:-0.4162,55:-0.5458,57:-0.1607,59:-0.3118,60:-0.3617,62:-0.1385,64:-0.3993,65:-0.3713,67:-0.1315,69:-0.2561,71:-0.4534,72:-0.4669,74:-0.4538,76:-0.6298,77:-0.6456,79:-0.374,81:-0.1089,83:-0.2967,84:-0.4515,86:-0.5717,88:-0.5542,89:-0.5167,91:-0.5047,93:-0.4938,95:-0.5094,96:-0.5477
vel.gamma=0.9832
decay.release=31:1.565,33:1.357,35:0.566,36:0.2618,38:0.479,40:0.2494,41:0.3861,43:0.2555,45:0.3399,47:0.1511,48:0.3112,50:0.3267,52:0.2434,53:0.242,55:0.3656,57:0.5977,59:0.479,60:0.5106,62:0.6371,64:0.7063,65:0.4016,67:0.7759,69:0.4746,72:0.8979,76:2.82,81:1.419
damper.harpT60=28:3.2,100:3.2
damper.velDb=0:-3.57,16:-2.64,36:-1.38,61:0.39,81:1.22,98:1.69,110:2.46,120:2.71,127:2.45
tonebar.split=1.18
tonebar.detune=5.99
tonebar.loss=0.559
human.speed=0.05
human.contact=0.1
rel1.gain=0.006
rel2.gain=0.01
rel1.delayMs=0
rel2.delayMs=0
rel1.gainByKey=71:6.95,82:0,95:-7.32
human.release=0.3
pedal.downGain=0.0588
pedal.upGain=0.0609
human.pedal=0.25
keys.tuneCents=0
keys.levelDb=0.6
keys.seed=65
keys.tuneByKey=28:0.76,29:-1.10,30:0.39,31:-2.74,32:1.21,33:-2.47,34:0.01,35:-0.19,36:-1.30,37:4.62,38:-0.96,39:0.76,40:0.89,41:-1.76,42:1.76,43:-3.49,44:-1.96,45:0.56,46:2.16,47:1.28,48:-3.25,49:-0.20,50:0.27,51:1.63,52:0.17,53:-0.03,54:-1.47,55:-0.10,56:-1.66,57:-0.08,58:1.95,59:1.34,60:0.04,61:0.81,62:-0.13,63:0.22,64:-0.44,65:-2.00,66:1.03,67:0.60,68:0.85,69:-0.22,70:-1.86,71:-2.38,72:-0.85,73:-3.56,74:-0.73,75:-2.71,76:-0.05,77:-0.20,78:-2.88,79:-1.47,80:-3.57,81:3.89,82:-1.04,83:-0.52,84:-0.46,85:-0.85,86:-0.15,87:-0.06,88:0.38,89:1.71,90:0.35,91:0.40,92:-0.62,93:4.51,94:-0.41,95:0.11,96:2.15,97:0.12,98:-1.60,99:-1.15,100:0.06
# the effect section of the MK8 (MK8_COMBINED_FX.md), carried by every instrument and OFF: fx.on=1 (MIDI CC/NRPN 10) switches it on
fx.on=0
fx.drive=25
fx.bass=3
fx.mid=-3
fx.midFreq=500
fx.treble=3
fx.panOn=1
fx.panWave=3
fx.panRate=1
fx.panDepth=50
fx.compOn=1
fx.compAmount=40
fx.compMakeup=4
fx.chorusOn=0
fx.chorusRate=2
fx.chorusDepth=1
fx.phaserOn=0
fx.phaserRate=1
fx.phaserDepth=40
fx.delayOn=1
fx.delayTime=0.25
fx.delayFeedback=25
fx.delayMix=25
fx.delayPingPong=1
# the amplifier section of a stage piano (docs/MK8_AMP.md): tone controls, a power amp with a little tube-like
# saturation and a guitar-style cabinet, designed values (no data); OFF, amp.on=1 (MIDI CC/NRPN 11) brings it in
amp.on=0
tone.bass=1
tone.treble=0
amp.drive=6
amp.asym=0.15
spk.lowHz=70
spk.highHz=6500
)"},
    {"suitcase", "Rhodes Suitcase", R"(
# preset:suitcase - not a second model: the mark2 core through the suitcase signal
# chain. The reference samples proved to be looped sampler material of unknown
# origin (SUITCASE_MERGE.md): their 2026-09 calibration (own T60/pickup curves,
# vel.gamma 1.33, treble +7.6 dB) fitted loop artifacts and is retired. The chain
# carries designed values (PLAN P3) until real suitcase recordings exist; the
# parameters below override the base preset, everything else comes from mark2.
# hybrid voice (docs/RHODES_HYBRID.md): the Mark II bank (hybrid.bank=2, inherited) through this chain, which stays in the
# path on the hybrid voice (hybrid.amp=1; the Wurlitzer bypasses its chain there, the recordings carry it)
hybrid.amp=1
# tone controls (Kuhschwanz): gentle
tone.bass=0.5
tone.treble=2
# power amp: a touch of saturation
amp.on=1
amp.drive=2
amp.asym=0
# speaker cabinet: band limitation of the built-in fullrange
spk.lowHz=60
spk.highHz=7500
# the recorded stereo width is a constant +/-7 cent detune (K8), kept subtle
detune.cents=7
detune.mix=0.44
# the Peterson vibrato (amplitude panning, no pitch shift) is switchable like on
# the instrument: vib.rate around 4.5 Hz, vib.depth 0..1 - off by default
)", "mark2"},
    {"mk8", "Rhodes MK8", R"(
keys.tuneByKey=
# preset:mk8 - calibrated with tools/analysis/calibrate.py against samples/rhodes_mk8
# 2026-10-06: rolled back to this plugin calibration - the real-MK8 anchoring of 2026-10-01
# (seven steps, docs/MK8_*.md) is reverted by hearing verdict: the samples are the reference
# again (compare: structure 6.1 dB against 12.6 anchored, T60 on the sample curve)
decay.t60=28:49.6,29:47.4,30:45.7,31:43.8,32:41.6,33:39.7,34:37.9,35:36,36:35.9,37:32.3,38:32.2,39:31.5,40:32,41:26.7,42:29,43:26.1,44:28.6,45:21.9,46:24.6,47:24.7,48:22.1,49:19.3,50:20.3,51:21.7,52:15.3,53:14.9,54:17.9,55:16,56:16.5,57:13.2,58:14.5,59:10.6,60:18.8,61:16.6,62:15.3,63:18.1,64:15.6,65:16.6,66:15,67:9.81,68:10,69:8.85,70:8.15,71:7.14,72:7.93,73:7.57,74:5.84,75:5.52,76:5.6,77:5.39,78:5.13,79:5.28,80:4.64,81:4.61,82:4.12,83:4.14,84:3.88,85:3.54,86:3.54,87:3.6,88:3.55,89:3.88,90:3.94,91:3.82,92:3.58,93:3.41,94:3.86,95:3.49,96:3.04,97:3.05,98:4.06,99:2.69,100:2.49
pickup.width=28:0.6376,29:0.6302,30:0.6626,31:0.6497,32:0.578,33:0.6376,34:1.185,35:1.142,36:0.4768,37:0.3576,38:0.2668,39:0.2928,40:0.4568,41:0.4057,42:0.7716,43:0.5339,44:0.4275,45:0.3822,46:0.4315,47:0.5199,48:0.3863,49:0.345,50:0.4171,51:0.4981,52:0.3206,53:0.3517,54:0.4778,55:0.3327,56:0.4356,57:0.4196,58:0.4289,59:0.4475,60:0.4371,61:0.4499,62:0.5207,63:0.4209,64:0.4541,65:0.4932,66:0.471,67:0.5357,68:0.4458,69:0.4188,70:0.6269,71:0.5383,72:0.6055,73:0.6711,74:0.7785,75:0.5614,76:0.6968,77:0.7478,78:0.7116,79:0.6981,80:0.5417,81:0.6217,82:0.4657,83:0.6708,84:0.64,85:0.6476,86:0.7792,87:0.8762,88:5.263,89:1.99,90:2.1,91:1.434,92:1.337,93:1.151,94:1.411,95:1.211,96:1.18,97:1.543,98:1.327,99:1.276,100:1.139
pickup.offset=28:-0.6798,29:-0.6781,30:-0.666,31:-0.6744,32:-0.7014,33:-0.6672,34:-0.2593,35:-0.2595,36:-1.051,37:-1.12,38:-1.212,39:-1.062,40:-1.033,41:-0.7447,42:-0.3418,43:-0.6906,44:-1.011,45:-0.5892,46:-0.6127,47:-0.6788,48:-0.6386,49:-0.5535,50:-0.6107,51:-0.7311,52:-0.5886,53:-0.5833,54:-0.6608,55:-0.5578,56:-0.4532,57:-0.333,58:-0.3789,59:-0.3608,60:-0.4163,61:-0.3641,62:-0.3478,63:-0.3649,64:-0.35,65:-0.379,66:-0.4188,67:-0.3303,68:-0.4095,69:-0.4163,70:-0.2898,71:-0.4486,72:-0.4757,73:-0.3121,74:-0.4641,75:-0.5289,76:-0.4905,77:-0.4701,78:-0.4901,79:-0.4191,80:-0.4123,81:-0.3077,82:-0.5371,83:-0.4959,84:-0.4238,85:-0.3952,86:-0.4959,87:-0.4882,88:-0.3328,89:-0.7709,90:-0.7074,91:-0.6901,92:-0.679,93:-0.7204,94:-0.6994,95:-0.6813,96:-0.6781,97:-0.6392,98:-0.6682,99:-0.6338,100:-0.6452
vel.gamma=1.582
decay.release=28:0.201,51:0.517,95:0.408
tonebar.split=0.5
tonebar.detune=9.4
tonebar.loss=0.797
mech1.hz=150.3
mech1.t60=0.5744
mech1.gain=1483
mech2.hz=52.17
mech2.t60=0.7
mech2.gain=432.6
mech3.hz=591.2
mech3.t60=0.01069
mech3.gain=427.3
hammer.speedHardening=25
tine.lossHf2=1.6e-5
vel.gainDb=0:-1.55,16:-1.55,36:-8.15,61:-8.55,81:-7.20,98:-5.82,110:-4.24,120:-1.58,127:0.00
# the manufacturer's effect chain (MK8_COMBINED_FX.md): fitted as a combination
# against the wet plugin renders (3.29 dB test set / 2.16 dB holdout, no single
# parameter re-tuned), then voiced by two listening rounds of 2026-09-28: the
# modulation effects stay OFF by default (see below) and are switched on per
# demand. fx.on=0 renders the dry instrument, which all analyses use.
vel.gainSlope=28:-0.0376,29:-0.0381,30:-0.0390,31:-0.0419,32:-0.0386,33:-0.0446,34:-0.0756,35:-0.0757,36:-0.0629,37:-0.0566,38:-0.0542,39:-0.0399,40:-0.0507,41:-0.0214,42:-0.0351,43:-0.0185,44:-0.0346,45:-0.0028,46:-0.0091,47:-0.0085,48:-0.0002,49:0.0061,50:0.0013,51:-0.0040,52:0.0090,53:0.0065,54:0.0011,55:0.0095,56:0.0028,57:-0.0045,58:-0.0002,59:0.0288,60:0.0016,61:-0.0024,62:-0.0064,63:-0.0032,64:-0.0036,65:-0.0021,66:0.0015,67:-0.0089,68:-0.0009,69:-0.0006,70:-0.0153,71:0.0024,72:0.0106,73:0.0008,74:0.0265,75:0.0288,76:0.0169,77:0.0111,78:0.0071,79:0.0007,80:0.0140,81:0.0079,82:0.0134,83:0.0109,84:0.0236,85:0.0213,86:0.0171,87:0.0283,88:0.0331,89:0.0156,90:0.0361,91:0.0042,92:0.0315,93:0.0364,94:0.0240,95:0.0394,96:0.0272,97:0.0443,98:0.0577,99:0.0772,100:0.0827
# 2026-10-06: dry by default - the player switches the chain on (hearing verdict;
# every analysis had already rendered fx.on=0)
fx.on=0
fx.drive=25
fx.bass=3
fx.mid=-3
fx.midFreq=500
fx.treble=3
fx.panOn=1
fx.panWave=3
fx.panRate=1
fx.panDepth=50
fx.compOn=1
fx.compAmount=40
fx.compMakeup=4
# modulation off by the listening verdicts of 2026-09-28: the plugin's chorus
# depth detunes its copy by up to +/-54 cent (2.5 ms), and a swept-allpass
# phaser at 1 Hz/40 % pulls the pitch of sustained low-mid notes by up to
# 40 cent - the plugin's own stages do the same (measured), but the smearing
# was rejected by ear. The chorus depth below is the value accepted for
# on-demand use; fx.chorusOn=1 / fx.phaserOn=1 switch the modulation on.
fx.chorusOn=0
fx.chorusRate=2
fx.chorusDepth=1
fx.phaserOn=0
fx.phaserRate=1
fx.phaserDepth=40
fx.delayOn=1
fx.delayTime=0.25
fx.delayFeedback=25
fx.delayMix=25
fx.delayPingPong=1
out.trimDb=6
# no hybrid voice: the MK8 bank from the plugin renders is gone (2026-10-25, no licence for them; docs/SAMPLELIB_IMPORT.md),
# the MK8 plays its physical model
)"},
    {"wurli200a", "Wurlitzer 200A", R"(
# preset:wurli200a - calibrated with tools/analysis/calibrate.py against samples/wurlitzer_real
# hybrid voice (docs/WURLITZER_HYBRID.md, docs/SAMPLELIB_BANKS.md): with banks/wurli_hybrid.bank loaded (bank version 5,
# 44.1 kHz, from the WörliTzer of the free libraries, fourth bank slot) the PCM attack + mode bank plays; the amp stage below
# (tone, power amp, speaker) is then bypassed, the recordings carry it. The recordings carry their dynamics: the bank's law
# alone, not vel.gainDb (the physical model's). Without the bank, or at another host rate than the bank's, the physical model
# below plays.
hybrid.on=1
hybrid.bank=3
hybrid.velGain=0
pickup.type=4
pickup.hpfHz=880
pickup.onsetMul=33:5,40:4,46:2.5,50:1.8,54:0.6,58:0.15,62:0
pickup.onsetMs=18
pickup.width=33:0.025755,34:0.02579,35:0.025755,36:0.025733,37:0.025711,38:0.025711,39:0.025663,40:0.025263,41:0.025263,42:0.025263,43:0.025032,44:0.024784,45:0.024784,46:0.024784,47:0.020311,48:0.020311,49:0.020311,50:0.020311,51:0.020311,52:0.02057,53:0.04445,54:0.04783,55:0.04783,56:0.04783,57:0.04783,58:0.04783,59:0.04458,60:0.04387,61:0.04387,62:0.04294,63:0.04248,64:0.04217,65:0.04217,66:0.058223,67:0.058082,68:0.057403,69:0.056851,70:0.056851,71:0.0804,72:0.08214,73:0.08214,74:0.10185,75:0.10185,76:0.10264,77:0.1502,78:0.1526,79:0.1526,80:0.21581,81:0.21581,82:0.23538,83:0.17516,84:0.17516,85:0.17434,86:0.1662,87:0.17112,88:0.17112,89:0.121,90:0.12386,91:0.12386,92:0.14516,93:0.16789,94:0.16473,95:0.16393,96:0.16314
pickup.offset=33:-0.7,34:-0.7,35:-0.7,36:-0.5,37:-0.5,38:-0.5,39:-0.5,40:-0.5,41:-0.5,42:-0.5,43:-0.7,44:-0.7,45:-0.7,46:-0.7,47:-0.3,48:-0.3,49:-0.3,50:-0.3,51:-0.3,52:-0.3,53:-0.3,54:-0.3,55:-0.3,56:-0.15,57:-0.15,58:-0.15,59:-0.15,60:-0.15,61:0.2,62:0.2,63:0.2,64:0.2,65:0.2,66:0.2,67:0.2,68:0.05,69:0.05,70:0.05,71:0.1,72:0.1,73:0.1,74:0.1,75:0.05,76:0.05,77:0.1,78:0.1,79:0.1,80:0.1,81:0.1,82:0,83:0,84:0,85:0,86:0.05,87:0.05,88:0.05,89:-0.15,90:-0.15,91:-0.15,92:-0.15,93:0.1,94:0.1,95:0.1,96:0.1
tonebar.split=0
tine.lossHf2=4e-06
tine.springMu=0.3
tine.springPos=0.99
tine.springMuByKey=33:0.8,60:0.3,84:0.05,100:0
vel.gamma=0.9078
pedal.downGain=0
pedal.upGain=0
keys.tuneByKey=
keys.tuneCents=1.5
keys.levelDb=0.6
keys.seed=200
amp.on=1
amp.perVoice=1
# the 200A's tremolo: the schematic's RC phase-shift oscillator (TR-3/TR-4) drives a lamp whose
# LDR shunts the signal - amplitude only. trem.depth 0 = off - the references carry no tremolo,
# our recordings' 2.02 Hz sidebands are an artifact of the shared recording lineage (docs 3.11)
trem.rate=5.6
trem.depth=0
tone.bass=17
tone.treble=-6
amp.drive=0
amp.asym=0.0023
spk.lowHz=25
spk.highHz=6000
decay.t60=33:21.7,34:22.9,35:21.1,36:38,37:38,38:33.8,39:29.8,40:28.2,41:26.7,42:25.6,43:14.7,44:14.4,45:14.1,46:13.9,47:9.62,48:9.35,49:9.28,50:8.19,51:7.94,52:8.34,53:5.88,54:5.91,55:5.89,56:7.19,57:6.86,58:6.81,59:6.62,60:6.52,61:7.26,62:6.92,63:6.14,64:6.22,65:6.12,66:4.72,67:4.73,68:4.66,69:4.59,70:4.69,71:3.62,72:3.62,73:3.61,74:3.49,75:3.47,76:3.46,77:2.69,78:2.76,79:2.75,80:2.76,81:2.77,82:2.73,83:2.7,84:2.69,85:2.7,86:2.05,87:2.06,88:2.07,89:1.56,90:1.57,91:1.54,92:1.99,93:1.73,94:1.76,95:1.73,96:1.74
spk.highQ=0.7
vel.gainDb=0:2.15,64:2.15,127:0.00
vel.gainSlope=33:0.0653,34:0.0666,35:0.0721,36:0.0599,37:0.0583,38:0.0611,39:0.0711,40:0.0781,41:0.0786,42:0.0769,43:0.0739,44:0.0750,45:0.0739,46:0.0718,47:0.1137,48:0.1131,49:0.1099,50:0.0957,51:0.0929,52:0.0896,53:-0.0140,54:-0.0156,55:-0.0094,56:0.0105,57:0.0049,58:0.0055,59:0.0362,60:0.0279,61:-0.0427,62:-0.0414,63:-0.0159,64:-0.0192,65:-0.0241,66:-0.0406,67:-0.0402,68:-0.0090,69:-0.0053,70:-0.0049,71:-0.0428,72:-0.0419,73:-0.0332,74:0.0107,75:0.0203,76:0.0219,77:0.0011,78:-0.0003,79:0.0003,80:-0.0012,81:0.0100,82:0.0213,83:-0.0056,84:-0.0213,85:-0.0048,86:-0.0380,87:-0.0356,88:-0.0367,89:-0.0398,90:-0.0428,91:-0.0382,92:-0.0144,93:-0.0907,94:-0.0897,95:-0.0931,96:-0.0921
out.keyGainDb=33:-3.68,34:-3.04,35:-3.47,36:-4.50,37:-5.22,38:-3.96,39:-2.59,40:-1.83,41:-1.17,42:-0.35,43:1.45,44:2.16,45:2.50,46:2.99,47:1.21,48:1.65,49:1.63,50:1.66,51:1.62,52:1.47,53:-3.07,54:-2.97,55:-2.52,56:-2.43,57:-2.27,58:-1.77,59:0.06,60:0.17,61:-1.24,62:-1.24,63:-0.80,64:-0.89,65:-1.30,66:-1.62,67:-1.99,68:-1.74,69:-1.56,70:-1.46,71:-1.90,72:-1.72,73:-1.14,74:0.74,75:1.10,76:1.22,77:0.50,78:0.41,79:0.16,80:0.15,81:-0.02,82:0.65,83:-0.71,84:-1.54,85:-2.16,86:-4.13,87:-4.82,88:-4.78,89:-3.69,90:-3.53,91:-4.03,92:-5.06,93:-8.27,94:-9.09,95:-9.08,96:-9.02
mech1.hz=332.69
mech1.t60=0.106123
mech1.gain=0.69964
mech2.hz=1189.69
mech2.t60=0.00315455
mech2.gain=17107.3
click.gain=49.3637
click.hpHz=830
click.lpHz=5100
mech.gainByKey=33:-22.5,34:-39.0,35:-25.5,36:-24.0,37:-51.0,38:-51.0,39:-39.0,40:-33.0,41:-24.0,42:-6.0,43:-6.0,44:-6.0,45:-6.0,46:-6.0,47:-22.0,48:-5.0,49:-9.0,50:-13.0,51:-14.0,52:-18.0,53:-18.0,54:-18.0,55:-18.0,56:-18.0,57:-18.0,58:-18.0,59:-18.0,60:-18.0,61:-66.0,62:-66.0,63:-21.0,64:-27.0,65:-18.0,66:-18.0,67:-17.2,68:-19.5,69:-18.8,70:-18.0,71:-17.2,72:-16.5,73:-21.8,74:-30.0,75:-30.0,76:-24.0,77:-24.0,78:-21.0,79:-27.0,80:-24.0,81:-24.0,82:-24.0,83:-18.0,84:-30.0,85:-30.0,86:-30.0,87:-30.0,88:-30.0,89:-27.0,90:-27.0,91:-27.0,92:-27.0,93:-24.0,94:-24.0,95:-24.0,96:-24.0
hammer.speedHardening=200
hammer.mass=28:0.000600,64:0.000400,100:0.000250
hammer.contact=28:12.135,34:8.581,40:6.067,46:3.861,52:2.427,58:1.716,64:1.213,70:0.858,76:0.516,82:0.365,88:0.212,94:0.150,100:0.106
out.trimDb=0.05
mech.levelled=0
click.expByKey=33:1.00,39:1.00,45:1.50,51:1.00,57:2.00,63:1.50,69:1.50,75:1.00,81:1.00,87:1.00,93:3.00,96:2.00
mech.expByKey=33:1.068,59:2.768,90:2.825
rel1.hz=106
rel1.t60=0.45
rel1.gain=0.01
rel1.delayMs=0
rel2.hz=327
rel2.t60=0.43
rel2.gain=0.02
rel2.delayMs=0
human.release=0.3
decay.release=33:0.318,35:0.299,36:0.258,38:0.231,40:0.204,41:0.204,43:0.193,45:0.193,47:0.181,48:0.181,50:0.179,52:0.179,53:0.178,55:0.179,57:0.174,59:0.146,60:0.119,62:0.096,64:0.095,65:0.093,67:0.094,69:0.095,71:0.096,72:0.091,74:0.076,76:0.060,77:0.046,79:0.044
damper.knock=0.05
damper.knockPeriods=0.5
damper.knockByKey=33:-31.0,34:-30.6,35:-30.4,36:-29.4,37:-29.3,38:-29.2,39:-29.0,40:-28.8,41:-28.6,42:-28.4,43:-28.3,44:-28.1,45:-24.7,46:-21.4,47:-17.8,48:-14.2,49:-10.6,50:-10.1,51:-9.3,52:-8.4,53:-7.5,54:-6.7,55:-5.9,56:-5.1,57:-4.6,58:-4.1,59:-3.7,60:-3.3,61:-3.2,62:-3.2,63:-3.2,64:-3.3,65:-3.5,66:-3.9,67:-4.5,68:-5.1,69:-5.7,70:-6.2,71:-6.5,72:-6.6,73:-6.6,74:-6.4,75:-6.2,76:-5.9,77:-5.6,78:-5.4,79:-5.2,80:-5.3,81:-5.5,82:-5.7,83:-5.8,84:-6.5,85:-7.1,86:-7.7,87:-8.4,88:-9.7,89:-10.6,90:-11.4,91:-12.3,92:-13.1,93:-13.3,94:-13.6,95:-13.8,96:-14.0
rel1.gainByKey=33:-1.2,34:-1.2,35:-1.2,36:-1.3,37:-1.3,38:-1.3,39:-1.3,40:-1.2,41:-1.1,42:-1.0,43:-1.0,44:-1.1,45:-1.2,46:-1.3,47:-1.4,48:-1.5,49:-1.6,50:-1.8,51:-1.9,52:-2.1,53:-2.0,54:-1.8,55:-1.6,56:-1.2,57:-0.8,58:-0.4,59:-0.0,60:0.3,61:0.5,62:0.6,63:0.3,64:-0.1,65:-0.7,66:-1.5,67:-2.2,68:-2.7,69:-3.0,70:-3.1,71:-3.1,72:-3.1,73:-3.0,74:-2.9,75:-2.7,76:-2.7,77:-2.7,78:-2.9,79:-3.1,80:-3.6,81:-4.1,82:-4.8,83:-5.5,84:-7.6,85:-9.6,86:-11.5,87:-13.3,88:-15.0,89:-15.4,90:-15.6,91:-16.8,92:-18.0,93:-19.3,94:-20.6,95:-21.7,96:-22.1
rel2.gainByKey=33:-1.2,34:-1.2,35:-1.2,36:-1.3,37:-1.3,38:-1.3,39:-1.3,40:-1.2,41:-1.1,42:-1.0,43:-1.0,44:-1.1,45:-1.2,46:-1.3,47:-1.4,48:-1.5,49:-1.6,50:-1.8,51:-1.9,52:-2.1,53:-2.0,54:-1.8,55:-1.6,56:-1.2,57:-0.8,58:-0.4,59:-0.0,60:0.3,61:0.5,62:0.6,63:0.3,64:-0.1,65:-0.7,66:-1.5,67:-2.2,68:-2.7,69:-3.0,70:-3.1,71:-3.1,72:-3.1,73:-3.0,74:-2.9,75:-2.7,76:-2.7,77:-2.7,78:-2.9,79:-3.1,80:-3.6,81:-4.1,82:-4.8,83:-5.5,84:-7.6,85:-9.6,86:-11.5,87:-13.3,88:-15.0,89:-15.4,90:-15.6,91:-16.8,92:-18.0,93:-19.3,94:-20.6,95:-21.7,96:-22.1
click.gainByKey=33:0.0,39:0.0,45:12.0,51:6.0,57:30.0,63:24.0,69:24.0,75:15.0,81:15.0,87:18.0,93:-24.0,96:30.0
tine.lossHf2ByKey=33:4e-06,34:4e-06,35:4e-06,36:4e-06,37:4e-06,38:4e-06,39:4e-06,40:1.42e-05,41:5.06e-05,42:1.42e-05,43:9.54e-05,44:1.42e-05,45:0.00018,46:5.06e-05,47:5.06e-05,48:5.06e-05,49:2.68e-05,50:2.68e-05,51:2.68e-05,52:2.68e-05,53:2.68e-05,54:7.54e-06,55:7.54e-06,56:2.68e-05,57:2.68e-05,58:2.68e-05,59:2.68e-05,60:2.68e-05,61:2.68e-05,62:1.42e-05,63:1.42e-05,64:1.42e-05,65:1.42e-05,66:1.42e-05,67:1.42e-05,68:4e-06,69:4e-06,70:4e-06,71:4e-06,72:4e-06,73:4e-06,74:4e-06,75:4e-06,76:4e-06,77:4e-06,78:4e-06,79:4e-06,80:4e-06,81:4e-06,82:4e-06,83:4e-06,84:4e-06,85:4e-06,86:4e-06,87:4e-06,88:4e-06,89:4e-06,90:4e-06,91:4e-06,92:4e-06,93:4e-06,94:4e-06,95:4e-06,96:4e-06
reed.glide=33:0,34:0,35:0,36:0,37:0,38:0,39:0,40:0,41:0,42:0,43:0,44:0,45:0,46:0,47:0,48:0,49:0,50:0,51:0,52:0,53:2.26,54:2.24,55:2.22,56:1.71,57:1.71,58:1.71,59:1.6,60:1.52,61:1.43,62:1.43,63:2.62,64:2.63,65:2.63,66:2.77,67:2.77,68:1.62,69:1.55,70:1.55,71:5.46,72:5.46,73:5.27,74:4.55,75:4.54,76:4.55,77:13.2,78:13.2,79:13.2,80:16.1,81:20.8,82:20.8,83:11,84:9.93,85:9.7,86:3.01,87:2.61,88:2.61,89:6.4,90:6.4,91:6.25,92:5.79,93:3.63,94:2.71,95:2.17,96:1.9
)"},
    {"clav6", "Hohner Clavinet D6", R"(
# preset:clav6 - the D6 as one instrument: the pickup rockers are parameters (clav.ab
# swaps the coil-pair set, clav.cd the solo/parallel positions) and so are the four
# tone switch banks (clav.soft/medium/treble/brilliant). Default state = what the
# acceptance heard: the bright set (now B), C position, banks off. The B position is
# calibrated against samples/clav_a (the bright pair), the A twins against samples/clav_b
# (the warm one) - clavinet_fit.py --set clav_a writes the B twins since the swap.
# The real four (Pickup_CA/CB/DA/DB, 2026-10-07: centroid 936/1194/1077/1442 Hz) settled
# the labelling: clav_a is the bright BRIDGE pair, clav_b the warm RHYTHM one - the
# calibration sets were swapped all along. The A position now carries the warm set, the
# B twins the bright set, and the default stays the accepted sound: B, solo (C).
# pickup.width is shared by both sets: the aperture of the coils does not move with
# the selector (the old clav6b ran on the 0.01 mm clamp - its 18.14 dB compare value
# predates the shared width curve, so B is due for a fresh compare).
# hybrid voice (docs/CLAVINET_HYBRID_BANK.md, docs/SAMPLELIB_BANKS.md): with banks/clav_hybrid.bank loaded (bank version 5,
# 44.1 kHz, from the Realistic Clavinet of the free libraries, fifth bank slot) the recorded position (clav.ab=1, clav.cd=0)
# plays the PCM attack + mode bank, at the bank's law; the other rocker positions and a missing bank play the physical model
# below
hybrid.on=1
hybrid.bank=4
tine.string=1
string.deadModes=4
num.maxModes=14
string.tension=150
string.massByKey=42:0.006,51:0.006,52:0.0031,88:0.0031
string.core=0.0009
string.bByKey=29:0.0005,34:0.0002,50:0.00009,51:0.0001,88:0.00008
vel.min=1
vel.max=4
pickup.exponent=1.0
hammer.substeps=4
amp.on=0
tone.bass=0
tone.treble=0
hammer.strike=42:0.170,55:0.150,67:0.110,84:0.080
tine.lossHf=0.04
tine.lossExp=0.7
tine.lossHf2=0
# the wrapped bass carries its highs longer than the plain treble strings (stand 28):
# per-key loss law, bass 0.03/f^0.5 blending to the standard 0.04/f^0.7 by key 48
tine.lossHfByKey=28:0.03,48:0.04
tine.lossExpByKey=28:0.5,48:0.7
string.releaseSemitones=3
pickup.offset=42:-0.25,88:-0.25
hammer.contact=42:12.135,52:2.427,64:1.213,76:0.607,88:0.303
hammer.alpha=42:3.5,88:3.5
pickup.width=42:1.2,60:1.0,67:0.9,88:0.85
string.deadGain=1.4
string.deadT60=4
# the tangent knock: the tip hits the anvil and couples through the string
# (ICMC 2013, 3.5) - the click behind the chain carries the attack centroid
click.gain=8
click.gainByKey=42:-7,55:-3,88:0
click.lpHz=6000
# the strike high pass: the bass and key 55 attack brighter than they sustain
pickup.onsetMul=42:6,48:4,55:12,60:0
pickup.onsetMs=8
# --- the A set (rocker A, solo position C) ------------------------------------------
decay.t60=42:26.6,43:27.2,45:29.4,47:25.6,48:28.7,50:27,52:19.1,53:21,55:11,57:10.3,59:9.54,60:9.3,62:9.48,64:4.62,65:4.74,67:4.36,69:5.22,71:5.5,72:5.19,74:5.34,76:4.94,77:2.11,79:4.01,81:3.7,83:3.42,84:3.42,86:2.98,88:2.12
decay.release=42:0.3,52:0.2,64:0.15,88:0.1
string.pickup2Gain=-0.1
string.pickupMetersByKey=42:0.028,43:0.025,45:0.020,47:0.015,48:0.020,50:0.020,52:0.020,53:0.045,55:0.045,57:0.045,59:0.040,60:0.025,62:0.040,64:0.045,65:0.045,67:0.040,69:0.035,71:0.020,72:0.020,74:0.055,76:0.055,77:0.050,79:0.040,81:0.040,83:0.035,84:0.035,86:0.035,88:0.043
string.pickup2MetersByKey=42:0.125,43:0.150,45:0.150,47:0.140,48:0.130,50:0.130,52:0.120,53:0.080,55:0.080,57:0.080,59:0.120,60:0.070,62:0.120,64:0.180,65:0.180,67:0.160,69:0.160,71:0.150,72:0.050,74:0.050,76:0.160,77:0.160,79:0.120,81:0.110,83:0.070,84:0.070,86:0.070,88:0.075
click.hpHz=1500
# the coil at the amp input: the register-independent colouring, the 'wah that
# does not quack' (docs/CLAVINET.md, sixteenth state)
pickup.resHz=1400
pickup.resQ=0.3
pickup.resDb=12
# --- the B set (rocker B, solo position C) - clav.ab=1 swaps to these twins -----------
decay.t60B=42:27.9,43:28.8,45:28.7,47:25.1,48:27.1,50:27.3,52:15,53:18.2,55:11.7,57:8.84,59:9.54,60:5.19,62:5.19,64:3.24,65:4.76,67:5.3,69:4.74,71:5.46,72:5.12,74:4.97,76:3.52,77:2.56,79:3.46,81:2.96,83:3.82,84:2.72,86:1.59,88:1.55
string.pickup2GainB=-0.3
string.pickupMetersByKeyB=42:0.038,43:0.020,45:0.025,47:0.025,48:0.025,50:0.035,52:0.040,53:0.040,55:0.030,57:0.030,59:0.030,60:0.030,62:0.040,64:0.040,65:0.040,67:0.035,69:0.035,71:0.035,72:0.025,74:0.020,76:0.020,77:0.035,79:0.035,81:0.030,83:0.015,84:0.015,86:0.015,88:0.015
string.pickup2MetersByKeyB=42:0.080,43:0.040,45:0.050,47:0.050,48:0.050,50:0.090,52:0.090,53:0.200,55:0.200,57:0.150,59:0.150,60:0.150,62:0.160,64:0.160,65:0.160,67:0.150,69:0.120,71:0.120,72:0.160,74:0.160,76:0.160,77:0.070,79:0.070,81:0.090,83:0.090,84:0.070,86:0.050,88:0.060
click.hpHzB=800
pickup.resHzB=2200
pickup.resQB=0.4
clav.ab=1
pickup.resDbB=17
)"},
    {"grand", "Grand Piano", R"(
# preset:grand - the hybrid grand (docs/PIANO_HYBRID_BANK.md, docs/PIANO_HYBRID_LOOP_DESIGN.md): a 100 ms PCM
# attack of the Splendid Grand Piano crossfaded into 48 freely decaying modes per key, the high band above the
# modes continued by a loop of the attack's own high band. It needs piano_hybrid.bank (version 4,
# 48 kHz); without it, or at another host rate, the grand is silent and Engine::hybridState() says why. The
# physical grand of docs/PIANO.md (strings, bridge sympathy, soundboard) is gone since 2026-10-09.
hybrid.on=1
# the strings ring along under the pedal and with held keys (docs/PIANO_HYBRID_RESONANCE.md): the coupling chosen
# by ear on 2026-10-20 between Pianoteq's ringing strings and its small pedal shimmer
res.on=1
res.gain=5e-6
# pedal curve of the dampers (docs/PIANO_HYBRID_PIANOTEQ_REVIEW.md P1, fitted to Pianoteq's half pedal, refitted with the
# damper of P3 below: the half-pedal loss scales with it): with the smoothstep of the engine, the tail of a staccato note
# 1 s after the release lies within 2 dB of Pianoteq's at CC64 70/80/90 and 5 dB too damped at 60 (relative to the free
# tail). The bank's 0.5/0.85 damped fully up to CC 80, where Pianoteq is nearly free.
damper.pedalLost=0.10
damper.pedalFull=0.74
# T60 under the damper (P3, stage 1): Pianoteq's damper takes about 27 dB in the first 0.2 s in every register (T60 0.45 s);
# the bank's curve (1.1 s at 28 .. 0.3 s at 100, the old physical grand's) was too slow in the bass and too fast in the treble
decay.release=28:0.45,100:0.45
# P3, stage 2: the felt lands over 60 ms, and 3 % of the amplitude (the polarisation the felt does not reach) decays with its
# own T60 of 2.5 s under the damper. Fitted to Pianoteq's damper alone (released minus held note) at C2 and C4: within 2.4 dB RMS
# over 0.05-1 s after the release. At C6 Pianoteq shows only its residual floor after 0.5 s, which is not a string share.
damper.rampMs=60
damper.slowShare=0.03
damper.slowT60=2.5
# P4: the loudness law. The bank's power law (v/92)^1.95 is 7 dB too quiet at v16 and silent at v1; Pianoteq is linear in dB
# (0.29 dB per step from v8 to v127) with a knee below v8. This curve adds the measured difference (peaks of A1, C4, C6
# relative to v92, mean) to the bank's law: pp louder, ff 2.8 dB louder. Refitted with the layers of the bank in phase (P5):
# between the anchors the coherent mix is up to 1.2 dB louder than the incoherent one was.
vel.gainDb=1:24.6,4:24.3,8:15.6,16:7.4,32:1.3,48:-0.6,64:-0.9,80:-0.6,92:0,112:1.8,127:2.8
# P6, the noises of the action on the hybrid voice: the pedal clunk of the engine (band 20-120 / 40-190 Hz) and the damper
# woosh, a high-passed noise burst 30 ms after the release of a damped key. Levels set to Pianoteq's order of magnitude
# (clunk 50-100 ms after the pedal: -12 dB below the peak of a C4 at v80 in the band below 200 Hz; woosh 30-60 ms after the
# release: -62 dB below the tone's peak above 6 kHz), to be judged by ear
pedal.downGain=0.0032
pedal.upGain=0.0032
human.pedal=0.25
# no release thump: Pianoteq's key release shows nothing below 2 kHz (the Mark I values would be inherited otherwise)
rel1.gain=0
rel2.gain=0
damper.wooshGain=0.00006
damper.wooshHz=6000
damper.wooshT60=0.06
damper.wooshDelayMs=30
human.release=0.3
# the output stage stays neutral (the defaults are the Mark I's): no amp, flat tone
amp.on=0
tone.bass=0
tone.treble=0
)"},
    {"pianet", "Hohner Pianet T", R"(
# preset:pianet - the hybrid voice only (docs/SAMPLELIB_BANKS.md): banks/pianet_hybrid.bank (bank version 5, 44.1 kHz, sixth bank
# slot) from Anne-Marie the Hohner T of the free libraries (the DI layers p and f of nine roots; the keys between from their
# nearest root). There is no physical model of the Pianet: without the bank, or at another host rate than the bank's, it is
# silent and Engine::hybridState() says why. The reed and its chain come from the Wurlitzer preset (the amp stage stays
# bypassed on the hybrid voice: the DI recordings carry the pickup); the recordings carry their dynamics, the bank's law alone.
hybrid.on=1
hybrid.bank=5
hybrid.velGain=0
)", "wurli200a"},
    {"steinway", "Steinway Grand", R"(
# preset:steinway - the hybrid voice only (docs/SAMPLELIB_BANKS.md): banks/steinway_hybrid.bank (bank version 5, 44.1 kHz, seventh
# bank slot) from the Steinway Grand of the free libraries (four layers of twelve roots, the keys between from their nearest
# root). Dampers, pedal, string resonance and the noises of the action are the grand's; the recordings carry their dynamics,
# the bank's law alone. Without the bank, or at another host rate than the bank's, it is silent.
hybrid.bank=6
hybrid.velGain=0
)", "grand"},
    {"grandpas", "Grandpa's Grand", R"(
# preset:grandpas - the hybrid voice only (docs/SAMPLELIB_BANKS.md): banks/grandpas_hybrid.bank (bank version 5, 44.1 kHz, eighth
# bank slot) from Grandpa's Piano of the free libraries (a Steinway L of 1933, five layers of 19 roots). Dampers, pedal, string
# resonance and the noises of the action are the grand's; the recordings carry their dynamics, the bank's law alone. Without
# the bank, or at another host rate than the bank's, it is silent.
hybrid.bank=7
hybrid.velGain=0
)", "grand"},
    {"cp80", "Yamaha CP80", R"(
# preset:cp80 - the hybrid voice only (docs/SAMPLELIB_BANKS.md): banks/cp80_hybrid.bank (bank version 5, 44.1 kHz, ninth bank
# slot) from Greg Sullivan's CP80 of the free libraries (four layers of 23 roots). The recordings are peak-normalised: the bank
# plays the grand's power law and the grand's vel.gainDb on top (Pianoteq's law, linear in dB), dampers, pedal, string
# resonance and the noises of the action are the grand's. Without the bank, or at another host rate than the bank's, it is
# silent.
hybrid.bank=8
)", "grand"},
};

} // namespace

// ---------------------------------------------------------------------------------------
// Defaults: base values, then the default instrument (first preset)
// ---------------------------------------------------------------------------------------
Params::Params() {
    // register trends only; the calibrated values per key come from the preset text
    t60 = Curve{{28, 38}, {64, 16}, {100, 1.1f}};

    // Contact time = 0.4 x period (bass 0.5 x period, max. 14 ms): even dynamic range over the
    // keyboard and, with the loss law, a tine "bell" (2nd mode) like the reference (B6).
    strikePos   = Curve{{28, 0.12f}, {64, 0.18f}, {100, 0.28f}};
    hammerMass  = Curve{{28, 0.006f}, {64, 0.004f}, {100, 0.0025f}};
    contactMs   = Curve{{28, 12.135f}, {34, 8.581f}, {40, 6.067f}, {46, 3.861f}, {52, 2.427f},
                        {58, 1.716f}, {64, 1.213f}, {70, 0.858f}, {76, 0.607f}, {82, 0.429f},
                        {88, 0.303f}, {94, 0.215f}, {100, 0.152f}};
    hammerAlpha = Curve{{28, 1.7f}, {100, 2.0f}};

    pickupWidth  = Curve{{28, 0.4f}, {64, 1.2f}, {100, 6.0f}};
    pickupOffset = Curve{{28, -0.5f}, {100, -0.5f}};

    releaseT60 = Curve{{28, 0.4f}, {64, 0.25f}, {100, 0.1f}};
    keyGainDb  = Curve{{28, 0.0f}, {100, 0.0f}};

    setText(kPresets[0].text);
}

// ---------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------
namespace {

struct ScalarEntry { const char* name; float Params::*ptr; const char* help; };
struct CurveEntry  { const char* name; Curve Params::*ptr; const char* help; };
struct VoiceEntry  { const char* name; float Voicing::*ptr; const char* help; };

const VoiceEntry kVoicing[] = {
    {"voicing.hammerHardness", &Voicing::hammerHardness, "% physical model: tip hardness, +100 = half contact time; hybrid voices: +100 = timbre 32 velocity steps harder"},
    {"voicing.hammerMass",     &Voicing::hammerMass,     "% hammer weight (physical model only)"},
    {"voicing.strikePoint",    &Voicing::strikePoint,    "% contact point along the tine (physical model only)"},
    {"voicing.tineGauge",      &Voicing::tineGauge,      "% tine diameter (designer re-tunes; physical model only)"},
    {"voicing.pickupGap",      &Voicing::pickupGap,      "% field width, wider = softer (physical model only)"},
    {"voicing.pickupOffset",   &Voicing::pickupOffset,   "x0 / width, added to the preset (physical model only)"},
    {"voicing.tonebarDetune",  &Voicing::tonebarDetune,  "cent, added to the preset (physical model only)"},
    {"voicing.damperRelease",  &Voicing::damperRelease,  "% decay time with the damper on (physical model only)"},
    {"voicing.decay",          &Voicing::decay,          "% hybrid voices: +100 = T60 of every mode doubled"},
    {"voicing.unison",         &Voicing::unison,         "% hybrid voices: spread of a partial's modes, -100 = none, +100 = doubled"},
    {"voicing.stretch",        &Voicing::stretch,        "cent/octave, hybrid voices: tuning stretch about C4 on the poles"},
};

const ScalarEntry kScalars[] = {
    {"tuning.a4",        &Params::a4,             "Hz"},
    {"tine.diameter",    &Params::tineDiameter,   "m"},
    {"tine.youngs",      &Params::tineYoungs,     "Pa"},
    {"tine.density",     &Params::tineDensity,    "kg/m^3"},
    {"tine.springMu",    &Params::springMu,       "spring mass / tine mass"},
    {"tine.springPos",   &Params::springPos,      "spring position / length"},
    {"tine.lossHf",      &Params::lossHf,         "higher-mode loss coefficient"},
    {"tine.lossExp",     &Params::lossExp,        "higher-mode loss exponent"},
    {"tine.lossHf2",     &Params::lossHf2,        "quadratic higher-mode loss"},
    {"tonebar.split",    &Params::tonebarSplit,   "cent, coupling (0 = rigid clamp)"},
    {"tonebar.detune",   &Params::tonebarDetune,  "cent"},
    {"tonebar.loss",     &Params::tonebarLoss,    "decay rate relative to tine"},
    {"hammer.lambda",    &Params::hammerLambda,   "s/m, contact hysteresis"},
    {"hammer.substeps",  &Params::hammerSubsteps, "sub-steps during contact"},
    {"keys.tuneCents",   &Params::keysTuneCents,  "sigma of the static tuning offset per key, cent (0 = off)"},
    {"keys.levelDb",     &Params::keysLevelDb,    "sigma of the static loudness offset per key, dB (0 = off)"},
    {"keys.seed",        &Params::keysSeed,       "seed of the static key deviations"},
    {"rel1.hz",          &Params::rel1Hz,         "Hz, release thump mode 1 (key stop)"},
    {"rel1.t60",         &Params::rel1T60,        "s"},
    {"rel1.gain",        &Params::rel1Gain,       "peak relative to the voice level (0 = off)"},
    {"rel1.delayMs",     &Params::rel1DelayMs,    "ms from the key release to the blow"},
    {"rel2.hz",          &Params::rel2Hz,         "Hz, release thump mode 2 (damper)"},
    {"rel2.t60",         &Params::rel2T60,        "s"},
    {"rel2.gain",        &Params::rel2Gain,       "peak relative to the voice level (0 = off)"},
    {"rel2.delayMs",     &Params::rel2DelayMs,    "ms from the key release to the blow"},
    {"human.release",    &Params::humanRelease,   "sigma of ln release thump amplitude per release (0 = off)"},
    {"pedal.downGain",   &Params::pedalDownGain,  "RMS of the clunk when the pedal goes down, relative to the voice level (0 = off)"},
    {"pedal.upGain",     &Params::pedalUpGain,    "RMS of the clunk when the pedal comes up, relative to the voice level (0 = off)"},
    {"human.pedal",      &Params::humanPedal,     "sigma of ln pedal clunk amplitude per movement (0 = off)"},
    {"damper.wooshGain",    &Params::damperWooshGain,    "hybrid grand: noise amplitude of the felt landing, relative to the voice level (0 = off)"},
    {"damper.wooshHz",      &Params::damperWooshHz,      "Hz, high pass of the woosh"},
    {"damper.wooshT60",     &Params::damperWooshT60,     "s, decay of the woosh"},
    {"damper.wooshDelayMs", &Params::damperWooshDelayMs, "ms from the key release to the woosh"},
    {"human.speed",      &Params::humanSpeed,     "sigma of ln hammer speed per strike (0 = off)"},
    {"human.contact",    &Params::humanContact,   "sigma of ln contact stiffness per strike (0 = off)"},
    {"human.seed",       &Params::humanSeed,      "seed of the strike variation"},
    {"hammer.speedHardening", &Params::hammerSpeedHardening,
                              "% extra hardness above 2 m/s, full at 3 m/s (0 = off)"},
    {"vel.min",          &Params::velMin,         "m/s"},
    {"vel.max",          &Params::velMax,         "m/s"},
    {"vel.gamma",        &Params::velGamma,       "curve exponent"},
    {"pickup.exponent",  &Params::pickupExponent, "flux falloff exponent"},
    {"pickup.onsetMs",   &Params::pickupOnsetMs,  "ms, time constant of the glide of pickup.onsetMul"},
    {"pickup.onsetVel",  &Params::pickupOnsetVel, "onset corner scales with the hammer speed: (speed/2)^p"},
    {"pickup.hpfHz",     &Params::pickupHpfHz,    "Hz, type 4: one-pole high pass behind the capacitance law (0 = off)"},
    {"pickup.resHz",     &Params::pickupResHz,   "Hz, coil resonance at the amp input (0: off)"},
    {"pickup.resQ",      &Params::pickupResQ,    "its width"},
    {"pickup.resDb",     &Params::pickupResDb,   "dB, its height"},
    {"clav.soft",        &Params::clavSoft,      "1 = the Soft bank of the D6 tone switches (30k||0.1u)"},
    {"clav.medium",      &Params::clavMedium,    "1 = the Medium bank (10k||15n)"},
    {"clav.treble",      &Params::clavTreble,    "1 = the Treble bank (2H||4.7n trap at 1.6 kHz)"},
    {"clav.brilliant",   &Params::clavBrilliant, "1 = the Brilliant bank (0.6 H coil)"},
    {"clav.ab",          &Params::clavAb,        "0 = the A coil pair, 1 = the B pair (the right rocker)"},
    {"clav.cd",          &Params::clavCd,        "0 = C solo, 1 = D parallel (A+D in phase, B+D out of phase)"},
    {"string.pickup2GainB", &Params::stringPickup2GainB, "the B set's second comb share"},
    {"pickup.resHzB",    &Params::pickupResHzB,  "Hz, the B set's coil resonance (0: follow A)"},
    {"pickup.resQB",     &Params::pickupResQB,   "its width"},
    {"pickup.resDbB",    &Params::pickupResDbB,  "its height"},
    {"click.hpHzB",      &Params::clickHpHzB,    "Hz, the B set's click high pass (<0: follow A)"},
    {"pickup.type",      &Params::pickupType,     "0 electromagnetic (Rhodes), 1 electrostatic one plate, 2 electrostatic reed in the slot, 3 the plates subtracted (odd, Wurlitzer)"},
    {"damper.pos",       &Params::damperPos,      "contact point / length"},
    {"damper.rampMs",    &Params::damperRampMs,   "ms"},
    {"damper.pedalLost", &Params::damperPedalLost, "CC64/127: dampers stay on below"},
    {"damper.pedalFull", &Params::damperPedalFull, "CC64/127: dampers are off above"},
    {"damper.slowShare", &Params::damperSlowShare, "hybrid grand: share of the amplitude the felt does not reach (0 = off)"},
    {"damper.slowT60",   &Params::damperSlowT60,   "s, hybrid grand: decay of that share under the damper"},
    {"mech1.hz",         &Params::mech1Hz,        "Hz, harp/action mode driven by the hammer"},
    {"mech1.t60",        &Params::mech1T60,       "s"},
    {"mech1.gain",       &Params::mech1Gain,      "coupling into the output (0 = off)"},
    {"mech2.hz",         &Params::mech2Hz,        "Hz"},
    {"mech2.t60",        &Params::mech2T60,       "s"},
    {"mech2.gain",       &Params::mech2Gain,      "coupling (0 = off)"},
    {"mech3.hz",         &Params::mech3Hz,        "Hz"},
    {"mech3.t60",        &Params::mech3T60,       "s"},
    {"click.gain",       &Params::clickGain,      "hammer click behind the chain: noise gated by the hammer force"},
    {"click.hpHz",       &Params::clickHpHz,      "Hz"},
    {"click.lpHz",       &Params::clickLpHz,      "Hz"},
    {"click.exp",        &Params::clickExp,       "exponent of the click over the hammer force (1 = proportional)"},
    {"mech3.gain",       &Params::mech3Gain,      "coupling (0 = off)"},
    {"mech.exp",         &Params::mechExp,        "exponent of the mech modes over the hammer force (1 = proportional)"},
    {"damper.knock",     &Params::damperKnock,    "speed [m/s] of the impulse of the damper landing on the reed (0 = off)"},
    {"damper.knockPeriods", &Params::damperKnockPeriods, "duration of the damper knock in periods of f1 (0 = impulse)"},
    {"mech.levelled",    &Params::mechLevelled,   "1: force of mech modes and click with vel.gainDb, 0: raw hammer force"},
    {"elec.lpfHz",       &Params::lpfHz,          "Hz"},
    {"elec.lpfQ",        &Params::lpfQ,           ""},
    {"elec.hpfHz",       &Params::hpfHz,          "Hz"},
    {"out.gain",         &Params::masterGain,     "linear"},
    {"out.trimDb",       &Params::trimDb,         "dB level trim of the instrument, apart from out.gain"},
    {"out.voiceRms",     &Params::voiceRms,       "auto-voicing target RMS"},
    {"out.clip",         &Params::softClip,       "1 = soft clip near full scale, 0 = off"},
    {"hybrid.on",        &Params::hybridOn,       "1 = hybrid voice (needs a bank), 0 = physical model"},
    {"hybrid.bank",      &Params::hybridBank,     "bank slot of the hybrid voice: 0 = grand, 1 = mark1, 2 = mark2 (and suitcase), 3 = wurlitzer, 4 = clavinet, 5 = pianet, 6 = steinway, 7 = grandpas, 8 = cp80"},
    {"hybrid.velGain",   &Params::hybridVelGain,  "1 = vel.gainDb of the preset on top of the bank's loudness law, 0 = the bank's law alone"},
    {"hybrid.amp",       &Params::hybridAmp,      "1 = the amp stage (amp.on) stays in the path on the hybrid voice (suitcase), 0 = bypassed there"},
    {"res.on",           &Params::resOn,          "1 = the hybrid grand's resonance bank (strings ring along), 0 = off"},
    {"res.gain",         &Params::resGain,        "coupling of the resonance bank (input weight at the key's f0)"},
    {"res.modes",        &Params::resModes,       "modes per key in the resonance bank"},
    {"res.tiltDb",       &Params::resTiltDb,      "dB per octave of the resonance input weight above f0"},
    {"res.undamped",     &Params::resUndamped,    "1 = keys without a damper ring along without the pedal too"},
    {"amp.on",           &Params::ampStage,       "1 = tone controls, power amp and cabinet in the path, 0 = bypassed"},
    {"tone.bass",        &Params::toneBassDb,     "dB, preamp low shelf 150 Hz"},
    {"tone.treble",      &Params::toneTrebleDb,   "dB, preamp high shelf 3 kHz"},
    {"amp.drive",        &Params::ampDrive,       "dB, power amp saturation (0 = clean)"},
    {"amp.asym",         &Params::ampAsym,        "saturation asymmetry 0..0.5"},
    {"amp.perVoice",     &Params::ampPerVoice,    "1 = amp.drive saturates every key before the sum (chords stay clean), 0 = the sum"},
    {"spk.lowHz",        &Params::spkLowHz,       "Hz, cabinet high pass (0 = off)"},
    {"spk.lowQ",         &Params::spkLowQ,        ""},
    {"spk.highHz",       &Params::spkHighHz,      "Hz, cone low pass (0 = off)"},
    {"spk.highQ",        &Params::spkHighQ,       ""},
    {"vib.rate",         &Params::vibRateHz,      "Hz, stereo vibrato"},
    {"vib.depth",        &Params::vibDepth,       "0 = off, 1 = full panning"},
    {"trem.rate",        &Params::tremRateHz,     "Hz, amplifier tremolo (200A: 5.6)"},
    {"trem.depth",       &Params::tremDepth,      "0 = off, 1 = gain to zero at the LFO trough"},
    {"detune.cents",     &Params::detuneCents,    "cent, left +, right -"},
    {"detune.mix",       &Params::detuneMix,      "level of the shifted copies"},
    {"detune.winMs",     &Params::detuneWinMs,    "ms, shifter window (<= 40)"},
    {"fx.on",           &Params::fxOn,            "1 = MK8 preamp and effect chain after the output stage"},
    {"fx.volume",       &Params::fxVolume,        "% preamp volume (53 = unity)"},
    {"fx.drive",        &Params::fxDrive,         "% preamp drive"},
    {"fx.envelope",     &Params::fxEnvelope,      "% touch-wah (velvetkeys)"},
    {"fx.bass",         &Params::fxBass,          "dB -15..15"},
    {"fx.mid",          &Params::fxMid,           "dB -15..15"},
    {"fx.midFreq",      &Params::fxMidFreq,       "Hz 100..2000"},
    {"fx.treble",       &Params::fxTreble,        "dB -15..15"},
    {"fx.panOn",        &Params::fxPanOn,         "Vari-Pan off/on"},
    {"fx.panWave",      &Params::fxPanWave,       "0 square, 1 ramp, 2 triangle, 3 sine"},
    {"fx.panRate",      &Params::fxPanRate,       "Hz 0.1..3800"},
    {"fx.panDepth",     &Params::fxPanDepth,      "%"},
    {"fx.compOn",       &Params::fxCompOn,        "compressor off/on"},
    {"fx.compAmount",   &Params::fxCompAmount,    "%"},
    {"fx.compMakeup",   &Params::fxCompMakeup,    "dB 0..20"},
    {"fx.chorusOn",     &Params::fxChorusOn,      "chorus off/on"},
    {"fx.chorusRate",   &Params::fxChorusRate,    "Hz 0.2..20"},
    {"fx.chorusDepth",  &Params::fxChorusDepth,   "ms 0..10"},
    {"fx.phaserOn",     &Params::fxPhaserOn,      "phaser off/on"},
    {"fx.phaserRate",   &Params::fxPhaserRate,    "Hz 0.1..10"},
    {"fx.phaserDepth",  &Params::fxPhaserDepth,   "%"},
    {"fx.delayOn",      &Params::fxDelayOn,       "delay off/on"},
    {"fx.delayTime",    &Params::fxDelayTime,     "s 0..4 (hardware knob 0.06..0.8; longer only via tap tempo)"},
    {"fx.delayFeedback", &Params::fxDelayFeedback, "%"},
    {"fx.delayMix",     &Params::fxDelayMix,      "%"},
    {"fx.delayPingPong", &Params::fxDelayPingPong, "ping-pong off/on"},
    {"num.oversampling", &Params::oversampling,   "1 or 2"},
    {"num.femElements",  &Params::femElements,    "beam elements"},
    {"num.maxModes",     &Params::maxModes,       "modes per key, 1..10"},
    {"num.maxVoices",    &Params::maxVoices,      "polyphony limit, 0 = all keys"},
    {"num.sleepDb",      &Params::sleepDb,        "dB"},
    {"tine.string",      &Params::tineString,     "1 = pinned string (Clavinet) instead of the cantilever"},
    {"string.tension",   &Params::stringTension,  "N"},
    {"string.mass",      &Params::stringMass,     "kg/m (with winding: the wrapped value)"},
    {"string.core",      &Params::stringCore,     "m core diameter, for the bending stiffness"},
    {"string.youngs",    &Params::stringYoungs,   "Pa"},
    {"string.pickupPos", &Params::stringPickupPos, "pickup / speaking length, from the tangent end"},
    {"string.pickupMeters", &Params::stringPickupMeters, "m, pickup distance from the far end (overrides the ratio)"},
    {"string.pickup2Meters", &Params::stringPickup2Meters, "m, second pickup from the far end (-1: off)"},
    {"string.pickup2Gain", &Params::stringPickup2Gain, "share of the second comb, negative = anti-phase"},
    {"string.releaseSemitones", &Params::stringReleaseSemitones, "pitch drop at key release (D6: 3)"},
    {"string.b",         &Params::stringB,         "inharmonicity B (<= 0: from the core stiffness)"},
    {"string.deadModes", &Params::stringDeadModes, "whole-string modes behind the ladder (0: off)"},
    {"string.deadGain",  &Params::stringDeadGain,  "excitation share of the whole-string modes"},
    {"string.deadT60",   &Params::stringDeadT60,   "s, their decay (the yarn lies on that part)"},
};

const CurveEntry kCurves[] = {
    {"decay.t60",      &Params::t60,          "s, fundamental"},
    {"string.massByKey", &Params::stringMassByKey, "kg/m per key (wound bass, plain treble)"},
    {"string.pickupPosByKey", &Params::stringPickupPosByKey, "pickup position per key as a ratio"},
    {"string.pickupMetersByKey", &Params::stringPickupMetersByKey, "pickup distance per key [m]"},
    {"string.pickup2MetersByKey", &Params::stringPickup2MetersByKey, "second pickup distance per key [m]"},
    {"string.pickupMetersByKeyB", &Params::stringPickupMetersByKeyB, "the B set's pickup distance per key [m]"},
    {"string.pickup2MetersByKeyB", &Params::stringPickup2MetersByKeyB, "the B set's second pickup distance [m]"},
    {"decay.t60B",     &Params::decayT60B,   "s, the B set's fundamental decay (empty: follow A)"},
    {"string.bByKey",  &Params::stringBByKey, "measured inharmonicity B per key (empty: string.b)"},
    {"tine.springMuByKey", &Params::springMuByKey, "spring mass / tine mass per key (empty: scalar)"},
    {"tine.springPosByKey", &Params::springPosByKey, "spring position / tine length per key"},
    {"tine.lossHf2ByKey", &Params::lossHf2ByKey, "quadratic high-mode loss per key"},
    {"tine.lossHfByKey", &Params::lossHfByKey, "high-mode loss per key (empty: tine.lossHf)"},
    {"tine.lossExpByKey", &Params::lossExpByKey, "loss exponent per key (empty: tine.lossExp)"},
    {"decay.release",  &Params::releaseT60,   "s, fundamental with damper"},
    {"damper.harpT60", &Params::damperHarpT60, "s, uniform extra decay under the felt (empty: off)"},
    {"damper.velDb", &Params::damperVelDb, "dB damper effectiveness over velocity (empty: off)"},
    {"rel1.gainByKey", &Params::rel1ByKey, "dB release thump mode 1 by key (empty: 0)"},
    {"keys.tuneByKey", &Params::keysTuneByKey, "cent tuning offset by key, on top of keys.tuneCents (empty: 0)"},
    {"rel2.gainByKey", &Params::rel2ByKey, "dB release thump mode 2 by key (empty: 0)"},
    {"mech.gainByKey", &Params::mechByKey, "dB coupling of the hammer force into the mech modes by key (empty: 0)"},
    {"click.gainByKey", &Params::clickByKey,    "dB coupling of the hammer force into the click by key (empty: 0)"},
    {"damper.knockByKey", &Params::damperKnockByKey, "dB of the damper knock by key"},
    {"mech.expByKey",   &Params::mechExpByKey,  "exponent of the mech modes over the hammer force per key"},
    {"click.expByKey",  &Params::clickExpByKey, "exponent of the click over the hammer force per key"},
    {"hammer.strike",  &Params::strikePos,    "contact point / length"},
    {"hammer.mass",    &Params::hammerMass,   "kg"},
    {"hammer.contact", &Params::contactMs,    "ms at 1 m/s (rigid wall)"},
    {"hammer.alpha",   &Params::hammerAlpha,  "force exponent"},
    {"hammer.speedHardeningByKey", &Params::hammerSpeedHardeningByKey,
                                   "% adjustment to high-speed hardness by key"},
    {"pickup.width",   &Params::pickupWidth,  "mm"},
    {"pickup.onsetMul", &Params::pickupOnsetMul, "per key: high pass at this multiple of f1 at the strike, gliding down (0 = off)"},
    {"reed.glide",     &Params::reedGlide,    "per key: cents the pitch drops per mm of swing (pickup law), linear (0 = off)"},
    {"pickup.offset",  &Params::pickupOffset, "x0 / width"},
    {"out.keyGainDb",  &Params::keyGainDb,    "dB"},
    {"vel.speed",      &Params::velSpeed,     "m/s over MIDI velocity (empty: vel.min/max/gamma)"},
    {"vel.gainDb",     &Params::velGainDb,    "dB over MIDI velocity, level of a strike (empty: 0)"},
    {"vel.gainSlope",  &Params::velGainSlope, "dB per velocity step below 127, per key (empty: 0)"},
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

// The public parameters: what the hybrid voices (piano_hybrid.*, Engine::updateHybrid, noteOn/noteOff/controller on the
// hybrid side) and the output stage read. Measured 2026-10-22 (README "Voicing"): everything else changes nothing on a
// hybrid voice.
bool Params::isPublic(const std::string& name) {
    static const char* const kPrefixes[] = {"hybrid.", "res.", "fx.", "rel1.", "rel2.", "pedal.", "spk.", "tone.", "trem.",
                                            "vib.", "detune.", "clav."};
    static const char* const kNames[] = {
        "voicing.hammerHardness", "voicing.decay", "voicing.unison", "voicing.stretch",   // on the bank's poles and layers
        "decay.release",                                                                 // the hybrid's release T60
        "damper.rampMs", "damper.pedalLost", "damper.pedalFull",                         // the pedal curve
        "damper.slowShare", "damper.slowT60",                                            // the two-population damper
        "damper.wooshGain", "damper.wooshHz", "damper.wooshT60", "damper.wooshDelayMs",  // the felt landing
        "human.release", "human.pedal",                                                  // variation of the noises
        "vel.gainDb",                                                                    // with hybrid.velGain
        "amp.on", "amp.drive", "amp.asym",                                               // the amp stage (hybrid.amp)
        "out.gain", "out.trimDb", "out.clip",
        "num.maxVoices",                                                                 // the voice limit (HybridPlayer::setMaxKeys)
        "string.releaseSemitones",                                                       // the clavinet's release
    };
    for (const char* pre : kPrefixes)
        if (name.compare(0, std::strlen(pre), pre) == 0) return true;
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
    int index = -1;
    const size_t at = lhs.find('@');
    if (at != std::string::npos) {
        index = std::atoi(lhs.c_str() + at + 1);
        lhs = lhs.substr(0, at);
    }
    if (!internalToo && !isPublic(lhs))
        return fail(error, lhs + " belongs to the physical model and is internal (presets, the C API and pmp-render --internal set it)");

    for (const auto& e : kScalars) {
        if (lhs != e.name) continue;
        float v;
        if (index >= 0 || !parseFloat(rhs, v)) return fail(error, "bad value for " + lhs);
        float& p = this->*e.ptr;
        p = (op == '*') ? p * v : (op == '+') ? p + v : v;
        return true;
    }
    for (const auto& e : kVoicing) {
        if (lhs != e.name) continue;
        float v;
        if (index >= 0 || !parseFloat(rhs, v)) return fail(error, "bad value for " + lhs);
        float& p = voicing.*e.ptr;
        p = (op == '*') ? p * v : (op == '+') ? p + v : v;
        return true;
    }
    for (const auto& e : kCurves) {
        if (lhs != e.name) continue;
        Curve& c = this->*e.ptr;
        if (op == '=' && index < 0) {
            if (!c.parse(rhs)) return fail(error, "bad curve for " + lhs + " (use key:value,...)");
            return true;
        }
        float v;
        if (!parseFloat(rhs, v)) return fail(error, "bad value for " + lhs);
        const int first = index >= 0 ? index : 0;
        const int last  = index >= 0 ? index : c.n - 1;
        if (first < 0 || last >= c.n) return fail(error, "curve index out of range for " + lhs);
        for (int i = first; i <= last; ++i)
            c.val[i] = (op == '*') ? c.val[i] * v : (op == '+') ? c.val[i] + v : v;
        return true;
    }
    return fail(error, "unknown parameter " + lhs);
}

bool Params::setText(const std::string& text, std::string* error) {
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
        if (!set(line.substr(a, b - a + 1), error)) return false;
    }
    return true;
}

std::string Params::dump(bool internalToo) const {
    std::ostringstream s;
    for (const auto& e : kVoicing)
        if (internalToo || isPublic(e.name)) s << e.name << "=" << formatNumber(voicing.*e.ptr) << "    # " << e.help << "\n";
    for (const auto& e : kScalars)
        if (internalToo || isPublic(e.name)) s << e.name << "=" << formatNumber(this->*e.ptr) << "    # " << e.help << "\n";
    for (const auto& e : kCurves)
        if (internalToo || isPublic(e.name)) s << e.name << "=" << (this->*e.ptr).toString() << "    # " << e.help << "\n";
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

bool Params::loadPreset(const std::string& id, std::string* error) {
    // a preset may derive from a base preset (the suitcase is the mark2 core plus
    // its signal chain): the derived text is appended after the base, so single
    // assignments override, curves simply replace
    std::string text;
    std::string cur = id;
    while (true) {
        const int i = presetIndex(cur);
        if (i < 0) return fail(error, "unknown instrument " + cur);
        text = std::string(kPresets[i].text) + (kPresets[i].own ? std::string("\n") + kPresets[i].own : std::string()) + "\n" + text;
        if (!kPresets[i].base) break;
        cur = kPresets[i].base;
    }
    Params p;
    if (!p.setText(text, error)) return false;
    *this = p;
    return true;
}

} // namespace pmp
