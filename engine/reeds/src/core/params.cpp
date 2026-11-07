// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// params.cpp - defaults and name-based access for Params
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
// tools/analysis/calibrate.py against samples/<instrument>. Every preset starts from the base
// values (Params(nullptr)), none inherits from another. The first preset is the default instrument.
// ---------------------------------------------------------------------------------------
namespace {

struct Preset {
    const char* id;
    const char* name;
    const char* text;
};

// The preset texts below keep their notes (calibration, sources) as '#' comments. The build drops every comment, from
// '#' to the end of the line as setText() does, so the notes stay in the sources and out of the binary.
template <std::size_t N>
struct PresetText {
    char text[N] = {};
    constexpr PresetText(const char (&in)[N]) {
        std::size_t o = 0;
        bool comment = false;
        for (std::size_t i = 0; i < N; ++i) {
            if (in[i] == '#') comment = true;
            if (in[i] == '\n') comment = false;
            if (!comment) text[o++] = in[i];
        }
    }
};

constexpr PresetText kWurliText{R"(
# preset:wurli - the reed of pmpiano's wurli200a (docs/REEDS.md), calibrated with tools/analysis/calibrate.py against
# samples/wurli
keys.lo=33
keys.hi=96
excitation=0
reed.thickness=0.0005
reed.width=0.003
reed.tipMu=0.3
reed.tipPos=0.99
reed.tipMuByKey=34:0.868,35:0.847,36:0.841,37:0.834,38:0.809,39:0.809,40:0.802,41:0.733,42:0.732,43:0.716,44:0.712,45:0.697,46:0.693,47:0.691,48:0.694,50:0.697,51:0.692,52:0.692,54:0.542,55:0.437,56:0.408,59:0.483,60:0.483,61:0.476,62:0.476,63:0.478,64:0.457,66:0.446,67:0.456,68:0.414,71:0.369,72:0.369,73:0.369,74:0.369,75:0.347,78:0.304,79:0.226,81:0.214,82:0.193,84:0.172,89:0.181,95:0.023,96:0.023
pickup.type=4
pickup.warp=-0.1
pickup.hpfHz=880
pickup.comb=0.22
pickup.onsetMul=33:5,40:4,46:2.5,50:1.8,54:0.6,58:0.15,62:0
pickup.onsetMs=18
pickup.width=33:0.03688,34:0.03512,35:0.04176,36:0.02878,37:0.0339,38:0.03682,39:0.0421,40:0.05175,41:0.04407,43:0.04305,44:0.04469,45:0.05018,46:0.04585,47:0.04677,48:0.04639,49:0.04602,50:0.05034,51:0.05086,52:0.06134,53:0.07183,54:0.106,55:0.1049,56:0.08785,57:0.1005,58:0.1001,59:0.1054,60:0.1108,61:0.1161,62:0.09003,63:0.1238,64:0.1173,65:0.0966,66:0.1328,68:0.08,69:0.1012,70:0.1246,71:0.1106,72:0.09668,73:0.1279,74:0.1141,75:0.221,76:0.1268,77:0.09783,78:0.3581,79:0.354,80:0.4485,81:0.2709,82:0.2626,83:0.2847,84:0.3068,85:0.3289,86:0.3805,87:0.4322,88:0.3237,89:0.3996,90:0.4999,92:4.721,93:0.8071,94:0.8822,95:4.195,96:2.161
pickup.offset=33:-0.2304,34:0.7725,35:0.2507,36:-0.3362,37:-0.2394,38:0.4074,39:0.6435,40:0.2969,41:0.5741,43:0.3842,44:0.3278,45:0.2151,46:-0.06836,47:0.1999,48:0.2702,49:0.3406,50:0.06321,51:0.3983,52:0.4625,53:0.5267,54:0.03113,55:-0.0573,56:0.2316,57:0.455,58:0.1143,59:0.1482,60:0.1822,61:0.2161,62:0.1299,63:0.2408,64:0.2407,65:0.1666,66:0.2053,68:0.5227,69:0.5495,70:0.2594,71:0.2156,72:0.1717,73:0.2744,74:0.1773,75:-0.9136,76:0.5156,77:0.6827,78:-0.9589,79:0.3456,80:-0.9508,81:0.4103,82:0.6684,83:0.5785,84:0.4887,85:0.3988,86:0.2956,87:0.1924,88:0.2112,89:0.1785,90:0.1947,92:-0.9892,93:0.001417,94:0.01332,95:-0.9583,96:0.1079
hammer.mass=28:0.0006,64:0.0004,100:0.00025
hammer.contact=33:10.48,34:10.16,35:9.591,36:9.054,37:8.547,38:8.069,39:7.617,40:7.19,41:5.873,43:5.594,44:5.627,45:5.391,46:5.089,47:4.804,48:3.247,49:4.281,50:4.041,51:3.633,52:2.645,53:3.4,54:3.209,55:3.03,56:2.86,57:2.614,58:2.549,59:1.976,60:1.437,61:1.718,62:2.024,63:1.865,64:1.485,65:1.542,66:1.599,68:1.425,69:1.345,70:1.27,71:0.7852,72:0.927,73:1.069,74:1.009,75:0.9527,76:0.589,77:0.8298,78:0.8017,79:0.6501,80:0.7146,81:0.6746,82:0.5453,83:0.3446,84:0.2993,85:0.3669,86:0.275,87:0.4313,88:0.3675,89:0.1242,90:0.3931,92:0.3111,93:0.0602,94:0.1015,95:0.08441,96:0.07638
hammer.speedHardening=0
hammer.alpha=28:2,55:2,64:3.5,100:3.5
vel.gamma=1.204
decay.t60=33:16.6,34:17,35:18.9,36:19.4,37:18.1,38:16.4,39:15.5,40:15.2,41:16.2,43:15.8,44:14.8,45:13.8,46:11.8,47:13.5,48:11.3,49:12.6,50:12.2,51:12.2,52:10.1,53:9.13,54:7.62,55:8.46,56:6.34,57:9.8,58:9.72,59:8.81,60:7.99,61:7.99,62:3.71,63:3.63,64:7.06,65:4.33,66:4.8,68:2.73,69:3.61,70:2.56,71:2.56,72:3.21,73:3.51,74:3.22,75:4.44,76:4.8,77:3.85,78:4.97,79:4.36,80:4.52,81:3.13,82:3.07,83:3.07,84:3.25,85:4.19,86:3.12,87:3.02,88:2.45,89:1.21,90:1.47,92:2.06,93:2.28,94:1.04,95:0.837,96:0.828
decay.release=33:0.318,36:0.258,40:0.204,43:0.193,47:0.181,50:0.179,53:0.178,57:0.174,59:0.146,60:0.119,62:0.096,67:0.094,72:0.091,74:0.076,76:0.06,77:0.046,79:0.044
damper.pos=0.45
damper.knock=0.05
damper.knockPeriods=0.5
damper.knockByKey=33:-31,44:-28.1,48:-14.2,52:-8.4,56:-5.1,60:-3.3,64:-3.3,68:-5.1,72:-6.6,76:-5.9,80:-5.3,84:-6.5,88:-9.7,92:-13.1,96:-14
reed.glide=52:0,53:9.71e-10,54:0,55:0,56:0,57:0,58:0,59:0,60:1.13e-13,61:0,62:0.0855,63:0,64:0.037,65:2.18,66:6.75,68:1.34,69:0.904,70:1.54,71:4.69,72:1.41,73:5.79,74:5.2,75:4.69,76:4.68,77:2.44,78:7.75,79:11.7,80:7.89,81:9.28,82:9.34,83:0,84:14,85:5.78,86:3.01,87:12.5,88:6.42,89:14.5,90:11.3,92:240,93:9.31,94:22,95:2.48,96:1.9
rel1.hz=106
rel1.t60=0.45
rel1.gain=0.01
rel1.delayMs=0
rel2.hz=327
rel2.t60=0.43
rel2.gain=0.02
rel2.delayMs=0
human.release=0.3
human.speed=0.05
human.contact=0.1
keys.tuneCents=1.5
keys.levelDb=0.6
keys.seed=200
keys.tuneByKey=33:4.3,34:4.9,35:5.1,36:2.6,37:4.4,38:1.5,39:1.2,40:-0.4,41:0.8,43:0.7,44:1.1,45:-0.1,46:0.8,47:0.0,48:1.8,49:1.1,50:0.3,51:1.3,52:1.0,53:-1.6,54:0.3,55:-1.5,56:2.1,57:-2.1,58:1.4,59:-0.1,60:-0.4,61:1.4,62:-0.6,63:3.1,64:1.8,65:1.8,66:2.4,68:0.8,69:1.7,70:-3.2,71:2.4,72:-0.8,73:1.4,74:2.4,75:3.0,76:3.2,77:-0.5,78:2.2,79:4.3,80:0.9,81:1.2,82:1.8,83:3.7,84:3.8,85:3.1,86:4.5,87:2.7,88:4.3,89:0.0,90:4.0,92:7.1,93:6.5,94:8.6,95:3.1,96:5.2
vel.gainDb=0:4.79,58:4.79,81:2.22,99:0.88,122:0.05,127:0.00
vel.gainSlope=33:-0.0156,34:-0.0114,35:-0.0035,36:-0.0104,37:-0.0138,38:-0.0068,39:-0.0084,40:-0.0101,41:0.0152,43:0.0130,44:0.0138,45:0.0175,46:0.0111,47:0.0179,48:0.0000,49:0.0102,50:0.0024,51:-0.0050,52:0.0000,53:-0.0003,54:0.0196,55:0.0728,56:0.0325,57:0.0015,58:-0.0275,59:0.0000,60:0.0000,61:0.0088,62:0.0535,63:0.0041,64:0.0100,65:0.0166,66:-0.0302,68:-0.0172,69:-0.0027,70:-0.0350,71:0.0000,72:0.0382,73:-0.0522,74:-0.0798,75:-0.1081,76:-0.1085,77:-0.0110,78:-0.0562,79:-0.0193,80:-0.1031,81:-0.1360,82:-0.0788,83:0.0000,84:0.0000,85:-0.1135,86:0.0000,87:-0.0289,88:-0.0082,89:-0.0454,90:-0.0391,92:0.1784,93:-0.1025,94:0.0688,95:-0.0148,96:0.0231
out.keyGainDb=33:-2.11,34:-3.91,35:-2.98,36:0.06,37:-0.80,38:-0.10,39:-1.72,40:-1.79,41:-0.76,43:-0.40,44:0.99,45:1.55,46:1.69,47:2.68,48:-0.79,49:2.92,50:3.19,51:1.85,52:1.85,53:0.10,54:0.78,55:4.35,56:2.43,57:0.96,58:3.39,59:2.43,60:-1.11,61:3.76,62:2.21,63:3.97,64:4.62,65:2.21,66:1.06,68:4.99,69:4.53,70:3.50,71:-1.30,72:5.12,73:0.00,74:1.72,75:-0.89,76:1.04,77:2.43,78:2.68,79:-0.61,80:-5.86,81:-4.35,82:-6.45,83:-4.45,84:-9.73,85:-4.18,86:-5.84,87:-3.55,88:-2.25,89:-6.95,90:-8.47,92:-6.69,93:-6.26,94:-2.14,95:-8.74,96:-7.86
out.trimDb=-2.92
blow.gain=1
blow.t60=0.25
blow.loHz=53.85
blow.hiHz=397.2
blow.exp=0.8
blow.lpOrder=4
blow.steepByKey=60:1,69:0
blow.gainByKey=33:-9.0,48:-9.0,52:-5.0,56:-7.0,76:-4.9,80:-4.9,84:-2.5,87:4.7,92:8.8,96:11.5
blow.leadMs=49:0.0,50:2.2,55:0.2,62:0.0,66:0.0,72:0.0,76:0.0,80:0.0,88:0.0,96:0.0
blow.leadVel0=70
blow.leadVel1=90
decay.fastShareByKey=71:0.00,72:0.50,73:0.50,74:0.50,75:0.50,76:0.50,77:0.65,78:0.65,79:0.50,80:0.65,81:0.50,82:0.50,83:0.50,84:0.50,85:0.50,86:0.50,87:0.50,88:0.50,89:0.50,90:0.50,92:0.50,93:0.50,94:0.00,95:0.00,96:0.00
decay.fastT60ByKey=71:0.88,72:0.88,73:0.88,74:0.88,75:0.88,76:0.88,77:1.15,78:1.15,79:1.15,80:1.15,81:0.88,82:0.88,83:0.88,84:0.88,85:0.88,86:0.88,87:0.674,88:0.396,89:0.396,90:0.396,92:0.535,93:0.674,94:0.674,95:0.674,96:0.08
decay.fastLinear=0
vel.speed=1:0.0171,20:0.2018,40:0.4499,60:0.7255,80:1.8776,100:2.2548,115:4.1718,127:4.2777
)"};
constexpr PresetText kPianetText{R"(
# preset:pianet - calibrated with tools/analysis/calibrate.py against samples/pianet_ds (the Decent Sampler set: every key,
# one take, as v127). The reed is plucked by the sticky pad of its key (excitation=1) and read by a magnetic pickup
# (pickup.type=0); no sustain pedal: the pad is the damper. The set has one layer: the velocity behaviour is the pad's own
# (pluck.velExp, vel.min/max), no level law of a recording; velExp 1 and vel.min 0.2 m/s follow the change over the three
# layers of the Little Black EP's Pianet (level v41/v82 against v127 within 1 dB, H2/H3; docs/REEDS.md 16).
keys.lo=29
keys.hi=88
excitation=1
reed.thickness=0.0004
reed.width=0.004
reed.tipMu=0
hammer.strike=29:0.816,30:0.827,31:0.831,32:0.827,33:0.827,34:0.827,35:0.818,36:0.836,37:0.819,38:0.818,39:0.813,40:0.813,41:0.799,42:0.799,43:0.791,44:0.791,45:0.791,46:0.791,47:0.791,48:0.789,49:0.792,50:0.811,51:0.792,52:0.791,53:0.791,54:0.791,55:0.792,56:0.811,57:0.856,58:0.856,59:0.856,60:0.856,61:0.856,62:0.856,63:0.856,64:0.844,72:0.850,88:0.850
pluck.tearMm=29:1.2,60:0.6,88:0.3
pluck.padRatio=3
pluck.velExp=1
pluck.releaseMs=29:0.6,88:0.15
vel.min=0.2
vel.max=1.5
vel.gamma=1
pickup.type=0
pickup.exponent=1.5
pickup.pole=2
pickup.width=29:0.1308,30:0.1847,31:0.1694,32:0.2223,33:0.1743,34:0.2057,35:0.15,36:0.2336,37:0.2147,38:0.2266,39:0.3146,40:0.1809,41:0.2378,42:0.1926,43:0.3279,44:0.2783,45:0.2299,46:0.2433,47:0.3104,48:0.3133,49:0.2687,50:0.3758,51:0.4214,52:0.4407,53:0.3562,54:0.4274,55:0.3347,56:0.4377,57:0.4279,58:0.4938,59:0.5158,60:0.8191,61:0.7105,62:0.7257,63:0.5023,64:1.126,65:0.7344,66:1.184,67:1.05,68:1.371,69:1.024,70:1.423,71:1.215,72:1.551,73:1.6,74:1.708,75:2.228,76:2.699,77:3.474,78:3.04,79:2.611,80:3.078,81:2.594,82:3.087,83:2.836,84:2.591,85:4.101,86:3.759,87:3.679,88:3.157
pickup.offset=29:-0.8528,30:-1.309,31:-1.453,32:-1.569,33:-1.098,34:-1.493,35:-1.367,36:-1.529,37:-1.482,38:-1.355,39:-1.029,40:-0.9843,41:-1.489,42:-1.377,43:-1.469,44:-1.414,45:-1.498,46:-1.466,47:-1.201,48:-1.238,49:-1.484,50:-1.271,51:-1.345,52:-1.649,53:-1.455,54:-1.29,55:-1.54,56:-1.536,57:-1.24,58:-1.376,59:-1.51,60:-1.503,61:-1.471,62:-1.493,63:-1.39,64:-1.414,65:-1.289,66:-1.411,67:-1.403,68:-1.401,69:-1.537,70:-1.4,71:-1.42,72:-1.409,73:-1.412,74:-1.418,75:-1.419,76:-1.384,77:-1.421,78:-1.378,79:-1.427,80:-1.525,81:-1.438,82:-1.454,83:-1.468,84:-1.457,85:-1.58,86:-1.505,87:-1.487,88:-1.398
decay.t60=29:66,30:43.1,31:35.3,32:31.6,33:36.9,34:24.8,35:22.8,36:18.8,37:17.9,38:19.3,39:20,40:19.9,41:14.3,42:14.7,43:14.7,44:14,45:11.1,46:10.7,47:11.1,48:11.3,49:11.3,50:11.5,51:11.5,52:12.3,53:12.3,54:11.6,55:8.96,56:8.96,57:9.5,58:9.5,59:8.96,60:8.62,61:8.96,62:8.12,63:8.12,64:6.15,65:5.49,66:5.49,67:5.47,68:5.92,69:5.15,70:5.04,71:5.04,72:5.04,73:2.9,74:2.9,75:3.26,76:3.4,77:3.87,78:3.87,79:2.72,80:2.31,81:2.31,82:3.06,83:3.06,84:2.55,85:2.55,86:2.42,87:1.73,88:1.09
decay.release=31:0.206,38:0.159,45:0.284,52:0.449,59:0.319,66:0.447,73:0.494,80:0.823,87:0.935
damper.pos=0.85
damper.pedal=0
human.speed=0.05
human.contact=0.08
keys.tuneByKey=29:-2.3,30:6.0,31:4.4,32:3.5,33:5.5,34:3.5,35:2.2,36:1.6,37:3.6,38:2.7,39:5.5,40:7.7,41:1.4,42:0.1,43:0.5,44:0.1,45:-0.1,46:3.7,47:1.0,48:3.5,49:0.7,50:23.9,51:2.6,52:3.8,53:0.5,54:1.0,55:5.2,56:-0.4,57:1.5,58:1.7,59:1.4,60:0.0,61:1.5,62:-0.6,63:-1.1,64:-4.5,65:3.5,66:2.9,67:1.9,68:2.2,69:2.1,70:2.3,71:2.6,72:2.2,73:3.7,74:4.9,75:5.2,76:3.1,77:6.6,78:4.0,79:4.7,80:7.7,81:6.9,82:5.0,83:6.7,84:5.7,85:7.3,86:4.8,87:3.6,88:6.8
out.keyGainDb=29:-1.70,30:-4.89,31:-3.42,32:-4.62,33:-1.54,34:-1.95,35:1.95,36:-1.45,37:-1.80,38:-0.02,39:-4.18,40:0.47,41:2.09,42:5.81,43:0.53,44:2.85,45:-0.52,46:-0.72,47:-4.33,48:-2.43,49:0.50,50:-3.64,51:-3.70,52:-1.50,53:0.78,54:0.02,55:2.51,56:-0.29,57:-3.92,58:-2.75,59:4.52,60:-2.96,61:0.28,62:0.64,63:2.67,64:-3.27,65:4.35,66:-2.37,67:0.18,68:-0.81,69:4.13,70:-1.83,71:1.04,72:-0.28,73:0.58,74:2.60,75:1.60,76:0.19,77:1.31,78:0.25,79:0.23,80:-1.54,81:0.39,82:0.38,83:1.70,84:3.09,85:-2.91,86:0.93,87:-1.22,88:-1.11
damper.knock=0.01285
damper.knockPeriods=0.5
damper.knockByKey=
out.trimDb=2.25
blow.gain=0.8692
blow.t60=0.3
blow.loHz=63.51
blow.hiHz=150
blow.exp=2.92
blow.gainByKey=29:0.0,48:0.0,52:10.9,55:11.1,59:10.7,62:12.9,65:10.7,68:10.7,72:9.5,75:8.1,78:6.0,81:4.5,85:5.9,88:5.2
)"};

constexpr Preset kPresets[] = {
    {"wurli", "Wurlitzer 200A", kWurliText.text},
    {"pianet", "Hohner Pianet T", kPianetText.text},
};

} // namespace

