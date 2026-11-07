// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// reed_designer.cpp - FEM eigen-analysis of a reed with its tuning weight
#include "reed_designer.h"

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

// DOF numbering of the clamped beam: node i >= 1: w = 2i-2, theta = 2i-1; node 0 is the clamp (fixed).
int dofW(int node) { return node == 0 ? -1 : 2 * node - 2; }
int dofT(int node) { return node == 0 ? -1 : 2 * node - 1; }

// Normalised beam: L = 1, EI = 1, rhoA = 1 (strip mass 1); the weight as a point mass.
void assemble(int nEl, double tipMass, double tipPos, Matrix& K, Matrix& M) {
    const int n = 2 * nEl;
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
        const int idx[4] = {dofW(e), dofT(e), dofW(e + 1), dofT(e + 1)};
        for (int a = 0; a < 4; ++a) {
            if (idx[a] < 0) continue;
            for (int b = 0; b < 4; ++b) {
                if (idx[b] < 0) continue;
                K(idx[a], idx[b]) += kc * Ke[a][b];
                M(idx[a], idx[b]) += mc * Me[a][b];
            }
        }
    }
    if (tipMass <= 0.0) return;
    // tuning weight: point mass at tipPos, distributed with the shape functions
    const int e = std::min(int(tipPos / l), nEl - 1);
    double N[4];
    hermite((tipPos - e * l) / l, l, N);
    const int idx[4] = {dofW(e), dofT(e), dofW(e + 1), dofT(e + 1)};
    for (int a = 0; a < 4; ++a) {
        if (idx[a] < 0) continue;
        for (int b = 0; b < 4; ++b)
            if (idx[b] >= 0) M(idx[a], idx[b]) += tipMass * N[a] * N[b];
    }
}

// Displacement of a DOF vector at normalised position x in [0, 1]
double shapeAt(const double* v, int nEl, double x) {
    const double l = 1.0 / nEl;
    const int e = std::min(std::max(int(x / l), 0), nEl - 1);
    double N[4];
    hermite((x - e * l) / l, l, N);
    const int idx[4] = {dofW(e), dofT(e), dofW(e + 1), dofT(e + 1)};
    double s = 0.0;
    for (int a = 0; a < 4; ++a)
        if (idx[a] >= 0) s += N[a] * v[idx[a]];
    return s;
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
    double mu = 0, tipPos = 0;
    std::vector<double> lambda;      // ascending
    Matrix X;                        // columns: DOF vectors
};

const Normalised* normalisedSolution(int nEl, double mu, double tipPos) {
    static std::vector<std::unique_ptr<Normalised>> cache;   // distinct parameter sets only
#ifdef PMP_HAS_MUTEX
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
#endif
    for (const auto& c : cache)
        if (c->nEl == nEl && c->mu == mu && c->tipPos == tipPos) return c.get();
    std::unique_ptr<Normalised> s(new Normalised);
    s->nEl = nEl;
    s->mu = mu;
    s->tipPos = tipPos;
    Matrix K, M;
    assemble(nEl, mu, tipPos, K, M);
    if (!generalizedEigen(K, M, s->lambda, s->X)) return nullptr;
    if (cache.size() >= 96) cache.erase(cache.begin());   // per-key weights: one entry per key
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

ReedModes designReed(const ReedSpec& spec, double fMax, int maxModes) {
    ReedModes out;
    const int nEl = std::max(4, spec.elements);
    const double mu = std::max(spec.tipMu, 0.0);
    const double tipPos = std::min(std::max(spec.tipPosRel, 0.05), 1.0);
    const Normalised* s = normalisedSolution(nEl, mu, tipPos);
    if (!s) return out;

    // physical size: the length from the normalised fundamental, lambda_1 = beta^4 (EI = rhoA = L = 1),
    // for the chosen cross-section: f1 = sqrt(lambda_1) / (2 pi L^2) sqrt(EI / rhoA), EI / rhoA = E h^2 / (12 rho)
    const double c = spec.thickness * std::sqrt(spec.youngs / (12.0 * spec.density));   // sqrt(EI/rhoA)
    out.length = std::sqrt(std::sqrt(s->lambda[0]) * c / (2.0 * kPi * spec.f1));
    out.reedMass = spec.density * spec.thickness * spec.width * out.length;
    out.tipMass = mu * out.reedMass;
    const double shapeScale = 1.0 / std::sqrt(out.reedMass);
    const double readAt = std::min(std::max(spec.pickupPosRel, 0.0), 1.0);

    const int n = int(s->lambda.size());
    std::vector<double> v(static_cast<size_t>(n));
    for (int j = 0; j < n && out.count < maxModes; ++j) {
        const double f = spec.f1 * std::sqrt(s->lambda[j] / s->lambda[0]);
        if (f > fMax) break;
        for (int i = 0; i < n; ++i) v[size_t(i)] = s->X(i, j);
        // the sign of a mode is free: the tip moves positive in every mode
        const double sign = shapeAt(v.data(), nEl, 1.0) < 0.0 ? -1.0 : 1.0;
        const int m = out.count++;
        out.freq[m] = f;
        out.phiStrike[m] = sign * shapeScale * shapeAt(v.data(), nEl, spec.strikePosRel);
        out.phiTip[m]    = sign * shapeScale * shapeAt(v.data(), nEl, readAt);
        out.phiDamper[m] = sign * shapeScale * shapeAt(v.data(), nEl, spec.damperPosRel);
        if (m == 0 && spec.combLenRel > 0.0) {
            const double at = shapeAt(v.data(), nEl, readAt);
            const double end = shapeAt(v.data(), nEl, std::max(readAt - spec.combLenRel, 0.0));
            out.combDrop = at != 0.0 ? std::min(std::max(1.0 - end / at, 0.0), 1.0) : 0.0;
        }
    }
    out.fundamental = 0;
    return out;
}

} // namespace pmp
