// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// hybrid_player.cpp - the hybrid voice: PCM attack crossfaded into computed modes,
// ported from pmpiano (src/core/piano_hybrid.cpp). Not part of the port: the parallel
// executor, the clavinet retune. The harp resonance bank is (docs/PIANO_HYBRID_RESONANCE.md).
//
// The algorithm is that of pmpiano's offline model (tools/analysis/piano_hybrid_render.py):
//   att[j]  = LP_v( w0 pcm_i[j] + w1 pcm_i+1[j] )                      j < N_att
//   z_k     = Z_k(n, v)  at j = N_e,   z_k <- a_k z_k   every sample after
//   out[j]  = G(v) ((1 - h) att + h Re sum z_k)   j < N_att,   G(v) Re sum z_k   afterwards
// with h the smoothstep over the fade [N_e, N_att]. The sustain of a key is ONE state vector
// per key: a new strike merges its mode state into it when its crossfade ends (the poles
// belong to the key, so the merge is exact), the old string is never reset. Setup (prepare)
// allocates; noteOn, noteOff, controller and process do not.
#include "hybrid_bank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace pmp {
namespace {

constexpr int kChunk = 64;
constexpr int kKnot = 32;            // loop band gains: linear between these output samples (pmpiano: piano_hybrid_loop.KNOT)
constexpr int kMaxSeam = 1024;

// A block of four modes sleeps when its energy falls below this share of the key's threshold (sleepEnergy, about -120 dB):
// -150 dB, far below what the equivalence check (-100 dB) resolves, well before the key as a whole sleeps.
constexpr double kBlockSleep = 1e-3;

// ---- the mode kernel ---------------------------------------------------------------------------------------------
// The free decay of n modes (n a multiple of 4) over c samples, four modes in one vector: z <- z a, the real parts summed.
// Register blocking: a block of four modes stays in registers over the whole chunk, the chunk's partial sums (one vector
// per sample) stay in L1; the old loop (sample outside, modes inside) loaded and stored the whole state of the key for
// every sample. The sum over the modes is lane-wise, then across the four lanes: another rounding than the ordered sum,
// not another model (pmpiano docs/PROFILING.md section 4.1). live: a bit per block; a block whose energy fell below thr
// is cleared and skipped from then on (nullptr: no sleep). y[0 .. c) receives the sum.
#if defined(__GNUC__) || defined(__clang__)
#define PMP_VEC4 1
typedef real v4r __attribute__((vector_size(4 * sizeof(real))));
// B live blocks at once: B independent chains per sample, so an in-order core (the Pi's A53) overlaps their latencies
template <int B>
static inline void runGroup(const int* blk, real* zr, real* zi, const real* ar, const real* ai, int c, v4r* acc) {
    v4r Zr[B], Zi[B], Ar[B], Ai[B];
    for (int g = 0; g < B; ++g) {
        std::memcpy(&Zr[g], zr + blk[g], sizeof(v4r));
        std::memcpy(&Zi[g], zi + blk[g], sizeof(v4r));
        std::memcpy(&Ar[g], ar + blk[g], sizeof(v4r));
        std::memcpy(&Ai[g], ai + blk[g], sizeof(v4r));
    }
    for (int s = 0; s < c; ++s) {
        v4r sum = Zr[0];
        for (int g = 1; g < B; ++g) sum += Zr[g];
        acc[s] += sum;
        for (int g = 0; g < B; ++g) {
            const v4r r = Zr[g] * Ar[g] - Zi[g] * Ai[g];
            Zi[g] = Zr[g] * Ai[g] + Zi[g] * Ar[g];
            Zr[g] = r;
        }
    }
    for (int g = 0; g < B; ++g) {
        std::memcpy(zr + blk[g], &Zr[g], sizeof(v4r));
        std::memcpy(zi + blk[g], &Zi[g], sizeof(v4r));
    }
}
// the harp (pmpiano docs/PIANO_HYBRID_RESONANCE.md): the same rotation, driven per sample in quadrature by hg x[s]
template <int B>
static inline void driveGroup(const int* blk, real* hr, real* hi, const real* ar, const real* ai, const real* hg, const real* x,
                              int c, v4r* acc) {
    v4r Hr[B], Hi[B], Ar[B], Ai[B], Hg[B];
    for (int g = 0; g < B; ++g) {
        std::memcpy(&Hr[g], hr + blk[g], sizeof(v4r));
        std::memcpy(&Hi[g], hi + blk[g], sizeof(v4r));
        std::memcpy(&Ar[g], ar + blk[g], sizeof(v4r));
        std::memcpy(&Ai[g], ai + blk[g], sizeof(v4r));
        std::memcpy(&Hg[g], hg + blk[g], sizeof(v4r));
    }
    for (int s = 0; s < c; ++s) {
        v4r sum = Hr[0];
        for (int g = 1; g < B; ++g) sum += Hr[g];
        acc[s] += sum;
        const real xs = x[s];
        for (int g = 0; g < B; ++g) {
            const v4r pi = Hr[g] * Ai[g] + Hi[g] * Ar[g];
            Hr[g] = Hr[g] * Ar[g] - Hi[g] * Ai[g];
            Hi[g] = pi + Hg[g] * xs;
        }
    }
    for (int g = 0; g < B; ++g) {
        std::memcpy(hr + blk[g], &Hr[g], sizeof(v4r));
        std::memcpy(hi + blk[g], &Hi[g], sizeof(v4r));
    }
}
#endif
// n modes (a multiple of 4) driven by x[0 .. c): y[s] receives the sum of their real parts (the same lane-wise rounding)
static void driveModes(real* hr, real* hi, const real* ar, const real* ai, const real* hg, const real* x, int n, int c, real* y) {
#if defined(PMP_VEC4)
    v4r acc[kChunk];
    for (int s = 0; s < c; ++s) acc[s] = v4r{};
    int blk[kHybridModes / 4], nb = 0;
    for (int b = 0; b < n; b += 4) blk[nb++] = b;
    int q = 0;
    for (; q + 4 <= nb; q += 4) driveGroup<4>(blk + q, hr, hi, ar, ai, hg, x, c, acc);
    for (; q + 2 <= nb; q += 2) driveGroup<2>(blk + q, hr, hi, ar, ai, hg, x, c, acc);
    for (; q < nb; ++q) driveGroup<1>(blk + q, hr, hi, ar, ai, hg, x, c, acc);
    for (int s = 0; s < c; ++s) y[s] = (acc[s][0] + acc[s][1]) + (acc[s][2] + acc[s][3]);
#else
    for (int s = 0; s < c; ++s) {
        real acc[4] = {0, 0, 0, 0};
        for (int b = 0; b < n; b += 4)
            for (int l = 0; l < 4; ++l) {
                const int m = b + l;
                acc[l] += hr[m];
                const real pi = hr[m] * ai[m] + hi[m] * ar[m];
                hr[m] = hr[m] * ar[m] - hi[m] * ai[m];
                hi[m] = pi + hg[m] * x[s];
            }
        y[s] = (acc[0] + acc[1]) + (acc[2] + acc[3]);
    }
#endif
}
static void runModes(real* zr, real* zi, const real* ar, const real* ai, int n, int c, uint32_t* live, real thr, real* y) {
#if defined(PMP_VEC4)
    v4r acc[kChunk];
    for (int s = 0; s < c; ++s) acc[s] = v4r{};
    int blk[kHybridModes / 4], nb = 0;                           // the live blocks
    for (int b = 0; b < n; b += 4)
        if (!live || (*live & (1u << (b >> 2)))) blk[nb++] = b;
    int q = 0;
    for (; q + 4 <= nb; q += 4) runGroup<4>(blk + q, zr, zi, ar, ai, c, acc);
    for (; q + 2 <= nb; q += 2) runGroup<2>(blk + q, zr, zi, ar, ai, c, acc);
    for (; q < nb; ++q) runGroup<1>(blk + q, zr, zi, ar, ai, c, acc);
    if (live)
        for (q = 0; q < nb; ++q) {
            const int b = blk[q];
            real e = 0;
            for (int l = 0; l < 4; ++l) e += zr[b + l] * zr[b + l] + zi[b + l] * zi[b + l];
            if (!(e > thr)) {
                for (int l = 0; l < 4; ++l) zr[b + l] = zi[b + l] = 0;
                *live &= ~(1u << (b >> 2));
            }
        }
    for (int s = 0; s < c; ++s) y[s] = (acc[s][0] + acc[s][1]) + (acc[s][2] + acc[s][3]);
#else
    real acc[kChunk][4];
    for (int s = 0; s < c; ++s) acc[s][0] = acc[s][1] = acc[s][2] = acc[s][3] = 0;
    for (int b = 0; b < n; b += 4) {
        const uint32_t bit = 1u << (b >> 2);
        if (live && !(*live & bit)) continue;
        real Zr[4], Zi[4], Ar[4], Ai[4];
        for (int l = 0; l < 4; ++l) { Zr[l] = zr[b + l]; Zi[l] = zi[b + l]; Ar[l] = ar[b + l]; Ai[l] = ai[b + l]; }
        for (int s = 0; s < c; ++s)
            for (int l = 0; l < 4; ++l) {
                acc[s][l] += Zr[l];
                const real r = Zr[l] * Ar[l] - Zi[l] * Ai[l];
                Zi[l] = Zr[l] * Ai[l] + Zi[l] * Ar[l];
                Zr[l] = r;
            }
        if (live) {
            real e = 0;
            for (int l = 0; l < 4; ++l) e += Zr[l] * Zr[l] + Zi[l] * Zi[l];
            if (!(e > thr)) { for (int l = 0; l < 4; ++l) Zr[l] = Zi[l] = 0; *live &= ~bit; }
        }
        for (int l = 0; l < 4; ++l) { zr[b + l] = Zr[l]; zi[b + l] = Zi[l]; }
    }
    for (int s = 0; s < c; ++s) y[s] = (acc[s][0] + acc[s][1]) + (acc[s][2] + acc[s][3]);
#endif
}

} // namespace

