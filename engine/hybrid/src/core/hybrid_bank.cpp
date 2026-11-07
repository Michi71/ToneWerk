// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// hybrid_bank.cpp - validation and access of a hybrid bank image, ported from pmpiano
// (src/core/piano_hybrid.cpp; bank versions 5 and 6 - version 4 and older are refused, the
// file readers stayed in pmpiano: synthkeys embeds the image as a compiled-in byte array)
#include "hybrid_bank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pmp {
namespace {

constexpr size_t kHeader = 256;
constexpr uint32_t kEndian = 0x01020304u;
constexpr size_t kMeta = 1024;       // version 6: the metadata block after the header

static_assert(sizeof(HybridLoopRec5) == 248, "HybridLoopRec5 is the 248-byte record of the bank builder");
static_assert(sizeof(HybridKeyRec) == 64, "HybridKeyRec fills the 64-byte key stride");

// the loop points and envelope of one band at one velocity (start, len, phase, a1, s1, a2, s2); maxLen: longest loop [samples]
bool loopPointsOk(const float* w, double h, double segLen, double minLen, double maxLen) {
    const double start = w[0], len = w[1], ph = w[2];
    if (!std::isfinite(start) || !std::isfinite(len) || !std::isfinite(ph) || !(len >= minLen) || !(len <= maxLen) || !(ph >= 0) ||
        !(ph < len) || !(start - h - 2 >= 0) || !(start + len + h + 3 <= segLen))
        return false;
    const float a1 = w[3], s1 = w[4], a2 = w[5], s2 = w[6];
    return a1 >= 0 && a2 >= 0 && s1 > 0 && s2 > 0 && std::isfinite(a1) && std::isfinite(a2) && s1 <= 1e5f && s2 <= 1e5f;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
double rdD(const uint8_t* p) { double v; std::memcpy(&v, p, 8); return v; }

bool fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}

// a text field of the metadata block: NUL-terminated within its size; ascii: only [a-z0-9_-] (the ids)
bool metaText(const uint8_t* p, size_t n, bool ascii, std::string* out) {
    const void* end = std::memchr(p, 0, n);
    if (!end) return false;
    out->assign(reinterpret_cast<const char*>(p), size_t(static_cast<const uint8_t*>(end) - p));
    if (ascii)
        for (char c : *out)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

// the metadata block of a version 6 bank (pmpiano docs/BANKBUILD.md section 2.1)
bool readMeta(const uint8_t* b, HybridBankMeta* m, std::string* err) {
    if (std::memcmp(b, "PMPHMETA", 8) != 0) return fail(err, "hybrid bank: bad metadata magic");
    if (rd32(b + 764) != crc32(b, 764)) return fail(err, "hybrid bank: metadata CRC");
    if (rd32(b + 8) != 1) return fail(err, "hybrid bank: unsupported metadata version");
    const uint32_t type = rd32(b + 12), n = rd32(b + 272);
    if (type > HybridBankMeta::Harpsichord) return fail(err, "hybrid bank: unknown instrument type");
    if (!metaText(b + 16, 32, true, &m->id) || m->id.empty() || !metaText(b + 48, 64, false, &m->name) ||
        !metaText(b + 112, 32, true, &m->base) || !metaText(b + 144, 96, false, &m->source) || !metaText(b + 248, 24, false, &m->tool))
        return fail(err, "hybrid bank: bad metadata text");
    if (n >= 488 || b[276 + n] != 0 || std::memchr(b + 276, 0, n)) return fail(err, "hybrid bank: bad metadata overlay");
    m->overlay.assign(reinterpret_cast<const char*>(b + 276), n);
    m->type = int(type);
    m->flags = rd32(b + 240);
    m->date = rd32(b + 244);
    return true;
}

} // namespace

bool HybridBank::attach(const uint8_t* d, size_t size, std::string* err) {
    keys_ = nullptr;
    meta_ = HybridBankMeta();
    loopKeys_ = loopPcm_ = nullptr;
    loopVel_ = nullptr;
    if (size < kHeader) return fail(err, "hybrid bank: too small");
    if (std::memcmp(d, "PMPHBANK", 8) != 0) return fail(err, "hybrid bank: bad magic");
    const uint32_t version = rd32(d + 8);
    if (version < 5 || version > 7) return fail(err, "hybrid bank: unsupported version (this build reads 5, 6 and 7)");
    const size_t start = version >= 6 ? kHeader + kMeta : kHeader;   // the first byte a section may use
    if (size < start) return fail(err, "hybrid bank: too small");
    if (rd32(d + 12) != kEndian) return fail(err, "hybrid bank: wrong endianness");
    if (rd32(d + 252) != crc32(d, 252)) return fail(err, "hybrid bank: header CRC");
    if ((reinterpret_cast<uintptr_t>(d) & 15) != 0) return fail(err, "hybrid bank: image not 16-byte aligned");
    const uint32_t rate = rd32(d + 16), first = rd32(d + 20), last = rd32(d + 24), nlay = rd32(d + 28), maxm = rd32(d + 32),
                   nAtt = rd32(d + 36), nE = rd32(d + 40), nvel = rd32(d + 44);
    const uint32_t offKey = rd32(d + 112), offMode = rd32(d + 116), offZ = rd32(d + 120), offPcm = rd32(d + 124), offF = rd32(d + 128),
                   total = rd32(d + 132);
    if (total != size) return fail(err, "hybrid bank: size does not match the header");
    if ((nlay < 2 || nlay > uint32_t(kHybridMaxLayers)) || maxm != kHybridModes ||
        (version <= 6 ? nvel != kHybridVelocities : (nvel < 2 || nvel > uint32_t(kHybridMaxVelocityAnchors))))
        return fail(err, "hybrid bank: layout (layers, modes, velocities) is not the one this build reads");
    if (version >= 7 && nlay == 5) return fail(err, "hybrid bank: a version 7 bank uses at most four layers");
    nVel_ = int(nvel);
    const uint32_t v7flags = version >= 7 ? rd32(d + 248) : 0;
    filterRows_ = int(v7flags & 1);
    loopRows_ = version >= 7 ? int((v7flags >> 8) & 0xFF) : kHybridVelocities;
    if ((version <= 6 && v7flags != 0) || loopRows_ < 2 || loopRows_ > kHybridVelocities ||
        (loopRows_ != kHybridVelocities && loopRows_ != nVel_))
        return fail(err, "hybrid bank: bad velocity table flags");
    if (version >= 7) {
        for (int i = 0; i < nVel_; ++i) {
            velAnchor_[i] = d[176 + i];
            if (velAnchor_[i] < 1 || velAnchor_[i] > kHybridVelocities || (i && velAnchor_[i] <= velAnchor_[i - 1]))
                return fail(err, "hybrid bank: bad velocity anchors");
        }
    } else {
        for (int i = 0; i < kHybridVelocities; ++i) velAnchor_[i] = uint8_t(i + 1);
    }
    if (rate < 8000 || rate > 192000 || last < first || last - first > 127 || nE < 16 || nAtt <= nE || nAtt > 1u << 20)
        return fail(err, "hybrid bank: bad header values");
    const uint64_t nk = last - first + 1;
    const uint64_t endKey = offKey + HybridBank::kKeyStride * nk, endMode = offMode + 16ull * kHybridModes * nk,
                   endZ = offZ + 8ull * kHybridModes * nVel_ * nk,
                   endF = offF + 40ull * (filterRows_ ? 1 : nVel_) * nk;
    uint64_t frames = 0;                                   // every key its own N_att
    if (endKey <= size)
        for (uint64_t i = 0; i < nk; ++i) frames += uint64_t(reinterpret_cast<const HybridKeyRec*>(d + offKey + HybridBank::kKeyStride * i)->nAtt) * nlay;
    const uint64_t endPcm = offPcm + 2ull * frames;
    if (endKey > size || endMode > size || endZ > size || endF > size || endPcm > size || offKey < start)
        return fail(err, "hybrid bank: section outside the image");
    if (((offKey | offMode | offZ | offF | offPcm) & 15) != 0) return fail(err, "hybrid bank: section not aligned");
    if (rd32(d + 232) != crc32(d + kHeader, size - kHeader)) return fail(err, "hybrid bank: payload CRC");
    // content
    for (uint64_t i = 0; i < nk; ++i) {
        const HybridKeyRec* r = reinterpret_cast<const HybridKeyRec*>(d + offKey + HybridBank::kKeyStride * i);
        if (r->modes > kHybridModes || !(r->releaseT60 > 0.01f) || !std::isfinite(r->releaseT60) || !std::isfinite(r->trimDb) ||
            std::fabs(r->trimDb) > 40.0f)
            return fail(err, "hybrid bank: bad key record");
        for (uint32_t l = 0; l < nlay; ++l)
            if (!(r->pcmScale[l] > 0.0) || !std::isfinite(r->pcmScale[l])) return fail(err, "hybrid bank: bad PCM scale");
        if (r->nE < 16 || r->nAtt <= r->nE || r->nAtt > nAtt) return fail(err, "hybrid bank: bad crossfade of a key");
        const double* pol = reinterpret_cast<const double*>(d + offMode + 16ull * kHybridModes * i);
        for (int m = 0; m < r->modes; ++m) {
            const double sg = pol[2 * m], om = pol[2 * m + 1];
            if (!(sg > 0.0) || !std::isfinite(sg) || !std::isfinite(om) || std::fabs(om) >= 0.9 * 3.14159265358979 * rate)
                return fail(err, "hybrid bank: bad pole");
        }
    }
    const float* z = reinterpret_cast<const float*>(d + offZ);
    for (uint64_t i = 0; i < nk * size_t(nVel_) * kHybridModes * 2; ++i)
        if (!std::isfinite(z[i])) return fail(err, "hybrid bank: non-finite mode state");
    const double* f = reinterpret_cast<const double*>(d + offF);
    for (uint64_t i = 0; i < nk * size_t(filterRows_ ? 1 : nVel_); ++i) {
        const double* c = f + 5 * i;
        for (int q = 0; q < 5; ++q)
            if (!std::isfinite(c[q])) return fail(err, "hybrid bank: non-finite filter");
        if (!(std::fabs(c[4]) < 1.0) || !(std::fabs(c[3]) < 1.0 + c[4])) return fail(err, "hybrid bank: unstable filter");   // a1, a2 of the biquad
    }
    // the high-band loop (pmpiano docs/PIANO_HYBRID_LOOP_DESIGN.md sections 4 and 13)
    const uint32_t offLK = rd32(d + 236), offLP = rd32(d + 240), offLV = rd32(d + 244);
    if (((offLK | offLP | offLV) & 15) != 0 || offLK < start || offLK + kLoopStride5 * nk > size || offLP > size || offLV > size)
        return fail(err, "hybrid bank: loop section outside the image");
    int seam = 0;                                  // one seam for the instrument, like the export writes
    for (uint64_t i = 0; i < nk; ++i) {
        const HybridLoopRec5* r = reinterpret_cast<const HybridLoopRec5*>(d + offLK + kLoopStride5 * i);
        if (r->bands == 0) continue;
        const HybridKeyRec* kr = reinterpret_cast<const HybridKeyRec*>(d + offKey + HybridBank::kKeyStride * i);
        if (r->bands > kHybridLoopBands || r->seam < 2 || (r->seam & 1) || r->seam > 1024 || (r->pcmOffset & 1) ||
            (seam && r->seam != seam))
            return fail(err, "hybrid bank: bad loop record");
        seam = r->seam;
        if (r->segLen < 64 || uint64_t(r->seg0) + r->segLen > kr->nAtt) return fail(err, "hybrid bank: bad loop piece range");
        if (!(r->split > 0) || !std::isfinite(r->split)) return fail(err, "hybrid bank: bad loop bands");
        for (int b = 0; b < r->bands; ++b) {
            if (!(r->lo[b] > 0) || !(r->hi[b] > r->lo[b]) || !(r->hi[b] < 0.5f * float(rate)) ||
                (b > 0 && !(r->lo[b] >= r->hi[b - 1] - 1e-3f)))
                return fail(err, "hybrid bank: bad loop bands");
            for (uint32_t l = 0; l < nlay; ++l)
                if (!(r->scale[l][b] > 0) || !std::isfinite(r->scale[l][b])) return fail(err, "hybrid bank: bad loop scale");
        }
        const uint64_t nv = 7 * uint64_t(r->bands);
        if (offLP + uint64_t(r->pcmOffset) + 2ull * nlay * r->bands * r->segLen > size ||
            offLV + 4ull * (uint64_t(r->velOffset) + uint64_t(loopRows_) * nv) > size)
            return fail(err, "hybrid bank: loop data outside the image");
        const float* v = reinterpret_cast<const float*>(d + offLV) + r->velOffset;
        for (int q = 0; q < kHybridVelocities; ++q) {              // the player's interpolated row
            float w[7 * kHybridLoopBands] = {};
            if (loopRows_ == kHybridVelocities) {
                for (size_t k = 0; k < nv; ++k) w[k] = v[size_t(q) * nv + k];
            } else {
                int i0, i1; float t;
                velocityRow(q, i0, i1, t);
                for (size_t k = 0; k < nv; ++k) {
                    const float a = v[size_t(i0) * nv + k], b2 = v[size_t(i1) * nv + k];
                    w[k] = a + t * (b2 - a);
                }
            }
            for (int b = 0; b < r->bands; ++b)
                if (!loopPointsOk(w + 7 * b, r->seam / 2, double(r->segLen), 0.005 * rate, 0.25 * rate))
                    return fail(err, "hybrid bank: bad loop points");
        }
    }
    loopKeys_ = d + offLK;
    loopPcm_ = d + offLP;
    loopVel_ = reinterpret_cast<const float*>(d + offLV);
    rate_ = int(rate); first_ = int(first); last_ = int(last); nAtt_ = int(nAtt); nE_ = int(nE);
    version_ = int(version);
    nLay_ = int(nlay);
    for (int l = 0; l < kHybridLayers && l < nLay_; ++l) anchors_[l] = int(rd32(d + 48 + 4 * l));
    if (nLay_ == 5) anchors_[4] = int(rd32(d + 248));       // version 5 keeps the fifth anchor where version 4 had the piece range
    level_ = rdD(d + 64); refVel_ = rdD(d + 72); gamma_ = rdD(d + 80);
    rampMs_ = rdD(d + 88); pedalLost_ = rdD(d + 96); pedalFull_ = rdD(d + 104);
    if (!(level_ > 0) || !(refVel_ > 0) || !(rampMs_ > 0) || !(pedalFull_ > pedalLost_)) return fail(err, "hybrid bank: bad parameters");
    for (int l = 0; l < nLay_ - 1; ++l)
        if (anchors_[l] >= anchors_[l + 1]) return fail(err, "hybrid bank: anchors not increasing");
    if (anchors_[0] < 1 || anchors_[nLay_ - 1] > kHybridVelocities) return fail(err, "hybrid bank: anchors out of range");
    keys_ = d + offKey;
    pcmOff_.assign(size_t(nk), 0);
    for (uint64_t i = 0, f0 = 0; i < nk; ++i) {
        pcmOff_[size_t(i)] = size_t(f0);
        f0 += uint64_t(reinterpret_cast<const HybridKeyRec*>(keys_ + HybridBank::kKeyStride * i)->nAtt) * nlay;
    }
    poles_ = reinterpret_cast<const double*>(d + offMode);
    z_ = z; filt_ = f;
    pcm_ = reinterpret_cast<const int16_t*>(d + offPcm);
    std::memcpy(revision_, d + 136, 40); revision_[40] = 0;
    std::memcpy(state_, d + 208, 24); state_[24] = 0;
    if (version >= 6 && !readMeta(d + kHeader, &meta_, err)) {
        keys_ = nullptr;
        return false;
    }
    return true;
}

// the two stored velocity rows around vel and the weight of the upper one (version 5/6: exact)
void HybridBank::velocityRow(int vel, int& i0, int& i1, float& t) const {
    vel = vel < 1 ? 1 : vel > kHybridVelocities ? kHybridVelocities : vel;
    if (nVel_ >= kHybridVelocities) { i0 = i1 = vel - 1; t = 0; return; }
    if (vel <= velAnchor_[0]) { i0 = i1 = 0; t = 0; return; }
    if (vel >= velAnchor_[nVel_ - 1]) { i0 = i1 = nVel_ - 1; t = 0; return; }
    int lo = 0;
    while (lo + 1 < nVel_ && velAnchor_[lo + 1] <= vel) ++lo;
    i0 = lo; i1 = lo + 1;
    t = float(vel - velAnchor_[i0]) / float(velAnchor_[i1] - velAnchor_[i0]);
}

void HybridBank::states(int note, int vel, float* out) const {
    int i0, i1; float t;
    velocityRow(vel, i0, i1, t);
    const size_t row = size_t(idx(note)) * size_t(nVel_);
    const float* a = z_ + (row + size_t(i0)) * kHybridModes * 2;
    if (i0 == i1 || t <= 0) {
        std::memcpy(out, a, sizeof(float) * kHybridModes * 2);
        return;
    }
    const float* b = z_ + (row + size_t(i1)) * kHybridModes * 2;
    for (int k = 0; k < kHybridModes * 2; ++k) out[k] = a[k] + t * (b[k] - a[k]);
}

const float* HybridBank::states(int note, int vel) const {
    int i0, i1; float t;
    velocityRow(vel, i0, i1, t);
    return z_ + (size_t(idx(note)) * size_t(nVel_) + size_t(i0)) * kHybridModes * 2;
}

void HybridBank::filter(int note, int vel, double* out) const {
    int i0, i1; float t;
    velocityRow(vel, i0, i1, t);
    const size_t rows = size_t(filterRows_ ? 1 : nVel_);
    const size_t row = size_t(idx(note)) * rows;
    const double* a = filt_ + (row + size_t(filterRows_ ? 0 : i0)) * 5;
    if (i0 == i1 || t <= 0 || filterRows_) {
        std::memcpy(out, a, sizeof(double) * 5);
        return;
    }
    const double* b = filt_ + (row + size_t(i1)) * 5;
    for (int k = 0; k < 5; ++k) out[k] = a[k] + double(t) * (b[k] - a[k]);
}

const double* HybridBank::filter(int note, int vel) const {
    int i0, i1; float t;
    velocityRow(vel, i0, i1, t);
    const size_t rows = size_t(filterRows_ ? 1 : nVel_);
    return filt_ + (size_t(idx(note)) * rows + size_t(filterRows_ ? 0 : i0)) * 5;
}

int HybridBank::attackFrames(int note) const {
    return int(key(note).nAtt);
}

int HybridBank::fadeStart(int note) const {
    return int(key(note).nE);
}

const HybridLoopRec5* HybridBank::loop5(int note) const {
    if (!loopKeys_ || note < first_ || note > last_) return nullptr;
    const HybridLoopRec5* r = reinterpret_cast<const HybridLoopRec5*>(loopKeys_ + size_t(note - first_) * kLoopStride5);
    return r->bands ? r : nullptr;
}

int HybridBank::loopBands(int note) const {
    if (const HybridLoopRec5* r = loop5(note)) return r->bands;
    return 0;
}

int HybridBank::loopSeam(int note) const {
    if (const HybridLoopRec5* r = loop5(note)) return r->seam;
    return 0;
}

int HybridBank::loopSegLength(int note) const {
    if (const HybridLoopRec5* r = loop5(note)) return int(r->segLen);
    return 0;
}

float HybridBank::loopScale(int note, int layer, int band) const {
    return loop5(note)->scale[layer][band];
}

const int16_t* HybridBank::loopPcm(int note, int layer, int band) const {
    const HybridLoopRec5* r = loop5(note);
    return reinterpret_cast<const int16_t*>(loopPcm_ + r->pcmOffset) + (size_t(layer) * r->bands + size_t(band)) * size_t(r->segLen);
}

void HybridBank::loopVelocity(int note, int vel, float* out) const {
    const int nb = loopBands(note);
    if (!nb) return;
    const size_t nv = 7 * size_t(nb);
    const float* base = loopVel_ + loop5(note)->velOffset;
    const int vq = vel < 1 ? 1 : vel > kHybridVelocities ? kHybridVelocities : vel;
    if (loopRows_ == kHybridVelocities) {
        std::memcpy(out, base + size_t(vq - 1) * nv, sizeof(float) * nv);
        return;
    }
    int i0, i1; float t;
    velocityRow(vq, i0, i1, t);
    const float* a = base + size_t(i0) * nv;
    if (i0 == i1 || t <= 0) {
        std::memcpy(out, a, sizeof(float) * nv);
        return;
    }
    const float* b = base + size_t(i1) * nv;
    for (size_t k = 0; k < nv; ++k) out[k] = a[k] + t * (b[k] - a[k]);
}

const float* HybridBank::loopVelocity(int note, int vel) const {
    const int vq = vel < 1 ? 1 : vel > kHybridVelocities ? kHybridVelocities : vel;
    const size_t nv = 7 * size_t(loopBands(note));
    if (loopRows_ == kHybridVelocities) return loopVel_ + loop5(note)->velOffset + size_t(vq - 1) * nv;
    int i0, i1; float t;
    velocityRow(vq, i0, i1, t);
    return loopVel_ + loop5(note)->velOffset + size_t(i0) * nv;
}

} // namespace pmp
