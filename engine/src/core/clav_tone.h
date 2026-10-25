// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// clav_tone.h - the four tone switch banks of the Clavinet D6
//
// Values and topology after docs/CLAVINET.md 2.2 (Gabrielli 2013, table 3, and the
// restored A-64 schematic, p.14): the banks are switchable impedances between the
// amplifier stages -
//     Soft       30 kOhm || 0.1 uF        Y(s) = (1 + sRC) / R
//     Medium     10 kOhm || 15 nF         Y(s) = (1 + sRC) / R
//     Treble     2 H || 4.7 nF (+loss)    Y(s) = (1 + sC(R + sL)) / (R + sL)
//     Brilliant  0.6 H                    Y(s) = 1 / (sL)
// The stage computes the parallel impedance of the ACTIVE banks - the electrical truth
// of the switch network - and follows its magnitude as one filter of order <= 3. With
// one bank active that is exactly the table-3 impedance of that bank; with several, the
// parallel combination: two RC banks in parallel are again one RC bank (R||R, C+C), so
// Soft+Medium reads 7.5k||0.115u - a 185 Hz corner, not the product of both curves.
// This deviates from the paper's cascade ("cascading the Hi(z)", its section 3.4) on
// purpose: the cascade compounds multiplicatively and buries combinations - Michael's
// hearing verdict on the 11_switches_demo (treble+brilliant ten times under the others)
// settled that. The corner is prewarped (the raw transform compresses the axis above
// 8 kHz by 2+ dB), the treble coil carries a series loss (a loss-free trap would be a
// needle no 2 H coil exhibits), and with all banks off the stage is transparent, not
// silent - the calibration reference lives in that state, while the original breaks
// the path and stays mute.
#pragma once

#include <cmath>
#include <complex>

namespace pmp {

class ClavTone {
public:
    // kind: 0 soft, 1 medium, 2 treble, 3 brilliant
    void setup(double fs, bool soft, bool medium, bool treble, bool brilliant) {
        const bool on[4] = {soft, medium, treble, brilliant};
        unsigned mask = 0;
        for (int i = 0; i < 4; ++i) mask |= unsigned(on[i]) << i;
        if (mask == mask_ && fs == fs_) return;
        mask_ = mask;
        fs_ = fs;
        if (mask) config(on);
        reset();
    }

    bool any() const { return mask_ != 0; }

    void process(float* x, int n) {
        if (!mask_) return;
        for (int i = 0; i < n; ++i) {
            const double in = x[i];
            const double y = bk_[0] * in + zh_[0];             // transposed direct form II
            for (int k = 1; k < order_; ++k)
                zh_[k - 1] = bk_[k] * in - ak_[k] * y + zh_[k];
            zh_[order_ - 1] = bk_[order_] * in - ak_[order_] * y;
            x[i] = float(y);
        }
    }

    void reset() { for (double& v : zh_) v = 0.0; }

