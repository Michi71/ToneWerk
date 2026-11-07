// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// reed_designer.h - modal analysis of a reed: a clamped steel strip with a tuning weight at its tip
//
// The reed is an Euler-Bernoulli cantilever of rectangular cross-section (finite elements with
// Hermite shape functions) carrying the tuning weight (the solder of the Wurlitzer reeds) as a
// point mass. The eigenproblem K v = w^2 M v yields its normal modes; like a technician, the
// designer then tunes the fundamental exactly to the target pitch (the length follows from it).
// The clamp is rigid: a reed has no tonebar.
#pragma once

#include "config.h"

namespace pmp {

struct ReedSpec {
    double f1           = 440.0;    // target pitch [Hz]
    double thickness    = 0.5e-3;   // [m] in the swing direction
    double width        = 3.0e-3;   // [m]
    double youngs       = 2.0e11;   // [Pa]
    double density      = 7850.0;   // [kg/m^3]
    double tipMu        = 0.0;      // tuning weight / reed mass (0: a bare strip)
    double tipPosRel    = 0.98;     // position of the weight / length
    double strikePosRel = 0.5;      // hammer contact or pad / length
    double damperPosRel = 0.8;      // damper contact / length
    double pickupPosRel = 1.0;      // where the pickup reads the reed / length (1 = the tip)
    double combLenRel   = 0.0;      // stretch of the reed the pickup reads, ending at pickupPosRel / length
    int    elements     = 20;
};

struct ReedModes {
    double length    = 0.0;         // [m]
    double reedMass  = 0.0;         // [kg] strip without the weight
    double tipMass   = 0.0;         // [kg]
    int    count     = 0;
    int    fundamental = 0;         // index of the fundamental (== f1); always 0 for a rigid clamp
    double freq[kMaxModes]      = {};  // [Hz]
    double phiStrike[kMaxModes] = {};  // mass-normalised mode shapes [1/sqrt(kg)]
    double phiTip[kMaxModes]    = {};
    double phiDamper[kMaxModes] = {};
    double combDrop  = 0.0;         // the fundamental's amplitude lost across combLenRel, share of the read point's
};

// Modes below fMax (at most maxModes), tuned so that freq[0] == spec.f1.
ReedModes designReed(const ReedSpec& spec, double fMax, int maxModes = kMaxModes);

// n-th root (n = 1, 2, ...) of the frequency equation of a cantilever with tip mass ratio mu:
// 1 + cos(b) cosh(b) + mu b (cos(b) sinh(b) - sin(b) cosh(b)) = 0
double cantileverBeta(double mu, int n);

} // namespace pmp