// -------------------------------------------------------------------------------------- player

struct HybridPlayer::Impl {
    // One high-band loop (pmpiano docs/PIANO_HYBRID_LOOP_DESIGN.md sections 2 and 13). The output sample m counts from
    // the strike's N_e. Every band b loops on its own: its segment p covers [b0, b0n), it reads the band at base + m,
    // base = start + phase - p len, with one Hermite FIR of the constant fraction of base; the first `seam` samples of a
    // segment (p >= 1) blend the previous pass run on (base + len) into it.
    struct Loop {
        bool used = false, fadeOut = false, frozen = false;
        int bands = 0, idx = 0;                  // idx: samples into the crossfade (fade in, or out for the old loop)
        int nF = 1;                              // length of its strike's crossfade
        real frozenW = 1;                        // the weight of a loop whose attack was stolen after N_e
        const int16_t* x0[kHybridLoopBands] = {};
        const int16_t* x1[kHybridLoopBands] = {};
        real c0[kHybridLoopBands] = {}, c1[kHybridLoopBands] = {};   // w0 scale0 / 32767, w1 scale1 / 32767
        double e1[kHybridLoopBands] = {}, e2[kHybridLoopBands] = {}; // the two exponentials at knot kb
        double r1[kHybridLoopBands] = {}, r2[kHybridLoopBands] = {}; // their ratio per knot
        double start[kHybridLoopBands] = {}, len[kHybridLoopBands] = {}, ph[kHybridLoopBands] = {};
        long p[kHybridLoopBands] = {}, b0[kHybridLoopBands] = {}, b0n[kHybridLoopBands] = {};
        long kb = 0, m = 0;
        real gain = 1, dmp = 1;                  // loudness of the strike, damper decay since N_e
        uint64_t age = 0;                        // the strike (its attack voice)
    };
    struct Key {
        alignas(32) real zr[kHybridModes], zi[kHybridModes];     // the sustaining mode state of the key
        alignas(32) real ar[kHybridModes], ai[kHybridModes];     // poles per sample
        real dsig = 0;                                            // extra loss rate of the damper [1/s]
        real af = 1, as = 1;                                      // slow phase: amplitude of the fast and the slow share since the strike
        double fc[kHybridModes] = {};                             // voicing: mean frequency [Hz] of the partial cluster of each mode
        real dpos = 0;                                            // damper position 0 (lifted) .. 1 (on)
        real dpos0 = 0;                                           // ... at the start of the chunk (the harp follows the ramp)
        real trim = 1;
        int modes = 0, n4 = 0;                                    // modes of the key in the bank, and rounded up to blocks of four
        uint32_t live = 0;                                        // a bit per block of four modes that still sounds (runModes)
        bool active = false, down = false, damped = true;
        Loop act, old;                                            // the current loop and the one fading out under a new strike
        alignas(32) real hr[kHybridModes], hi[kHybridModes];     // the harp: the key's strings driven by the others
        real hg[kHybridModes];                                    // input weight per mode
        int hModes = 0;
        bool hActive = false;
        bool choking = false;                                     // given up for the voice limit: fades out, then cleared
        real chokeEnv = 1;
    };
    struct Voice {
        bool used = false;
        int key = 0, vel = 0, j = 0;
        int nE = 0, nAtt = 0;                                     // the crossfade of its key
        real w0 = 1, w1 = 0, sc0 = 0, sc1 = 0;
        const int16_t *p0 = nullptr, *p1 = nullptr;
        float zinit[2 * kHybridModes] = {};                       // interpolated at noteOn (version 7: anchors)
        real b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, f1 = 0, f2 = 0;
        real gain = 1, scale = 1;                                 // loudness, and the damper's decay while the attack runs
        bool pend = false;
        real fadeEnv = 1, fadeDec = 0;                            // a stolen attack: 5 ms fade
        bool fading = false;
        uint64_t age = 0;
        alignas(32) real zr[kHybridModes], zi[kHybridModes];     // pending mode state of this strike
    };