    // |H| at a frequency, for tests and the fit tools
    double magnitudeAt(double f) const {
        const double w = 2.0 * kPi * f / fs_;
        const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
        std::complex<double> num = 0.0, den = 0.0, p = 1.0;
        for (int k = 0; k <= order_; ++k, p *= z1) {
            num += bk_[k] * p;
            den += ak_[k] * p;
        }
        return std::abs(num / den);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr int kMaxOrder = 5;                   // H stays order <= 5
    static constexpr int kPoly = 9;                       // headroom for the products
    // the stage around the banks - single vision reads of the A-64 schematic, second
    // pass pending; tools/analysis/clavinet_tone.py scans their sensitivity
    static constexpr double kRs = 2.0e3;                  // source impedance of stage one
    static constexpr double kCin = 0.22e-6;               // its coupling cap to the stack
    static constexpr double kRl = 30.0e3;                 // series resistor into stage two
    static constexpr double kCout = 10.0e-9;              // series cap into the base
    static constexpr double kRbase = 100.0e3;             // the bias divider behind
    static constexpr double kTrebleR = 390.0;             // coil loss, not readable in the plan; also damps the
    static constexpr double kBrilliantR = 390.0;         // coil pair resonance at 3.4 kHz to a moderate hump
                                                          // rings against the treble trap at
                                                          // 21 kHz with Q ~ 1e6

    void config(const bool on[4]) {
        // Y = num/den per bank (ascending powers of s), summed fraction by fraction:
        // N/d = n1/d1 + n2/d2 + ... via N = N*di + ni*D, D = D*di (D starts as 1).
        double N[kPoly] = {}, D[kPoly] = {};
        int nN = -1, nD = 0;                              // nN = -1: N is still zero
        D[0] = 1.0;
        for (int i = 0; i < 4; ++i) {
            if (!on[i]) continue;
            double ni[kPoly] = {}, di[kPoly] = {};
            int nni, ndi;
            if (i == 0)      { ni[0] = 1.0 / 30.0e3; ni[1] = 0.1e-6;            nni = 2; di[0] = 1.0; ndi = 1; }
            else if (i == 1) { ni[0] = 1.0 / 10.0e3; ni[1] = 15.0e-9;          nni = 2; di[0] = 1.0; ndi = 1; }
            else if (i == 2) { ni[0] = 1.0; ni[1] = 4.7e-9 * kTrebleR;
                              ni[2] = 4.7e-9 * 2.0;                            nni = 3;
                              di[0] = kTrebleR; di[1] = 2.0;                   ndi = 2; }
            else             { ni[0] = 1.0;                                    nni = 1;
                              di[0] = kBrilliantR; di[1] = 0.6;               ndi = 2; }
            double tN[kPoly] = {}, tD[kPoly] = {};
            for (int a = 0; a <= (nN < 0 ? 0 : nN); ++a)
                for (int b = 0; b < ndi; ++b)
                    tN[a + b] += N[a] * di[b];
            const int dN = nN < 0 ? -1 : nN + ndi - 1;
            for (int a = 0; a <= nD; ++a)
                for (int b = 0; b < nni; ++b)
                    tN[a + b] += D[a] * ni[b];
            nN = std::max(dN, nD + nni - 1);
            for (int k = 0; k < kPoly; ++k) N[k] = tN[k];   // tN is the full sum already
            for (int a = 0; a <= nD; ++a)
                for (int b = 0; b < ndi; ++b)
                    tD[a + b] += D[a] * di[b];
            nD = nD + ndi - 1;
            for (int k = 0; k < kPoly; ++k) D[k] = tD[k];
        }
        // (2) the stage around the banks, as the voltage divider it is drawn as:
        //     H = 1 / (1 + ZS * (Y_par + YL)) with
        //     ZS = (1 + s RS CIN) / (s CIN)          the source through its coupling cap
        //     YL = s COUT / (1 + s COUT (RL + RBASE)) the load into the second base
        // The load caps every coil resonance, the source sets the absolute scale, and
        // "no bank" is flat by construction - no normalization trick needed.
        const double zsN[2] = {1.0, kRs * kCin}, zsD[2] = {0.0, kCin};
        const double ylN[2] = {0.0, kCout}, ylD[2] = {1.0, kCout * (kRl + kRbase)};
        double ynN[kPoly] = {}, ynD[kPoly] = {};
        int nynN = 0, nynD = 0;
        {   // Y_node = N/D + ylN/ylD = (N*ylD + ylN*D) / (D*ylD)
            double a[kPoly] = {}, b[kPoly] = {}, c[kPoly] = {};
            int na = pmul(N, nN, ylD, 1, a);
            int nb = pmul(ylN, 1, D, nD, b);
            int nc = pmul(D, nD, ylD, 1, c);
            nynN = padd(a, na, b, nb, ynN);
            nynD = nc;
            for (int i = 0; i < kPoly; ++i) ynD[i] = c[i];
        }
        double hz[kPoly] = {}, hd[kPoly] = {};           // H = zsD*ynD / (zsD*ynD + zsN*ynN)
        int nhz = 0, nhd = 0;
        {
            double a[kPoly] = {}, c[kPoly] = {};
            int na = pmul(zsD, 1, ynD, nynD, a);
            int nc = pmul(zsD, 1, ynD, nynD, c);
            int nd = pmul(zsN, 1, ynN, nynN, hd);
            nhz = na;
            for (int i = 0; i < kPoly; ++i) hz[i] = a[i];
            double sum[kPoly] = {};
            int ns = padd(c, nc, hd, nd, sum);
            nhd = ns;
            for (int i = 0; i < kPoly; ++i) hd[i] = sum[i];
        }
        // prewarp to the highest active bank corner (treble trap, else medium, else
        // soft; brilliant alone brings no corner), bilinear, and off we go
        double wc = 0.0;
        if (on[2])      wc = 1.0 / std::sqrt(4.7e-9 * 2.0);
        else if (on[1]) wc = 1.0 / (10.0e3 * 15.0e-9);
        else if (on[0]) wc = 1.0 / (30.0e3 * 0.1e-6);
        const double k = wc > 0.0 ? wc / std::tan(wc / (2.0 * fs_)) : 2.0 * fs_;
        order_ = std::max(nhz, nhd);
        double bz[kPoly] = {}, az[kPoly] = {};
        substitute(hz, nhz, k, bz);
        substitute(hd, nhd, k, az);
        if (az[0] == 0.0) az[0] = 1e-30;
        for (int i = 0; i <= order_; ++i) { bk_[i] = bz[i] / az[0]; ak_[i] = az[i] / az[0]; }
        for (int i = order_ + 1; i < kMaxOrder + 1; ++i) { bk_[i] = 0.0; ak_[i] = 0.0; }
    }

    // polynomial helpers, coefficients ascending
    static int pmul(const double* a, int na, const double* b, int nb, double* out) {
        for (int i = 0; i < kPoly; ++i) out[i] = 0.0;
        for (int i = 0; i <= na; ++i)
            for (int j = 0; j <= nb; ++j)
                if (i + j < kPoly) out[i + j] += a[i] * b[j];
        return std::min(na + nb, kPoly - 1);
    }
    static int padd(const double* a, int na, const double* b, int nb, double* out) {
        for (int i = 0; i < kPoly; ++i) out[i] = 0.0;
        for (int i = 0; i <= na; ++i) out[i] += a[i];
        for (int i = 0; i <= nb; ++i) out[i] += b[i];
        return std::max(na, nb);
    }

    // bilinear transform of p(s) (ascending, degree np) into z^-1 with s = k(1-z^-1)/(1+z^-1),
    // embedded into the current order_ via powers of (1 + z^-1)
    void substitute(const double* p, int np, double k, double* out) const {
        for (int i = 0; i < kPoly; ++i) out[i] = 0.0;
        for (int i = 0; i <= np; ++i) {
            double bin1[kPoly] = {}, bin2[kPoly] = {};
            binomial(i, -1.0, bin1);                      // (1 - z^-1)^i
            binomial(order_ - i, 1.0, bin2);              // (1 + z^-1)^(order - i)
            const double ks = std::pow(k, i);
            for (int a = 0; a <= i; ++a)
                for (int b = 0; b <= order_ - i; ++b)
                    out[a + b] += p[i] * ks * bin1[a] * bin2[b];
        }
    }

    static void binomial(int n, double sign, double* out) {
        double c[kPoly] = {};
        c[0] = 1.0;
        for (int m = 0; m < n; ++m)
            for (int j = std::min(m + 1, kPoly - 1); j > 0; --j) c[j] += sign * c[j - 1];
        for (int i = 0; i < kPoly; ++i) out[i] = i <= n ? c[i] : 0.0;
    }

    double magOf(const double* b, const double* a, double f) const {
        const double w = 2.0 * kPi * f / fs_;
        const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
        std::complex<double> num = 0.0, den = 0.0, p = 1.0;
        for (int k = 0; k <= order_; ++k, p *= z1) {
            num += b[k] * p;
            den += a[k] * p;
        }
        return std::abs(num / den);
    }

    unsigned mask_ = 0;
    double fs_ = 0.0;
    int order_ = 1;
    double bk_[kMaxOrder + 1] = {1, 0, 0, 0, 0, 0};
    double ak_[kMaxOrder + 1] = {1, 0, 0, 0, 0, 0};
    double zh_[kMaxOrder] = {};
};

} // namespace pmp
