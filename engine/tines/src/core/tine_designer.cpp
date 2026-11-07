// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// tine_designer.cpp - FEM eigen-analysis of tine + tuning spring + tonebar
#include "tine_designer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// The solution cache below is shared between threads on the desktop (calibration). Bare-metal
// toolchains such as the Pico's (newlib without threads) have no std::mutex and need no lock.
#if !defined(__GLIBCXX__) || defined(_GLIBCXX_HAS_GTHREADS)
#include <mutex>
#define PMP_HAS_MUTEX 1
#endif

namespace pmp {
namespace {

struct Matrix {
    int n = 0;
    std::vector<double> a;
    explicit Matrix(int size = 0) : n(size), a(size_t(size) * size_t(size), 0.0) {}
    double& operator()(int i, int j) { return a[size_t(i) * n + j]; }
    double operator()(int i, int j) const { return a[size_t(i) * n + j]; }
};

// Hermite cubic shape functions of a beam element (DOFs: w1, theta1, w2, theta2)
void hermite(double xi, double l, double N[4]) {
    const double xi2 = xi * xi, xi3 = xi2 * xi;
    N[0] = 1.0 - 3.0 * xi2 + 2.0 * xi3;
    N[1] = l * (xi - 2.0 * xi2 + xi3);
    N[2] = 3.0 * xi2 - 2.0 * xi3;
    N[3] = l * (xi3 - xi2);
}

// DOF numbering: 0 = clamp translation u (tonebar), node i >= 1: w = 2i-1, theta = 2i.
// The clamp rotation is fixed (theta_0 = 0) unless the tonebar carries a rotational
// compliance (tonebar.gyr > 0): then theta_0 is the last DOF, 2 nEl + 1.
int dofW(int node) { return node == 0 ? 0 : 2 * node - 1; }
int dofT(int node) { return node == 0 ? -1 : 2 * node; }
int dofRot(int nEl) { return 2 * nEl + 1; }

// Normalised beam: L = 1, EI = 1, rhoA = 1 (rod mass 1); spring as point mass.
void assemble(int nEl, double springMass, double springPos, Matrix& K, Matrix& M,
              bool rotDof = false) {
    const int n = 2 * nEl + 1 + (rotDof ? 1 : 0);
    const int th0 = rotDof ? dofRot(nEl) : -1;
    K = Matrix(n);
    M = Matrix(n);
    const double l = 1.0 / nEl;
    const double kc = 1.0 / (l * l * l), mc = l / 420.0;
    const double Ke[4][4] = {{12, 6 * l, -12, 6 * l},
                             {6 * l, 4 * l * l, -6 * l, 2 * l * l},
                             {-12, -6 * l, 12, -6 * l},
                             {6 * l, 2 * l * l, -6 * l, 4 * l * l}};
    const double Me[4][4] = {{156, 22 * l, 54, -13 * l},
                             {22 * l, 4 * l * l, 13 * l, -3 * l * l},
                             {54, 13 * l, 156, -22 * l},
                             {-13 * l, -3 * l * l, -22 * l, 4 * l * l}};
    for (int e = 0; e < nEl; ++e) {
        const int idx[4] = {dofW(e), e == 0 ? th0 : dofT(e), dofW(e + 1), dofT(e + 1)};
        for (int a = 0; a < 4; ++a) {
            if (idx[a] < 0) continue;
            for (int b = 0; b < 4; ++b) {
                if (idx[b] < 0) continue;
                K(idx[a], idx[b]) += kc * Ke[a][b];
                M(idx[a], idx[b]) += mc * Me[a][b];
            }
        }
    }
    // tuning spring: point mass at springPos, distributed with the shape functions
    const int e = std::min(int(springPos / l), nEl - 1);
    double N[4];
    hermite((springPos - e * l) / l, l, N);
    const int idx[4] = {dofW(e), e == 0 ? th0 : dofT(e), dofW(e + 1), dofT(e + 1)};
    for (int a = 0; a < 4; ++a) {
        if (idx[a] < 0) continue;
        for (int b = 0; b < 4; ++b)
            if (idx[b] >= 0) M(idx[a], idx[b]) += springMass * N[a] * N[b];
    }
}

// Displacement of a (full) DOF vector at normalised position x in [0, 1]; th0Dof is the
// index of the clamp rotation (-1: fixed)
double shapeAt(const double* v, int nEl, double x, int th0Dof = -1) {
    const double l = 1.0 / nEl;
    const int e = std::min(std::max(int(x / l), 0), nEl - 1);
    double N[4];
    hermite((x - e * l) / l, l, N);
    const double th0 = (e == 0) ? (th0Dof >= 0 ? v[th0Dof] : 0.0) : v[dofT(e)];
    return N[0] * v[dofW(e)] + N[1] * th0 + N[2] * v[dofW(e + 1)] + N[3] * v[dofT(e + 1)];
}

bool cholesky(Matrix& A) {  // in place, lower triangle = L
    const int n = A.n;
    for (int j = 0; j < n; ++j) {
        double s = A(j, j);
        for (int k = 0; k < j; ++k) s -= A(j, k) * A(j, k);
        if (s <= 0.0) return false;
        const double d = std::sqrt(s);
        A(j, j) = d;
        for (int i = j + 1; i < n; ++i) {
            double t = A(i, j);
            for (int k = 0; k < j; ++k) t -= A(i, k) * A(j, k);
            A(i, j) = t / d;
        }
        for (int i = 0; i < j; ++i) A(i, j) = 0.0;
    }
    return true;
}

// cyclic Jacobi eigenvalue algorithm for a symmetric matrix; A is destroyed
void jacobi(Matrix& A, std::vector<double>& eig, Matrix& V) {
    const int n = A.n;
    V = Matrix(n);
    for (int i = 0; i < n; ++i) V(i, i) = 1.0;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0, diag = 0.0;
        for (int i = 0; i < n; ++i) {
            diag += A(i, i) * A(i, i);
            for (int j = i + 1; j < n; ++j) off += A(i, j) * A(i, j);
        }
        if (off <= 1e-30 * diag) break;
        for (int p = 0; p < n - 1; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = A(p, q);
                if (std::fabs(apq) <= 1e-300) continue;
                const double theta = (A(q, q) - A(p, p)) / (2.0 * apq);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                                 (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {       // A = A J
                    const double akp = A(k, p), akq = A(k, q);
                    A(k, p) = c * akp - s * akq;
                    A(k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {       // A = J^T A
                    const double apk = A(p, k), aqk = A(q, k);
                    A(p, k) = c * apk - s * aqk;
                    A(q, k) = s * apk + c * aqk;
                }
                A(p, q) = A(q, p) = 0.0;
                for (int k = 0; k < n; ++k) {       // V = V J
                    const double vkp = V(k, p), vkq = V(k, q);
                    V(k, p) = c * vkp - s * vkq;
                    V(k, q) = s * vkp + c * vkq;
                }
            }
        }
    }
    eig.resize(n);
    for (int i = 0; i < n; ++i) eig[i] = A(i, i);
}

// K x = lambda M x, M-normalised eigenvectors in the columns of X, ascending lambda
bool generalizedEigen(const Matrix& K, const Matrix& M, std::vector<double>& lambda, Matrix& X) {
    const int n = K.n;
    Matrix L = M;
    if (!cholesky(L)) return false;
    // Y = L^-1 K (forward substitution per column)
    Matrix Y = K;
    for (int c = 0; c < n; ++c)
        for (int i = 0; i < n; ++i) {
            double s = Y(i, c);
            for (int k = 0; k < i; ++k) s -= L(i, k) * Y(k, c);
            Y(i, c) = s / L(i, i);
        }
    // C = L^-1 Y^T  (= L^-1 K L^-T)
    Matrix C(n);
    for (int c = 0; c < n; ++c)
        for (int i = 0; i < n; ++i) {
            double s = Y(c, i);
            for (int k = 0; k < i; ++k) s -= L(i, k) * C(k, c);
            C(i, c) = s / L(i, i);
        }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) C(i, j) = C(j, i) = 0.5 * (C(i, j) + C(j, i));

    std::vector<double> eig;
    Matrix Z;
    jacobi(C, eig, Z);

    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return eig[a] < eig[b]; });