    const HybridBank* bank = nullptr;
    void setPoles(Key& k, int note);   // from the bank's poles and the voicing
    HybridParams prm;
    double fs = 0;                     // the bank's rate, set by prepare() (a bank plays at its own rate only)
    int first = 0, nKeys = 0;
    std::vector<Key> keys;
    std::vector<Voice> voices, fades;
    real lawTab[kHybridVelocities + 1] = {};    // the bank's loudness law
    real gainTab[kHybridVelocities + 1] = {};   // ... with the velocity gain of the preset
    real rampStep = 0;               // damper travel per sample
    real lost = 0.5, full = 0.85;
    real slowShare = 0, slowSig = 0;   // the slow phase under the damper (0: off)
    HybridVoicing voicing;
    int velShift = 0;                  // hardness as a velocity offset of the timbre
    real stealDec = 0;
    int maxKeys = 0;                   // sounding keys at most (setMaxKeys; 0 = all)
    real chokeDec = 0;                 // a key given up for the limit fades out in 20 ms
    int pedal = 0;
    uint64_t age = 0;
    bool killing = false;
    real killEnv = 1, killDec = 0;
    std::vector<real> fac;           // damper decay factor of each key in this chunk
    std::vector<float> yk;           // each key's own share of the chunk's output (kChunk per key), summed in key order
    int seam = 0;                    // loop seam [samples], from the bank
    real seamCos[kMaxSeam] = {}, seamSin[kMaxSeam] = {};
    HybridResonance res;

    std::vector<int> slotStart, slotList;   // the attack slots of each key in this chunk: its voices, then its fades, in slot order
    std::vector<uint8_t> keyOut, harpOut;   // key i wrote its row of yk / of hk in this chunk
    std::vector<real> hk;                   // each key's harp output of the chunk (kChunk per key)
    int cc = 0;                             // the chunk's length
    real onFracC = 0;                       // on_fraction() of the chunk
    const float* oC = nullptr;              // the chunk's dry output, for the harp
    bool loudC = false;

    Voice& slot(int s) { return s < int(voices.size()) ? voices[size_t(s)] : fades[size_t(s) - voices.size()]; }
    void groupSlots() {                     // counting sort of the used slots by key
        std::fill(slotStart.begin(), slotStart.end(), 0);
        const int ns = int(voices.size() + fades.size());
        for (int s = 0; s < ns; ++s)
            if (slot(s).used) ++slotStart[size_t(slot(s).key - first + 1)];
        for (int i = 0; i < nKeys; ++i) slotStart[size_t(i + 1)] += slotStart[size_t(i)];
        int fill[128];                      // a bank covers MIDI notes at most
        for (int i = 0; i < nKeys; ++i) fill[i] = slotStart[size_t(i)];
        for (int s = 0; s < ns; ++s)
            if (slot(s).used) slotList[size_t(fill[slot(s).key - first]++)] = s;
    }
    void keyWork(int i);
    void attack(Voice& v, int c, float* __restrict ko);

