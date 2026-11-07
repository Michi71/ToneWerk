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
    double pickupPosRel  = 1.0;     // where the pickup reads the tine / length (1 = the tip)
    double tonebarSplit  = 0.0;     // [cent] coupling as mode split at zero detune; 0 = rigid
    double tonebarDetune = 0.0;     // [cent]
    double tonebarMass   = 0.0;     // tonebar mass / tine mass; > 0 sets the coupling directly
    double tonebarGyr    = 0.0;     // radius of gyration of the tonebar in tine lengths; > 0 lets the clamp rotate
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
};

// Modes below fMax (at most maxModes), tuned so that freq[fundamental] == spec.f1.
TineModes designTine(const TineSpec& spec, double fMax, int maxModes = kMaxModes);

// n-th root (n = 1, 2, ...) of the frequency equation of a cantilever with tip mass ratio mu:
// 1 + cos(b) cosh(b) + mu b (cos(b) sinh(b) - sin(b) cosh(b)) = 0
double cantileverBeta(double mu, int n);

} // namespace pmp
