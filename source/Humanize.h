// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Humanize.h - strike variation: every note-on's velocity scatters a little
//
// The hybrid voices play a velocity bit for bit the same every time; a player's strikes never are. Like the physical model's
// human.speed (the hammer speed scattered per strike), each velocity is multiplied by exp(sigma g), g ~ N(0, 1): the level
// and, through the layer blend, the timbre move a little. 50 % is the physical presets' human.speed = 0.05 (sigma of ln speed)
// carried over to the velocity (speed ~ velocity^1.33: sigma 0.038), 100 % twice that. Real-time safe; the plugin applies
// it to every instrument and builds the engine with human.speed = 0 (no double scatter on the model).
#pragma once

#include <cmath>
#include <cstdint>

namespace tw
{
class Humanize
{
public:
    static constexpr double kSigmaAtFull = 0.075; // sigma of ln(velocity) at 100 %

    explicit Humanize (uint64_t seed = 0x9E3779B97F4A7C15ULL) : state_ (seed | 1) {}
    void seed (uint64_t s) { state_ = s | 1; }

    // amount 0..1; velocity 1..127 in, 1..127 out (0 stays 0: a note-off)
    int apply (int velocity, double amount)
    {
        if (velocity <= 0 || amount <= 0.0)
            return velocity;
        const double v = double (velocity) * std::exp (kSigmaAtFull * amount * gaussian());
        return int (std::fmin (127.0, std::fmax (1.0, std::round (v))));
    }

private:
    uint64_t next() // xorshift64*
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }
    double gaussian() // sum of four uniforms, unit variance, within +-3.5 sigma
    {
        double s = 0.0;
        for (int i = 0; i < 4; ++i)
            s += double (next() >> 11) * 0x1p-53;
        return (s - 2.0) * 1.7320508075688772;
    }

    uint64_t state_;
};
} // namespace tw