    Key& K(int note) { return keys[size_t(note - first)]; }
    // the smoothstep over a crossfade of nF samples at sample j of it (0 .. nF)
    static real smooth(int j, int nF) {
        const double s = double(j) / double(nF);
        return real(3 * s * s - 2 * s * s * s);
    }
    real onFraction() const {
        const real p = real(pedal) / real(127);
        real x = (p - lost) / (full - lost);
        x = x < 0 ? 0 : x > 1 ? 1 : x;
        return real(1) - x * x * (real(3) - real(2) * x);
    }
    void reset() {
        for (Key& k : keys) {
            std::memset(k.zr, 0, sizeof k.zr);
            std::memset(k.zi, 0, sizeof k.zi);
            k.active = false; k.down = false;
            k.dpos = k.damped ? real(1) : real(0);               // at rest the dampers lie on the strings (the harp sees no free string)
            k.af = k.as = 1;
            k.act.used = k.old.used = false;
            k.choking = false;
            clearHarp(k);
        }
        for (Voice& v : voices) v.used = false;
        for (Voice& v : fades) v.used = false;
        killing = false;
        killEnv = 1;
    }
    static void clearHarp(Key& k) {
        std::memset(k.hr, 0, sizeof k.hr);
        std::memset(k.hi, 0, sizeof k.hi);
        k.hActive = false;
    }
    // the harp of key i over the chunk: if its strings are free, x = X - y_i drives them (X the dry output oC, y_i the key's own
    // share), its output goes to its row of hk; the rows are added to the output afterwards. Nothing of it reaches X: no loop, no
    // limit.
    void harpKey(int i) {
        Key& k = keys[size_t(i)];
        harpOut[size_t(i)] = 0;
        if (k.hModes == 0) return;
        // the string takes its drive as far as its damper is off, sample by sample along the damper ramp (exact for any chunk
        // division); a key without a damper is free, or with res.undamped off as free as the pedal makes the others
        const bool ownDamper = k.damped || res.undamped;
        const real p0 = ownDamper ? k.dpos0 : onFracC, p1 = ownDamper ? k.dpos : onFracC;
        const bool drive = loudC && (p0 < real(1) || p1 < real(1));
        if (!drive && !k.hActive) return;
        const int c = cc;
        const float* o = oC;
        const float* own = keyOut[size_t(i)] ? &yk[size_t(i) * kChunk] : nullptr;
        const int nm = k.hModes;
        const real step = rampStep;
        // the drive enters in quadrature (i g x): a string driven at its resonance answers 90 degrees off, so it and its
        // source add in energy, not in amplitude (pmpiano docs section 9.7); hg, hr and hi beyond hModes are zero (setResonance)
        real x[kChunk] = {};                                         // c of them are read (= {}: GCC cannot tell)
        for (int s = 0; s < c; ++s) {
            const real ps = p1 >= p0 ? std::min(p0 + step * real(s + 1), p1) : std::max(p0 - step * real(s + 1), p1);
            x[s] = drive ? (real(1) - ps) * real(o[s] - (own ? own[s] : 0.0f)) : real(0);
        }
        driveModes(k.hr, k.hi, k.ar, k.ai, k.hg, x, (nm + 3) & ~3, c, &hk[size_t(i) * kChunk]);
        harpOut[size_t(i)] = 1;
        if (drive) {                                                 // a driven string never sleeps: it may still be building up
            k.hActive = true;
            return;
        }
        real e = 0;
        for (int m = 0; m < nm; ++m) e += k.hr[m] * k.hr[m] + k.hi[m] * k.hi[m];
        if (!(e > real(prm.sleepEnergy))) clearHarp(k);              // undriven and faded out: asleep
    }
    // merge the pending state of a voice, scaled by w, into its key (the strike has become, or is becoming, sustain)
    void mergePending(Voice& v, real w) {
        Key& k = K(v.key);
        if (v.pend) {
            for (int m = 0; m < k.n4; ++m) {
                k.zr[m] += w * v.zr[m];
                k.zi[m] += w * v.zi[m];
            }
            k.active = true;
            k.live = (1u << (k.n4 >> 2)) - 1u;
        }
        v.pend = false;
    }
    void layers(int vel, int& i0, real& w0, int& i1, real& w1) const {
        const int nl = bank->layers();
        const int a0 = bank->anchor(0), a3 = bank->anchor(nl - 1);
        if (vel <= a0) { i0 = i1 = 0; w0 = 1; w1 = 0; return; }
        if (vel >= a3) { i0 = i1 = nl - 1; w0 = 1; w1 = 0; return; }
        int i = 0;
        for (int j = 0; j < nl - 1; ++j)
            if (bank->anchor(j) <= vel) i = j;
        const double t = double(vel - bank->anchor(i)) / double(bank->anchor(i + 1) - bank->anchor(i));
        i0 = i; i1 = i + 1; w0 = real(1.0 - t); w1 = real(t);
    }
    // ---- the high-band loop
    void startLoop(Key& k, int note, const Voice& v) {
        const int nb = bank->loopBands(note);
        if (!nb) return;
        if (k.act.used) {                                         // the old loop fades out under the new strike's crossfade
            k.old = k.act;
            k.old.fadeOut = true;
            k.old.idx = 0;
        }
        Loop& L = k.act;
        L = Loop();
        L.used = true;
        L.bands = nb;
        L.nF = v.nAtt - v.nE;
        int i0, i1;
        real w0, w1;
        layers(v.vel, i0, w0, i1, w1);
        float pv[7 * kHybridLoopBands] = {};
        bank->loopVelocity(note, v.vel, pv);
        const double knot = double(kKnot) / fs;
        const long h = seam / 2;
        for (int b = 0; b < nb; ++b) {
            const float* w = pv + 7 * b;
            L.x0[b] = bank->loopPcm(note, i0, b);
            L.x1[b] = bank->loopPcm(note, i1, b);
            L.c0[b] = real(double(w0) * double(bank->loopScale(note, i0, b)) / 32767.0);
            L.c1[b] = real(double(w1) * double(bank->loopScale(note, i1, b)) / 32767.0);
            L.start[b] = w[0]; L.len[b] = w[1]; L.ph[b] = w[2];
            L.e1[b] = w[3]; L.e2[b] = w[5];
            L.r1[b] = std::exp(-double(w[4]) * knot); L.r2[b] = std::exp(-double(w[6]) * knot);
            L.p[b] = 0; L.b0[b] = 0;
            L.b0n[b] = long(std::ceil(L.len[b] - L.ph[b])) - h;
            if (L.b0n[b] <= 0) {                                  // a first pass shorter than h
                L.p[b] = 1; L.b0[b] = L.b0n[b]; L.b0n[b] = long(std::ceil(2.0 * L.len[b] - L.ph[b])) - h;
            }
        }
        L.gain = v.gain * v.scale;
        L.dmp = 1;
        L.age = v.age;
    }
    // band b over the output samples [a - 1, e + 2) read at base + m, times its gain, then the Hermite FIR: y[0 .. e - a)
    void readBand(const Loop& L, int b, long a, long e, double base, real* y) const {
        const double qf = std::floor(base);
        const long q = long(qf);
        const real f = real(base - qf);
        const real h0 = ((real(-0.5) * f + real(1)) * f - real(0.5)) * f, h1 = (real(1.5) * f - real(2.5)) * f * f + real(1),
                   h2 = ((real(-1.5) * f + real(2)) * f + real(0.5)) * f, h3 = (real(0.5) * f - real(0.5)) * f * f;
        const int n = int(e - a) + 3;
        real mix[kChunk + 3];
        // gain: linear between knots, knot values from the recursion at kb (m clamped at 0: before N_e the gain of N_e)
        const long mlo = std::max(a - 1, 0L);
        const long k0 = mlo / kKnot, k1 = std::max(e + 1, 0L) / kKnot + 1;
        double kv[kChunk / kKnot + 4];
        double v1 = L.e1[b], v2 = L.e2[b];
        for (long k = L.kb; k < k0; ++k) { v1 *= L.r1[b]; v2 *= L.r2[b]; }
        for (long k = k0; k <= k1; ++k) { kv[k - k0] = v1 + v2; v1 *= L.r1[b]; v2 *= L.r2[b]; }
        // the gain per sample: between the knots linear, as a start and a step in real per knot interval (the knot values
        // stay in double: their recursion runs over the whole note); the loops over a knot interval and the mix vectorize
        real gv[kChunk + 3];
        for (int i = 0; i < n;) {
            const long mm = a - 1 + i;
            const long mc = mm < 0 ? 0 : mm;                      // before the loop's start the gain of its first knot
            const long kk = mc / kKnot;
            const int run = mm < 0 ? std::min(n - i, int(-mm)) : std::min(n - i, int((kk + 1) * kKnot - mc));
            const real g0 = real(kv[kk - k0]);
            const real dg = (real(kv[kk - k0 + 1]) - g0) * (real(1) / real(kKnot));
            const real f0 = mm < 0 ? real(0) : real(mc - kk * kKnot);
            real* gi = gv + i;
            for (int j = 0; j < run; ++j) gi[j] = g0 + dg * (f0 + real(j));
            i += run;
        }
        const int16_t* x0 = L.x0[b] + q;
        const int16_t* x1 = L.x1[b] + q;
        const real c0 = L.c0[b], c1 = L.c1[b];
        const long m0 = a - 1;
        for (int i = 0; i < n; ++i) mix[i] = gv[i] * (c0 * real(x0[m0 + i]) + c1 * real(x1[m0 + i]));
        for (int i = 0; i < n - 3; ++i) y[i] = h0 * mix[i] + h1 * mix[i + 1] + h2 * mix[i + 2] + h3 * mix[i + 3];
    }
    // c samples of a loop into acc (unweighted, summed over its bands), advancing it
    void renderLoop(Loop& L, real* acc, int c) {
        const long h = seam / 2;
        const long end = L.m + c;
        real y[kChunk], yo[kChunk];
        for (int s = 0; s < c; ++s) acc[s] = 0;
        for (int b = 0; b < L.bands; ++b) {
            long cur = L.m;
            while (cur < end) {
                const long se = std::min(end, L.b0n[b]);
                if (se > cur) {
                    const double base = L.start[b] + L.ph[b] - double(L.p[b]) * L.len[b];
                    readBand(L, b, cur, se, base, y);
                    if (L.p[b] >= 1 && cur < L.b0[b] + seam) {    // the seam: the old pass run on, the new one led in
                        const long s1 = std::min(se, L.b0[b] + seam);
                        readBand(L, b, cur, s1, base + L.len[b], yo);
                        for (long m = cur; m < s1; ++m) {
                            const long i = m - L.b0[b];
                            y[m - cur] = yo[m - cur] * seamCos[i] + y[m - cur] * seamSin[i];
                        }
                    }
                    for (long m = cur; m < se; ++m) acc[m - L.m] += y[m - cur];
                    cur = se;
                }
                if (cur == L.b0n[b]) {
                    ++L.p[b];
                    L.b0[b] = L.b0n[b];
                    L.b0n[b] = long(std::ceil(double(L.p[b] + 1) * L.len[b] - L.ph[b])) - h;
                }
            }
        }
        L.m = end;
        // advance the knot base to the lowest knot the next chunk needs
        const long kNew = std::max(L.m - 1, 0L) / kKnot;
        for (int b = 0; b < L.bands; ++b)
            for (long k = L.kb; k < kNew; ++k) { L.e1[b] *= L.r1[b]; L.e2[b] *= L.r2[b]; }
        L.kb = kNew;
    }
    // a key sounds while it has a mode state, a loop or an attack
    bool sounding(int i) const {
        const Key& k = keys[size_t(i)];
        if (k.active || k.act.used || k.old.used) return true;
        for (const Voice& v : voices)
            if (v.used && v.key == first + i) return true;
        for (const Voice& v : fades)
            if (v.used && v.key == first + i) return true;
        return false;
    }
    // the voice limit before a strike of a key that does not sound: while maxKeys or more keys sound (choking ones not
    // counted), the quietest gives up - a released key before a held or pedalled one, a key in its attack last - and fades
    // out within 20 ms, slow enough for a bass key not to click
    void makeRoom(int note) {
        if (maxKeys <= 0 || maxKeys >= nKeys || sounding(note - first)) return;
        const bool pedalDown = onFraction() < real(0.5);
        for (;;) {
            int playing = 0, victim = -1;
            bool victimReleased = false;
            double victimLevel = 0;
            for (int i = 0; i < nKeys; ++i) {
                const Key& k = keys[size_t(i)];
                if (k.choking || !sounding(i)) continue;
                ++playing;
                const bool released = !k.down && k.damped && !pedalDown;
                double level = 0;
                for (int m = 0; m < k.n4; ++m) level += double(k.zr[m]) * k.zr[m] + double(k.zi[m]) * k.zi[m];
                for (const Voice& v : voices)
                    if (v.used && !v.fading && v.key == first + i) level += 1e30;   // a fresh strike goes last
                if (victim < 0 || (released && !victimReleased) || (released == victimReleased && level < victimLevel)) {
                    victim = i;
                    victimReleased = released;
                    victimLevel = level;
                }
            }
            if (playing < maxKeys || victim < 0) return;
            choke(victim);
        }
    }
    void choke(int i) {
        Key& k = keys[size_t(i)];
        k.choking = true;
        k.chokeEnv = 1;
        for (std::vector<Voice>* pool : {&voices, &fades})
            for (Voice& v : *pool)
                if (v.used && v.key == first + i && !v.fading) {
                    v.pend = false;                                   // no tail: the key is going
                    v.fading = true;
                    v.fadeEnv = 1;
                    v.fadeDec = chokeDec;
                }
    }
    Voice* freeVoice() {
        for (Voice& v : voices)
            if (!v.used) return &v;
        return nullptr;
    }
    void retire(Voice& v, long& stolen, long& dropped) {   // an attack is given up: fade it out, keep its weighted sustain
        const real h = v.j >= v.nE ? smooth(std::min(v.j, v.nAtt) - v.nE, v.nAtt - v.nE) : real(0);
        mergePending(v, h);
        Loop& L = K(v.key).act;                               // its loop keeps the weight its modes keep
        if (L.used && L.age == v.age && !L.frozen) { L.frozen = true; L.frozenW = h; }
        Voice* slot = nullptr;
        for (Voice& f : fades)
            if (!f.used) { slot = &f; break; }
        ++stolen;
        if (slot) {
            *slot = v;
            slot->fading = true;
            slot->fadeEnv = 1;
            slot->fadeDec = stealDec;
        } else {
            ++dropped;
        }
        v.used = false;
    }
};