    lambda.resize(n);
    X = Matrix(n);
    for (int j = 0; j < n; ++j) {
        const int src = order[j];
        lambda[j] = eig[src];
        for (int i = n - 1; i >= 0; --i) {  // back substitution L^T x = z
            double s = Z(i, src);
            for (int k = i + 1; k < n; ++k) s -= L(k, i) * X(k, j);
            X(i, j) = s / L(i, i);
        }
    }
    return true;
}

// Solution of the normalised problem; identical for all keys that share these parameters
struct Normalised {
    int nEl = 0;
    double mu = 0, springPos = 0, split = 0, detune = 0, mass = 0, gyr = 0;
    int n = 0;                       // DOFs of the full vector (incl. clamp translation)
    int th0 = -1;                    // DOF of the clamp rotation (-1: fixed)
    double tonebarInertia = 0;       // normalised rotational inertia of the tonebar
    std::vector<double> lambda;      // ascending
    Matrix X;                        // columns: full DOF vectors (X(0, j) = clamp translation)
    double tonebarMass = 0;          // normalised lumped tonebar mass
    double lambdaRigid = 0;          // first eigenvalue with rigid clamp
};

bool solveNormalised(Normalised& s) {
    Matrix K, M;
    assemble(s.nEl, s.mu, s.springPos, K, M);
    const int n = K.n;
    s.n = n;

    // 1) rigid clamp: drop DOF 0
    Matrix Kr(n - 1), Mr(n - 1);
    for (int i = 1; i < n; ++i)
        for (int j = 1; j < n; ++j) {
            Kr(i - 1, j - 1) = K(i, j);
            Mr(i - 1, j - 1) = M(i, j);
        }
    std::vector<double> lr;
    Matrix Xr;
    if (!generalizedEigen(Kr, Mr, lr, Xr)) return false;
    s.lambdaRigid = lr[0];

    if (s.split <= 0.0 && s.mass <= 0.0) {
        s.tonebarMass = 0.0;
        s.th0 = -1;
        s.lambda = lr;
        s.X = Matrix(n);
        for (int j = 0; j < n - 1; ++j)
            for (int i = 1; i < n; ++i) s.X(i, j) = Xr(i - 1, j);
        s.lambda.push_back(1e300);  // placeholder for the missing clamp mode (never used)
        return true;
    }

    // 2) participation of the first tine mode in a translation of the clamp:
    //    Gamma = v^T M r with r = unit translation (w = 1, theta = 0)
    std::vector<double> r(n, 0.0), v(n, 0.0);
    r[0] = 1.0;
    for (int node = 1; node <= s.nEl; ++node) r[dofW(node)] = 1.0;
    for (int i = 1; i < n; ++i) v[i] = Xr(i - 1, 0);
    double gamma = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) gamma += v[i] * M(i, j) * r[j];

    // 3) lumped tonebar: either its mass is given (ratio to the tine mass, the measured
    //    tonebar) or it follows from the split dw/w ~= Gamma / sqrt(M'), M' = Mb + tine mass
    const double mTine = 1.0 + s.mu;
    double mPrime;
    if (s.mass > 0.0) {
        s.tonebarMass = s.mass * mTine;
        mPrime = s.tonebarMass + mTine;
    } else {
        const double ratio = std::pow(2.0, s.split / 1200.0) - 1.0;
        mPrime = gamma * gamma / (ratio * ratio);
        s.tonebarMass = std::max(mPrime - mTine, 0.0);
    }
    const double wb2 = s.lambdaRigid * std::pow(2.0, 2.0 * s.detune / 1200.0);
    if (s.gyr > 0.0) {
        // 4) rotational compliance instead of the translation: the tonebar bends under the
        //    tine mount, the root rotates and the tip follows with the lever of the tine
        //    length (the measured tonebar line, docs/ATTACK_WINDOWS.md 5, needs this lever
        //    to reach the pickup as loud as it does). Inertia Mb gyr^2 (gyr: radius of
        //    gyration in tine lengths), spring tuned to wb with the assembly's inertia about
        //    the root (tonebar + tine: 1/3 + mu s^2); the clamp translation is pinned.
        assemble(s.nEl, s.mu, s.springPos, K, M, true);
        s.n = K.n;
        s.th0 = dofRot(s.nEl);
        s.tonebarInertia = s.tonebarMass * s.gyr * s.gyr;
        const double jPrime = s.tonebarInertia + 1.0 / 3.0 + s.mu * s.springPos * s.springPos;
        K(s.th0, s.th0) += jPrime * wb2;
        M(s.th0, s.th0) += s.tonebarInertia;
        K(0, 0) += 1e6 * s.lambdaRigid;        // translation pinned: one tonebar mode only
        M(0, 0) += 1e-6;
        return generalizedEigen(K, M, s.lambda, s.X);
    }
    s.th0 = -1;
    K(0, 0) += mPrime * wb2;
    M(0, 0) += s.tonebarMass;
    return generalizedEigen(K, M, s.lambda, s.X);
}