// ---------------------------------------------------------------------------------------
// Defaults: base values, then the default instrument (first preset)
// ---------------------------------------------------------------------------------------
Params::Params(std::nullptr_t) {
    // register trends only; the calibrated values per key come from the preset text
    t60 = Curve{{29, 20}, {64, 6}, {96, 1.5f}};

    strikePos   = Curve{{28, 0.12f}, {64, 0.18f}, {100, 0.28f}};
    hammerMass  = Curve{{28, 0.0006f}, {64, 0.0004f}, {100, 0.00025f}};
    contactMs   = Curve{{28, 12.135f}, {34, 8.581f}, {40, 6.067f}, {46, 3.861f}, {52, 2.427f},
                        {58, 1.716f}, {64, 1.213f}, {70, 0.858f}, {76, 0.607f}, {82, 0.429f},
                        {88, 0.303f}, {94, 0.215f}, {100, 0.152f}};
    hammerAlpha = Curve{{28, 1.7f}, {100, 2.0f}};
    pluckTearMm    = Curve{{29, 1.0f}, {96, 0.3f}};
    pluckReleaseMs = Curve{{29, 0.5f}, {96, 0.15f}};

    pickupWidth  = Curve{{29, 1.0f}, {96, 1.0f}};
    pickupOffset = Curve{{29, -0.5f}, {96, -0.5f}};

    releaseT60 = Curve{{29, 0.3f}, {64, 0.15f}, {96, 0.06f}};
    keyGainDb  = Curve{{29, 0.0f}, {96, 0.0f}};
}

