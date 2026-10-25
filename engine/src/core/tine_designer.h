// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// tine_designer.h - modal analysis of the Rhodes tone generator (tine + tonebar)
//
// The tine is an Euler-Bernoulli cantilever (finite elements with Hermite shape functions)
// carrying the tuning spring as a point mass. The tonebar is modelled as a lumped
// resonator acting on the translation of the clamp. The coupled eigenproblem
// K v = w^2 M v yields the normal modes of the "asymmetric tuning fork". Like a technician,
// the designer then tunes the tine-dominant mode exactly to the target pitch.
#pragma once

#include "config.h"

namespace pmp {

struct TineSpec {
    double f1            = 440.0;   // target pitch [Hz]
    double diameter      = 2.0e-3;  // [m]
    double youngs        = 2.0e11;  // [Pa]
    double density       = 7850.0;  // [kg/m^3]
    double springMu      = 0.3;     // spring mass / tine mass
    double springPosRel  = 0.93;    // spring position / length
    double strikePosRel  = 0.15;    // hammer contact / length
    double damperPosRel  = 0.45;    // damper contact / length
    double tonebarSplit  = 0.0;     // [cent] coupling as mode split at zero detune; 0 = rigid
    double tonebarDetune = 0.0;     // [cent]
    int    elements      = 20;
};

struct TineModes {
    double length     = 0.0;        // [m]
    double tineMass   = 0.0;        // [kg] rod without spring
    double springMass = 0.0;        // [kg]
    int    count      = 0;
    int    fundamental = 0;         // index of the tine-dominant mode (== f1)
    double freq[kMaxModes]         = {};  // [Hz]
    double phiStrike[kMaxModes]    = {};  // mass-normalised mode shapes [1/sqrt(kg)]
    double phiTip[kMaxModes]       = {};
    double phiDamper[kMaxModes]    = {};
    double tonebarShare[kMaxModes] = {};  // part of the modal mass located in the tonebar
    bool   isDead[kMaxModes] = {};       // whole-string/chorus mode (own loss law)
};

// Modes below fMax (at most maxModes), tuned so that freq[fundamental] == spec.f1.
TineModes designTine(const TineSpec& spec, double fMax, int maxModes = kMaxModes);

// n-th root (n = 1, 2, ...) of the frequency equation of a cantilever with tip mass ratio mu:
// 1 + cos(b) cosh(b) + mu b (cos(b) sinh(b) - sin(b) cosh(b)) = 0
double cantileverBeta(double mu, int n);

// The Clavinet D6 string: pinned at both ends (the tangent traps it against the metal stud for
// the duration of the note, docs/CLAVINET.md 2.1), so the mode shapes are sin(n*pi*s/L) and the
// frequencies n*f1*sqrt(1 + B*n^2) with the piano inharmonicity B from the bending stiffness.
// The voice is read at the pickup position (phiTip - for the tine the same slot plays where the
// pickup sits, and the comb of the magnetic transducer comes with it), struck at the tangent
// (phiStrike) and damped by the yarn winding along the whole string (phiDamper, uniform).
struct StringSpec {
    double f1            = 440.0;   // target pitch [Hz]
    double tension       = 150.0;   // [N]
    double linearMass    = 3.0e-3;  // [kg/m] (with winding: the wrapped value)
    double diameter      = 0.9e-3;  // [m] core diameter, for the bending stiffness
    double youngs        = 2.0e11;  // [Pa]
    double strikePosRel  = 0.03;    // tangent contact / speaking length
    double pickupPosRel  = 0.94;    // pickup / speaking length (from the tangent end)
    double pickup2PosRel = -1.0;    // second pickup / speaking length (the selector sums both)
    double pickup2Gain   = 0.0;     // its share, negative = anti-phase (A+D of the D6)
    double inharmB       = -1.0;    // measured inharmonicity (Gabrieli et al. 2013, table 1);
                                    // <= 0: derived from the core stiffness
    // The whole string speaks too: the tangent traps it loosely, so the full length rings on
    // its own ladder n*deadRatio*f1 - the odd lines at (k+0.5) f1, the even ones inside the
    // harmonic series (docs/CLAVINET.md, ninth state). Struck and read through the same
    // positions scaled to the full length, damped by the yarn.
    int    deadModes     = 0;       // whole-string modes behind the ladder (0: off)
    double deadRatio     = 0.5;     // whole-string fundamental / speaking fundamental
    double deadGain      = 1.0;     // excitation share of the whole-string modes
};

TineModes designString(const StringSpec& spec, double fMax, int maxModes = kMaxModes);

} // namespace pmp