const Normalised* normalisedSolution(int nEl, double mu, double springPos, double split,
                                     double detune, double mass, double gyr) {
    static std::vector<std::unique_ptr<Normalised>> cache;   // distinct parameter sets only
#ifdef PMP_HAS_MUTEX
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
#endif
    for (const auto& c : cache)
        if (c->nEl == nEl && c->mu == mu && c->springPos == springPos && c->split == split &&
            c->detune == detune && c->mass == mass && c->gyr == gyr)
            return c.get();
    std::unique_ptr<Normalised> s(new Normalised);
    s->nEl = nEl;
    s->mu = mu;
    s->springPos = springPos;
    s->split = split;
    s->detune = detune;
    s->mass = mass;
    s->gyr = gyr;
    if (!solveNormalised(*s)) return nullptr;
    if (cache.size() >= 96) cache.erase(cache.begin());   // per-key tonebars: one entry per key
    cache.push_back(std::move(s));
    return cache.back().get();
}

} // namespace

double cantileverBeta(double mu, int n) {
    auto f = [mu](double b) {
        return 1.0 + std::cos(b) * std::cosh(b) +
               mu * b * (std::cos(b) * std::sinh(b) - std::sin(b) * std::cosh(b));
    };
    int found = 0;
    double b0 = 0.01, f0 = f(b0);
    for (double b1 = 0.02; b1 < 60.0; b1 += 0.01) {
        const double f1 = f(b1);
        if ((f0 < 0.0) != (f1 < 0.0)) {
            if (++found == n) {
                double lo = b0, hi = b1, flo = f0;
                for (int it = 0; it < 80; ++it) {
                    const double mid = 0.5 * (lo + hi), fm = f(mid);
                    if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
                    else hi = mid;
                }
                return 0.5 * (lo + hi);
            }
        }
        b0 = b1;
        f0 = f1;
    }
    return 0.0;
}