HybridPlayer::HybridPlayer() : d_(new Impl) {}
HybridPlayer::~HybridPlayer() = default;

double HybridPlayer::gainOf(int v) const {
    return d_->bank ? double(d_->gainTab[v < 1 ? 1 : v > kHybridVelocities ? kHybridVelocities : v]) : 0.0;
}

int HybridPlayer::attackVoices() const {
    int n = 0;
    for (const auto& v : d_->voices) n += v.used;
    for (const auto& v : d_->fades) n += v.used;
    return n;
}

int HybridPlayer::sustainingKeys() const {
    int n = 0;
    for (const auto& k : d_->keys) n += k.active || k.act.used || k.old.used;
    return n;
}

int HybridPlayer::loops() const {
    int n = 0;
    for (const auto& k : d_->keys) n += int(k.act.used) + int(k.old.used);
    return n;
}

bool HybridPlayer::prepare(double fs, const HybridBank& bank, const HybridParams& p) {
    Impl& d = *d_;
    if (!bank.valid() || std::fabs(fs - bank.rate()) > 0.5 || p.attackSlots < 1 || p.fadePool < 0) {
        d.bank = nullptr;                     // refused: the player is empty, it never keeps a bank it was not prepared for
        return false;
    }
    d.bank = &bank;
    d.prm = p;
    d.fs = fs;
    d.first = bank.firstKey();
    d.nKeys = bank.lastKey() - bank.firstKey() + 1;
    d.keys.assign(size_t(d.nKeys), Impl::Key());
    d.voices.assign(size_t(p.attackSlots), Impl::Voice());
    d.fades.assign(size_t(p.fadePool), Impl::Voice());
    d.fac.assign(size_t(d.nKeys), real(1));
    const double level = p.level >= 0 ? p.level : bank.level(), refVel = p.refVel >= 0 ? p.refVel : bank.refVel(),
                 gamma = p.gamma >= 0 ? p.gamma : bank.gamma();
    for (int v = 1; v <= kHybridVelocities; ++v) d.lawTab[v] = real(level * std::pow(double(v) / refVel, gamma));
    d.lawTab[0] = 0;
    std::copy(d.lawTab, d.lawTab + kHybridVelocities + 1, d.gainTab);
    d.stealDec = real(1.0 / std::max(1.0, p.stealFadeMs * 1e-3 * fs));
    d.chokeDec = real(1.0 / (0.020 * fs));
    for (int i = 0; i < d.nKeys; ++i) {
        Impl::Key& k = d.keys[size_t(i)];
        const int note = d.first + i;
        const HybridKeyRec& r = bank.key(note);
        const double* pol = bank.poles(note);
        k.modes = r.modes;
        k.n4 = (k.modes + 3) & ~3;                                // the modes beyond are dead in the bank (sigma 1000/s): not played
        k.damped = r.damped != 0;
        k.trim = real(std::pow(10.0, double(r.trimDb) / 20.0));
        k.dsig = real(kLn1000 / double(r.releaseT60));
        // the partial clusters of the voicing: live modes sorted by frequency, a mode joins the cluster of the mode before it
        // when it lies within 1 % of that cluster's mean (unison detunes are a few cents, partials lie further apart)
        {
            int order[kHybridModes];
            const int K = k.modes;
            for (int m = 0; m < K; ++m) order[m] = m;
            std::sort(order, order + K, [&](int a, int b) { return std::fabs(pol[2 * a + 1]) < std::fabs(pol[2 * b + 1]); });
            int start = 0;
            double sum = 0;
            auto flush = [&](int end) { for (int i = start; i < end; ++i) k.fc[order[i]] = sum / double(end - start); };
            for (int i = 0; i < K; ++i) {
                const double f = std::fabs(pol[2 * order[i] + 1]) / (2.0 * kPi);
                if (i > start && std::fabs(f - sum / double(i - start)) > 0.01 * (sum / double(i - start))) { flush(i); start = i; sum = 0; }
                sum += f;
            }
            flush(K);
            for (int m = K; m < kHybridModes; ++m) k.fc[m] = 0;
        }
        d.setPoles(k, note);
    }
    d.seam = 0;
    for (int i = 0; i < d.nKeys && bank.hasLoop(); ++i)
        if (const int sm = bank.loopSeam(d.first + i)) d.seam = sm;
    for (int i = 0; i < d.nKeys && bank.hasLoop(); ++i) {          // one seam for the instrument (the export writes one)
        const int sm = bank.loopSeam(d.first + i);
        if (sm && sm != d.seam) { d.bank = nullptr; return false; }
    }
    for (int i = 0; i < d.seam; ++i) {
        const double w = 0.5 * kPi * (i + 0.5) / d.seam;
        d.seamCos[i] = real(std::cos(w));
        d.seamSin[i] = real(std::sin(w));
    }
    d.pedal = 0;
    d.age = 0;
    stolen_ = dropped_ = 0;
    d.yk.assign(size_t(d.nKeys) * kChunk, 0.0f);
    d.hk.assign(size_t(d.nKeys) * kChunk, real(0));
    d.keyOut.assign(size_t(d.nKeys), 0);
    d.harpOut.assign(size_t(d.nKeys), 0);
    d.slotStart.assign(size_t(d.nKeys) + 1, 0);
    d.slotList.assign(d.voices.size() + d.fades.size(), 0);
    d.res = HybridResonance();
    d.reset();
    setDamper(p.rampMs, p.pedalLost, p.pedalFull);
    setResonance(p.res);
    return true;
}