Params::Params() : Params(nullptr) {
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
    {"voicing.hammerHardness", &Voicing::hammerHardness, "% felt hardness (+100 = half contact time) or pad tear (+100 = half the time), brighter"},
    {"voicing.hammerMass",     &Voicing::hammerMass,     "% hammer weight, or adhesion of the pad"},
    {"voicing.strikePoint",    &Voicing::strikePoint,    "% contact point of hammer or pad along the reed"},
    {"voicing.reedGauge",      &Voicing::reedGauge,      "% reed thickness (the designer re-tunes by the length)"},
    {"voicing.pickupGap",      &Voicing::pickupGap,      "% slot or field width, wider = softer"},
    {"voicing.pickupOffset",   &Voicing::pickupOffset,   "x0 / width, added to the preset (live: mod wheel)"},
    {"voicing.damperRelease",  &Voicing::damperRelease,  "% decay time with the damper on"},
};

const ScalarEntry kScalars[] = {
    {"tuning.a4",        &Params::a4,             "Hz"},
    {"keys.lo",          &Params::keysLo,         "lowest key of the instrument (MIDI)"},
    {"keys.hi",          &Params::keysHi,         "highest key of the instrument (MIDI)"},
    {"reed.thickness",   &Params::reedThickness,  "m, in the swing direction"},
    {"reed.width",       &Params::reedWidth,      "m"},
    {"reed.youngs",      &Params::reedYoungs,     "Pa"},
    {"reed.density",     &Params::reedDensity,    "kg/m^3"},
    {"reed.tipMu",       &Params::tipMu,          "tuning weight / reed mass"},
    {"reed.tipPos",      &Params::tipPos,         "weight position / reed length"},
    {"reed.lossHf",      &Params::lossHf,         "higher-mode loss coefficient"},
    {"reed.lossExp",     &Params::lossExp,        "higher-mode loss exponent"},
    {"reed.lossHf2",     &Params::lossHf2,        "quadratic higher-mode loss"},
    {"excitation",       &Params::excitation,     "0 hammer (Wurlitzer), 1 pad plucks the reed (Pianet)"},
    {"hammer.lambda",    &Params::hammerLambda,   "s/m, contact hysteresis"},
    {"hammer.substeps",  &Params::hammerSubsteps, "sub-steps during contact"},
    {"decay.fastLinear",     &Params::fastLinear,     "share of that part read at the pickup's slope at rest, 0..1"},
    {"hammer.speedHardening", &Params::hammerSpeedHardening,
                              "% extra hardness above 2 m/s, full at 3 m/s (0 = off)"},
    {"pluck.padRatio",   &Params::pluckPadRatio,  "stiffness of the pad's foam / static stiffness of the reed at the pad"},
    {"pluck.velExp",     &Params::pluckVelExp,    "adhesion ~ (key speed / 1 m/s)^velExp"},
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
    {"human.speed",      &Params::humanSpeed,     "sigma of ln speed per strike (0 = off)"},
    {"human.contact",    &Params::humanContact,   "sigma of ln contact stiffness or adhesion per strike (0 = off)"},
    {"human.seed",       &Params::humanSeed,      "seed of the strike variation"},
    {"vel.min",          &Params::velMin,         "m/s"},
    {"vel.max",          &Params::velMax,         "m/s"},
    {"vel.gamma",        &Params::velGamma,       "curve exponent"},
    {"pickup.type",      &Params::pickupType,     "0 magnetic; electrostatic: 1 plate, 2 slot, 3 plates subtracted, 4 slot table (200A)"},
    {"pickup.exponent",  &Params::pickupExponent, "magnetic: flux falloff exponent"},
    {"pickup.warp",      &Params::pickupWarp,     "1/mm, quadratic warp of the table's penetration axis (0: off)"},
    {"pickup.pole",      &Params::pickupPole,     "magnetic: half-width of the pole face in widths (0: a point)"},
    {"pickup.comb",      &Params::pickupComb,     "type 4: length of the reed in the comb / reed length, the law averaged along it (0: one cross-section)"},
    {"pickup.axial",     &Params::pickupAxial,    "where the pickup reads the reed / length (1 = tip)"},
    {"pickup.hpfHz",     &Params::pickupHpfHz,    "Hz, type 4: high pass behind the capacitance law (0 = off)"},
    {"pickup.onsetMs",   &Params::pickupOnsetMs,  "ms, glide of the strike high pass (pickup.onsetMul)"},
    {"damper.pos",       &Params::damperPos,      "contact point / length"},
    {"damper.rampMs",    &Params::damperRampMs,   "ms"},
    {"damper.pedal",     &Params::damperPedal,    "1: CC64 lifts the dampers, 0: no sustain pedal"},
    {"damper.pedalLost", &Params::damperPedalLost, "CC64/127: dampers stay on below"},
    {"damper.pedalFull", &Params::damperPedalFull, "CC64/127: dampers are off above"},
    {"damper.knock",     &Params::damperKnock,    "impulse of the damper landing / (hammer mass x 1 m/s) (0 = off)"},
    {"damper.knockPeriods", &Params::damperKnockPeriods, "duration of that blow in periods of the fundamental (0 = an impulse)"},
    {"mech1.hz",         &Params::mech1Hz,        "Hz, frame/action mode driven by the excitation"},
    {"mech1.t60",        &Params::mech1T60,       "s"},
    {"mech1.gain",       &Params::mech1Gain,      "coupling into the output (0 = off)"},
    {"mech2.hz",         &Params::mech2Hz,        "Hz"},
    {"mech2.t60",        &Params::mech2T60,       "s"},
    {"mech2.gain",       &Params::mech2Gain,      "coupling (0 = off)"},
    {"mech3.hz",         &Params::mech3Hz,        "Hz"},
    {"mech3.t60",        &Params::mech3T60,       "s"},
    {"mech3.gain",       &Params::mech3Gain,      "coupling (0 = off)"},
    {"blow.gain",        &Params::blowGain,       "RMS of the strike's noise burst re the voice level at 1 m/s (0 = off)"},
    {"blow.t60",         &Params::blowT60,        "s, decay of the burst"},
    {"blow.loHz",        &Params::blowLoHz,       "Hz, high pass of the burst"},
    {"blow.hiHz",        &Params::blowHiHz,       "Hz, low pass of the burst"},
    {"blow.leadVel0",    &Params::blowLeadVel0,   "velocity below which the hammer strikes at note-on (no lead)"},
    {"blow.leadVel1",    &Params::blowLeadVel1,   "velocity from which the full lead applies"},
    {"blow.lead",        &Params::blowLead,       "share of the lead before the hammer strikes, 0..1 (0: no lead, no latency)"},
    {"blow.exp",         &Params::blowExp,        "the burst grows with (speed / 1 m/s)^exp"},
    {"blow.lpOrder",     &Params::blowLpOrder,    "order of the burst's low pass, 2 or 4"},
    {"mech.exp",         &Params::mechExp,        "exponent of the mech modes over the force (1 = proportional)"},
    {"elec.lpfHz",       &Params::lpfHz,          "Hz"},
    {"elec.lpfQ",        &Params::lpfQ,           ""},
    {"elec.hpfHz",       &Params::hpfHz,          "Hz"},
    {"out.gain",         &Params::masterGain,     "linear"},
    {"out.trimDb",       &Params::trimDb,         "dB level trim of the instrument, apart from out.gain"},
    {"out.voiceRms",     &Params::voiceRms,       "auto-voicing target RMS"},
    {"num.oversampling", &Params::oversampling,   "1 or 2"},
    {"num.femElements",  &Params::femElements,    "beam elements"},
    {"num.maxModes",     &Params::maxModes,       "modes per key, 1..16"},
    {"num.maxVoices",    &Params::maxVoices,      "polyphony limit, 0 = all keys"},
    {"num.sleepDb",      &Params::sleepDb,        "dB"},
};

