// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// VelocityCurve.h - fits the plugin to the keyboard: a power curve on the velocity of every note-on
//
// Keyboards differ in the velocities a given touch sends, and the physical models answer the velocity steeply. The curve
// bends the velocity before the strike variation: v' = 127 (v / 127)^g with g = 2^(-amount). Positive amounts lift the
// middle (a light touch plays louder: 64 becomes 90 at +100 %), negative ones lower it (64 becomes 32 at -100 %); 1 and 127
// stay where they are and 0 % is the identity.
#pragma once
#include <cmath>

namespace tw
{
// amount -1..1; velocity 1..127 in, 1..127 out (0 stays 0: a note-off)
inline int velocityCurve (int velocity, double amount)
{
    if (velocity <= 0 || amount == 0.0)
        return velocity;
    const double v = 127.0 * std::pow (double (velocity) / 127.0, std::exp2 (-amount));
    return int (std::fmin (127.0, std::fmax (1.0, std::round (v))));
}
} // namespace tw