void HybridPlayer::setReleaseT60(const float* t60ByNote) {
    Impl& d = *d_;
    if (!d.bank) return;
    for (int i = 0; i < d.nKeys; ++i) {
        const int note = d.first + i;
        const double t60 = t60ByNote ? double(t60ByNote[note]) : double(d.bank->key(note).releaseT60);
        d.keys[size_t(i)].dsig = real(kLn1000 / std::max(t60, 0.01));
    }
}

void HybridPlayer::Impl::setPoles(Key& k, int note) {
    const double* pol = bank->poles(note);
    const HybridVoicing& v = voicing;
    const bool neutral = v.decay == 0 && v.unison == 0 && v.stretch == 0;
    const double decayFac = std::pow(2.0, -v.decay / 100.0);                       // on sigma
    const double stretchFac = std::pow(2.0, v.stretch * ((note - 60) / 12.0) / 1200.0);
    for (int m = 0; m < kHybridModes; ++m) {
        double sg = pol[2 * m], om = pol[2 * m + 1];
        if (!neutral && m < k.modes) {
            const double f = std::fabs(om) / (2.0 * kPi);
            const double f1 = k.fc[m] + (f - k.fc[m]) * (1.0 + v.unison / 100.0);
            const double f2 = std::max(f1 * stretchFac, 0.0);
            om = (om < 0 ? -1.0 : 1.0) * 2.0 * kPi * f2;
            sg = sg * decayFac;
        }
        const double mag = std::exp(-sg / fs);
        k.ar[m] = real(mag * std::cos(om / fs));
        k.ai[m] = real(mag * std::sin(om / fs));
    }
}

void HybridPlayer::setVoicing(const HybridVoicing& v) {
    Impl& d = *d_;
    d.voicing = v;
    d.velShift = int(std::lround(32.0 * v.hardness / 100.0));
    if (!d.bank) return;
    for (int i = 0; i < d.nKeys; ++i) d.setPoles(d.keys[size_t(i)], d.first + i);
}

void HybridPlayer::setVelocityGain(const float* dbByVelocity) {
    Impl& d = *d_;
    for (int v = 0; v <= kHybridVelocities; ++v)
        d.gainTab[v] = dbByVelocity ? real(double(d.lawTab[v]) * std::pow(10.0, double(dbByVelocity[v]) / 20.0)) : d.lawTab[v];
}

void HybridPlayer::setSlowPhase(double share, double slowT60) {
    Impl& d = *d_;
    d.slowShare = real(std::min(std::max(share, 0.0), 0.5));
    d.slowSig = real(kLn1000 / std::max(slowT60, 0.01));
}

void HybridPlayer::setDamper(double rampMs, double pedalLost, double pedalFull) {
    Impl& d = *d_;
    if (!d.bank) return;
    const HybridBank& bank = *d.bank;
    const double ramp = rampMs > 0 ? rampMs : bank.rampMs();
    const double lost = std::min(std::max(pedalLost >= 0 ? pedalLost : bank.pedalLost(), 0.0), 0.95);
    const double full = std::max(std::min(pedalFull >= 0 ? pedalFull : bank.pedalFull(), 1.0), lost + 0.05);
    d.rampStep = real(1.0 / (ramp * 1e-3 * d.fs));
    d.lost = real(lost);
    d.full = real(full);
}

void HybridPlayer::setResonance(const HybridResonance& r) {
    Impl& d = *d_;
    if (!d.bank) return;
    const bool toggled = r.on != d.res.on;
    d.res = r;
    for (int i = 0; i < d.nKeys; ++i) {
        Impl::Key& k = d.keys[size_t(i)];
        const int note = d.first + i;
        const double* pol = d.bank->poles(note);
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        k.hModes = r.on ? std::max(0, std::min(k.modes, r.modes)) : 0;
        for (int m = 0; m < kHybridModes; ++m) {
            const double f = std::fabs(pol[2 * m + 1]) / (2.0 * kPi);
            const double oct = f > 0 ? std::log2(f / f0) : 0.0;
            k.hg[m] = m < k.hModes ? real(r.gain * std::pow(10.0, r.tiltDb * oct / 20.0)) : real(0);
            if (m >= k.hModes) k.hr[m] = k.hi[m] = 0;
        }
        if (toggled) Impl::clearHarp(k);
    }
}