const CurveEntry kCurves[] = {
    {"decay.t60",      &Params::t60,          "s, fundamental"},
    {"reed.tipMuByKey", &Params::tipMuByKey,  "tuning weight / reed mass per key (empty: reed.tipMu)"},
    {"reed.lossHf2ByKey", &Params::lossHf2ByKey, "quadratic high-mode loss per key"},
    {"reed.lossHfByKey", &Params::lossHfByKey, "high-mode loss per key (empty: reed.lossHf)"},
    {"reed.lossExpByKey", &Params::lossExpByKey, "loss exponent per key (empty: reed.lossExp)"},
    {"reed.glide",     &Params::reedGlide,    "cents per mm of swing (pitch drop at large amplitude, empty: off)"},
    {"decay.release",  &Params::releaseT60,   "s, fundamental with damper"},
    {"decay.fastShareByKey", &Params::fastShareByKey, "amplitude share of the fundamental's fast-decaying part, 0..1 (empty: 0)"},
    {"decay.fastT60ByKey",   &Params::fastT60ByKey,   "s, decay of that part"},
    {"damper.harpT60", &Params::damperHarpT60, "s, uniform extra decay under the felt (empty: off)"},
    {"damper.velDb", &Params::damperVelDb, "dB damper effectiveness over velocity (empty: off)"},
    {"damper.knockByKey", &Params::damperKnockByKey, "dB of the damper's blow by key (empty: 0)"},
    {"rel1.gainByKey", &Params::rel1ByKey, "dB release thump mode 1 by key (empty: 0)"},
    {"rel2.gainByKey", &Params::rel2ByKey, "dB release thump mode 2 by key (empty: 0)"},
    {"keys.tuneByKey", &Params::keysTuneByKey, "cent tuning offset by key, on top of keys.tuneCents (empty: 0)"},
    {"blow.leadMs",    &Params::blowLeadMs, "ms per key: the hammer strikes this long after the knock of a loud note (empty: none)"},
    {"blow.gainByKey", &Params::blowByKey, "dB of the strike's noise burst by key (empty: 0)"},
    {"blow.steepByKey", &Params::blowSteepByKey, "share of the burst through the steep low pass by key, 0..1 (empty: 1)"},
    {"mech.gainByKey", &Params::mechByKey, "dB coupling of the force into the mech modes by key (empty: 0)"},
    {"mech.expByKey",   &Params::mechExpByKey,  "exponent of the mech modes over the force per key"},
    {"hammer.strike",  &Params::strikePos,    "contact point of hammer or pad / length"},
    {"hammer.mass",    &Params::hammerMass,   "kg"},
    {"hammer.contact", &Params::contactMs,    "ms at 1 m/s (rigid wall)"},
    {"hammer.alpha",   &Params::hammerAlpha,  "force exponent"},
    {"hammer.speedHardeningByKey", &Params::hammerSpeedHardeningByKey,
                                   "% adjustment to high-speed hardness by key"},
    {"pluck.tearMm",   &Params::pluckTearMm,  "mm, static deflection of the reed at the pad when the bond tears (at 1 m/s)"},
    {"pluck.releaseMs", &Params::pluckReleaseMs, "ms, time of the tear (shorter = brighter)"},
    {"pickup.width",   &Params::pickupWidth,  "mm"},
    {"pickup.offset",  &Params::pickupOffset, "x0 / width"},
    {"pickup.onsetMul", &Params::pickupOnsetMul, "multiple of f1 of the strike high pass per key (empty/0: off)"},
    {"out.keyGainDb",  &Params::keyGainDb,    "dB"},
    {"vel.speed",      &Params::velSpeed,     "m/s over MIDI velocity (empty: vel.min/max/gamma)"},
    {"vel.gainDb",     &Params::velGainDb,    "dB over MIDI velocity, level of a strike (empty: 0)"},
    {"vel.gainSlope",  &Params::velGainSlope, "dB per velocity step below 127, per key (empty: 0)"},
    {"vel.speedCapByKey", &Params::velSpeedCapByKey, "m/s per key, soft limit of the speed (empty: none)"},
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

// The public parameters: the voicing controls of the physical model, the mechanical noises, the
// output level and the polyphony - what a player sets (host, pmp-render --set). The
// raw model parameters belong to the presets, the calibration and the analysis tools: the C API
// and pmp-render --internal set them, a plain set() refuses them.
bool Params::isPublic(const std::string& name) {
    static const char* const kPrefixes[] = {"rel1.", "rel2.", "pedal."};
    static const char* const kNames[] = {
        "voicing.hammerHardness", "voicing.hammerMass", "voicing.strikePoint", "voicing.reedGauge",
        "voicing.pickupGap", "voicing.pickupOffset", "voicing.damperRelease",
        "decay.release",
        "damper.rampMs", "damper.pedalLost", "damper.pedalFull",                          // the pedal curve
        "damper.knock",                                                                  // the damper's blow
        "blow.lead",                                                                     // the lead (latency) switch
        "human.release", "human.pedal",                                                  // variation of the noises
        "vel.gainDb",                                                                    // level of a strike over velocity
        "out.gain", "out.trimDb",
        "num.maxVoices",                                                                 // the voice limit
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