TineModes designTine(const TineSpec& spec, double fMax, int maxModes) {
    TineModes out;
    const int nEl = std::max(4, spec.elements);
    const Normalised* s = normalisedSolution(nEl, spec.springMu, spec.springPosRel,
                                             spec.tonebarSplit, spec.tonebarDetune,
                                             spec.tonebarMass, spec.tonebarGyr);
    if (!s) return out;

    // physical size: length from the tip-mass cantilever formula for the chosen diameter
    const double beta1 = cantileverBeta(spec.springMu, 1);
    const double c = 0.25 * spec.diameter * std::sqrt(spec.youngs / spec.density);  // sqrt(EI/rhoA)
    out.length = std::sqrt(beta1 * beta1 * c / (2.0 * kPi * spec.f1));
    out.tineMass = spec.density * kPi * 0.25 * spec.diameter * spec.diameter * out.length;
    out.springMass = spec.springMu * out.tineMass;
    const double shapeScale = 1.0 / std::sqrt(out.tineMass);

    // where the pickup reads the tine (the tip unless set): tine mode 2 has its node a
    // little before the tip (0.83 L with the spring), so a pickup that reads near it hardly
    // sees that mode (docs/ATTACK_WINDOWS.md 7)
    const double readAt = std::min(std::max(spec.pickupPosRel, 0.0), 1.0);

    // tine-dominant mode near the rigid-clamp fundamental: largest bending of the tine
    // (tip minus clamp translation), so that a strongly coupled tonebar mode, which moves
    // the whole tine, is not mistaken for it
    std::vector<double> v(s->n);
    int fund = 0;
    double best = -1.0;
    for (int j = 0; j < s->n; ++j) {
        const double ratio = s->lambda[j] / s->lambdaRigid;
        if (ratio < 0.5 || ratio > 2.0) continue;
        for (int i = 0; i < s->n; ++i) v[i] = s->X(i, j);
        const double a = std::fabs(shapeAt(v.data(), nEl, 1.0, s->th0) - v[0]);
        if (a > best) { best = a; fund = j; }
    }
    const double lambdaFund = s->lambda[fund];

    for (int j = 0; j < s->n && out.count < maxModes; ++j) {
        if (s->lambda[j] >= 1e299) break;
        const double f = spec.f1 * std::sqrt(s->lambda[j] / lambdaFund);
        if (f > fMax) break;
        for (int i = 0; i < s->n; ++i) v[i] = s->X(i, j);
        const int m = out.count++;
        out.freq[m] = f;
        out.phiStrike[m] = shapeScale * shapeAt(v.data(), nEl, spec.strikePosRel, s->th0);
        out.phiTip[m] = shapeScale * shapeAt(v.data(), nEl, readAt, s->th0);
        out.phiDamper[m] = shapeScale * shapeAt(v.data(), nEl, spec.damperPosRel, s->th0);
        out.tonebarShare[m] = s->tonebarMass * v[0] * v[0] +
                              (s->th0 >= 0 ? s->tonebarInertia * v[s->th0] * v[s->th0] : 0.0);
        if (j == fund) out.fundamental = m;
    }
    return out;
}

} // namespace pmp