void HybridPlayer::noteOn(int note, int velocity) {
    Impl& d = *d_;
    if (!d.bank) return;
    if (velocity <= 0) { noteOff(note); return; }
    if (note < d.bank->firstKey() || note > d.bank->lastKey()) return;
    const int vel = velocity > kHybridVelocities ? kHybridVelocities : velocity;
    d.makeRoom(note);
    Impl::Voice* v = d.freeVoice();
    if (!v) {                       // overload: the oldest attack of this key, else the oldest of all gives up its slot
        Impl::Voice* pick = nullptr;
        for (Impl::Voice& c : d.voices)
            if (c.used && c.key == note && (!pick || c.age < pick->age)) pick = &c;
        if (!pick)
            for (Impl::Voice& c : d.voices)
                if (c.used && (!pick || c.age < pick->age)) pick = &c;
        d.retire(*pick, stolen_, dropped_);
        v = pick;
    }
    Impl::Key& k = d.K(note);
    k.choking = false;              // struck again while it was giving up: it sounds on from where the fade got to
    k.chokeEnv = 1;
    k.down = true;
    k.dpos = 0;                     // the key lifts the damper before the hammer reaches the string: a new strike is not damped
    k.af = k.as = 1;                // both shares of the string are fresh
    *v = Impl::Voice();
    v->used = true;
    const int vt = std::min(kHybridVelocities, std::max(1, vel + d.velShift));   // the timbre's velocity (hardness)
    v->key = note; v->vel = vt; v->j = 0;
    v->nE = d.bank->fadeStart(note);
    v->nAtt = d.bank->attackFrames(note);
    int i0, i1;
    d.layers(vt, i0, v->w0, i1, v->w1);
    const HybridKeyRec& r = d.bank->key(note);
    v->p0 = d.bank->pcm(note, i0);
    v->p1 = d.bank->pcm(note, i1);
    v->sc0 = real(r.pcmScale[i0] / 32767.0);
    v->sc1 = real(r.pcmScale[i1] / 32767.0);
    double c[5];
    d.bank->filter(note, vt, c);
    v->b0 = real(c[0]); v->b1 = real(c[1]); v->b2 = real(c[2]); v->a1 = real(c[3]); v->a2 = real(c[4]);
    d.bank->states(note, vt, v->zinit);
    v->gain = d.gainTab[vel] * k.trim;                                            // the level follows the played velocity
    v->scale = 1;
    v->age = ++d.age;
}

void HybridPlayer::noteOff(int note) {
    Impl& d = *d_;
    if (!d.bank || note < d.bank->firstKey() || note > d.bank->lastKey()) return;
    d.K(note).down = false;
}

void HybridPlayer::controller(int cc, int value) {
    Impl& d = *d_;
    if (cc == 64) d.pedal = value < 0 ? 0 : value > 127 ? 127 : value;
    else if (cc == 120) allSoundOff();
    else if (cc == 123 && d.bank)
        for (int note = d.bank->firstKey(); note <= d.bank->lastKey(); ++note) d.K(note).down = false;
}

void HybridPlayer::allSoundOff() {
    Impl& d = *d_;
    if (!d.bank) return;
    d.killing = true;
    d.killDec = d.stealDec;
}

void HybridPlayer::reset() { d_->reset(); }

// ---- one key's work of a chunk: the loop starts, the damper, the attacks, the sustain, the loops, the end of the attacks
void HybridPlayer::Impl::keyWork(int i) {
    Key& k = keys[size_t(i)];
    const int c = cc;
    const int* sl = slotList.data() + slotStart[size_t(i)];
    const int ns = slotStart[size_t(i + 1)] - slotStart[size_t(i)];
    keyOut[size_t(i)] = 0;
    if (seam)                                                    // loops start at the N_e of their strike
        for (int q = 0; q < ns; ++q) {
            const Voice& v = slot(sl[q]);
            if (sl[q] < int(voices.size()) && !v.fading && v.j == v.nE) startLoop(k, v.key, v);
        }

    // ---- damper: a released key drops its damper on the string, a lifted pedal or a held key lifts it
    {
        const real target = (k.down || !k.damped) ? real(0) : onFracC;
        real f = real(1);
        k.dpos0 = k.dpos;
        if (k.dpos != target || k.dpos > 0) {
            const real step = rampStep * real(c);
            const real old = k.dpos;
            k.dpos = old < target ? std::min(old + step, target) : std::max(old - step, target);
            const real p = real(0.5) * (old + k.dpos) * real(c) / real(fs);                // damper pressure over the chunk
            const real x = k.dsig * p;                                                     // exponent over the chunk, small
            f = real(1) / (real(1) + x + real(0.5) * x * x + x * x * x / real(6));
            if (slowShare > 0) {
                // two populations with the same poles: the fast share under the felt, the slow share beside it; the factor of
                // the chunk is the ratio of their sums, so the key's states follow the two-exponential envelope
                const real xs = slowSig * p;
                const real fs2 = real(1) / (real(1) + xs + real(0.5) * xs * xs + xs * xs * xs / real(6));
                const real before = (real(1) - slowShare) * k.af + slowShare * k.as;
                k.af *= f;
                k.as *= fs2;
                f = ((real(1) - slowShare) * k.af + slowShare * k.as) / before;
            }
        }
        if (k.choking) {                                         // the voice limit: a linear fade over 20 ms
            const real e1 = std::max(real(0), k.chokeEnv - chokeDec * real(c));
            f *= k.chokeEnv > 0 ? e1 / k.chokeEnv : real(0);
            k.chokeEnv = e1;
        }
        fac[size_t(i)] = f;
        if (f != real(1) && k.active)
            for (int m = 0; m < kHybridModes; ++m) { k.zr[m] *= f; k.zi[m] *= f; }
        if (f != real(1)) { k.act.dmp *= f; k.old.dmp *= f; }
        if (f != real(1) && k.hActive)
            for (int m = 0; m < k.hModes; ++m) { k.hr[m] *= f; k.hi[m] *= f; }
    }
    if (!ns && !k.active && !k.act.used && !k.old.used) return;  // silent: no row of yk
    float* __restrict ko = &yk[size_t(i) * kChunk];       // the key's row: nothing else is written through it
    std::fill(ko, ko + c, 0.0f);
    keyOut[size_t(i)] = 1;

    // ---- attacks: PCM mix, velocity filter, crossfade, pending mode state
    for (int q = 0; q < ns; ++q) attack(slot(sl[q]), c, ko);

    // ---- sustain: the free decay of the key's mode state
    if (k.active) {
        real y[kChunk];
        runModes(k.zr, k.zi, k.ar, k.ai, k.n4, c, &k.live, real(prm.sleepEnergy * kBlockSleep), y);
        for (int s = 0; s < c; ++s) ko[s] += float(y[s]);
    }

    // ---- the high-band loops: weighted with the crossfade of their strike (the old one with its complement)
    if (seam) {
        real acc[kChunk];
        for (Loop* L : {&k.act, &k.old}) {
            if (!L->used) continue;
            renderLoop(*L, acc, c);
            const real g = L->gain * L->dmp;
            const int nF = L->nF, idx = L->idx;
            const bool fadeOut = L->fadeOut, frozen = L->frozen;
            const real frozenW = L->frozenW;
            for (int s = 0; s < c; ++s) {
                const int ix = std::min(idx + s, nF);
                const real hx = smooth(ix, nF);
                const real w = fadeOut ? real(1) - hx : frozen ? frozenW : hx;
                ko[s] += float(w * g * acc[s]);
            }
            L->idx = std::min(L->idx + c, nF);
            if (L->fadeOut && L->idx >= nF) { L->used = false; continue; }
            real e = 0;                                        // sleep: all band envelopes below the threshold
            for (int b = 0; b < L->bands; ++b) { const real v = real(L->e1[b] + L->e2[b]); e += v * v; }
            if (!(e * g * g > real(prm.sleepEnergy))) L->used = false;
        }
    }

    // ---- the end of an attack: merge its state into the key, free the slot
    for (int q = 0; q < ns; ++q) {
        Voice& v = slot(sl[q]);
        if (v.j >= v.nAtt) {
            if (!v.fading) mergePending(v, real(1));
            v.used = false;
        } else if (v.fading && v.fadeEnv <= real(0)) {
            v.used = false;
        }
    }
    // a choked key is silent at the end of its fade: cleared (its attacks fade on their own)
    if (k.choking && k.chokeEnv <= real(0)) {
        std::memset(k.zr, 0, sizeof k.zr);
        std::memset(k.zi, 0, sizeof k.zi);
        k.active = false;
        k.act.used = k.old.used = false;
        clearHarp(k);
        k.choking = false;
    }
    // sleep: a key whose state fell below the threshold is cleared
    if (k.active) {
        real e = 0;
        for (int m = 0; m < k.n4; ++m) e += k.zr[m] * k.zr[m] + k.zi[m] * k.zi[m];
        if (!(e > real(prm.sleepEnergy))) {
            std::memset(k.zr, 0, sizeof k.zr);
            std::memset(k.zi, 0, sizeof k.zi);
            k.active = false;
        }
    }
}

