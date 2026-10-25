// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Limiter.h - stereo-linked peak limiter in front of the engine's soft clip
//
// Full chords drive every instrument above full scale (eight notes at velocity 127: up to +14 dB), and the soft clip, the
// engine's last safeguard, distorts audibly when it works. The limiter turns the gain down instead: attack 0.1 ms, release
// 120 ms, no lookahead (no added latency), the soft clip catches what the attack lets through (measured with loud chords:
// 0.3 ms left 3-10 times more samples in the clip, 0.03 ms a few times fewer, with the gain stepping within two samples).
// Below the threshold it passes the signal bit for bit.
#pragma once

namespace tw
{
class Limiter
{
public:
    static constexpr float kThreshold = 0.85f; // below the soft clip's knee at 0.9
    static constexpr double kAttackMs = 0.1, kReleaseMs = 120.0;

    void prepare (double sampleRate);
    void reset() { gain_ = 1.0f; }
    void process (float* left, float* right, int n); // right may be nullptr (mono)
    float gain() const { return gain_; }

private:
    float gain_ = 1.0f, attack_ = 1.0f, release_ = 0.0f;
};
} // namespace tw