void HybridPlayer::Impl::attack(Voice& v, int c, float* __restrict ko) {
    const real f = fac[size_t(v.key - first)];
    Key& k = K(v.key);
    if (f != real(1)) {
        v.scale *= f;
        if (v.pend)
            for (int m = 0; m < kHybridModes; ++m) { v.zr[m] *= f; v.zi[m] *= f; }
    }
    const real sg = v.gain * v.scale;
    // the pending mode state: it starts at N_e (within this chunk at s0, or earlier) and runs through the kernel
    int s0 = c;
    if (!v.fading && v.j <= v.nE && v.nE < v.j + c) {
        s0 = v.nE - v.j;
        real e = 0;
        for (int m = 0; m < k.n4; ++m) {
            v.zr[m] = sg * real(v.zinit[2 * m]);
            v.zi[m] = sg * real(v.zinit[2 * m + 1]);
            e += v.zr[m] * v.zr[m] + v.zi[m] * v.zi[m];
        }
        v.pend = e > real(prm.sleepEnergy);                          // already below the sleep threshold: no tail
    } else if (v.pend) {
        s0 = 0;
    }
    real yp[kChunk];
    if (v.pend) runModes(v.zr, v.zi, k.ar, k.ai, k.n4, c - s0, nullptr, 0, yp + s0);
    for (int s = 0; s < c; ++s) {
        const int j = v.j + s;
        const real x = v.w0 * (real(v.p0[j]) * v.sc0) + v.w1 * (real(v.p1[j]) * v.sc1);
        const real y = v.b0 * x + v.f1;
        v.f1 = v.b1 * x - v.a1 * y + v.f2;
        v.f2 = v.b2 * x - v.a2 * y;
        real h = 0, o2;
        if (j >= v.nE) h = smooth(j - v.nE, v.nAtt - v.nE);
        o2 = (real(1) - h) * y * sg;
        if (v.pend && s >= s0) o2 += h * yp[s];
        ko[s] += float(v.fading ? o2 * v.fadeEnv : o2);
        if (v.fading) v.fadeEnv = std::max(real(0), v.fadeEnv - v.fadeDec);
    }
    v.j += c;
}

void HybridPlayer::process(float* out, int n) {
    if (n > 0) std::memset(out, 0, sizeof(float) * size_t(n));
    Impl& d = *d_;
    if (!d.bank || n <= 0) return;
    int done = 0;
    while (done < n) {
        int c = std::min(n - done, kChunk);
        for (const Impl::Voice& v : d.voices)               // chunks end where an attack ends (its state is merged there)
            if (v.used && v.j < v.nAtt) c = std::min(c, v.nAtt - v.j);
        if (d.seam)                                          // ... and where a loop starts (N_e of an attack)
            for (const Impl::Voice& v : d.voices)
                if (v.used && !v.fading && v.j < v.nE) c = std::min(c, v.nE - v.j);
        for (const Impl::Voice& v : d.fades)
            if (v.used && v.j < v.nAtt) c = std::min(c, v.nAtt - v.j);
        float* o = out + done;

        // ---- every key's work, then the output as the sum of the keys' rows in key order (block-size independent)
        d.cc = c;
        d.onFracC = d.onFraction();
        d.groupSlots();
        for (int i = 0; i < d.nKeys; ++i) d.keyWork(i);
        for (int i = 0; i < d.nKeys; ++i)
            if (d.keyOut[size_t(i)]) {
                const float* ko = &d.yk[size_t(i) * kChunk];
                for (int s = 0; s < c; ++s) o[s] += ko[s];
            }

        // ---- the harp: the strings that ring along (after everything that goes into X, before sound off), in key order
        if (d.res.on) {
            bool loud = false;
            for (int s = 0; s < c; ++s) loud = loud || o[s] != 0.0f;
            d.oC = o;
            d.loudC = loud;
            for (int i = 0; i < d.nKeys; ++i) d.harpKey(i);
            real acc[kChunk] = {};
            for (int i = 0; i < d.nKeys; ++i)
                if (d.harpOut[size_t(i)]) {
                    const real* h = &d.hk[size_t(i) * kChunk];
                    for (int s = 0; s < c; ++s) acc[s] += h[s];
                }
            for (int s = 0; s < c; ++s) o[s] += float(acc[s]);
        }

        // sound off: a short fade of everything, then silence
        if (d.killing) {
            for (int s = 0; s < c; ++s) {
                o[s] *= float(d.killEnv);
                d.killEnv = std::max(real(0), d.killEnv - d.killDec);
            }
            if (d.killEnv <= real(0)) d.reset();
        }
        done += c;
    }
}

void HybridPlayer::setMaxKeys(int n) { d_->maxKeys = n < 0 ? 0 : n; }

} // namespace pmp
