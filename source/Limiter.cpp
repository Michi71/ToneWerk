// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "Limiter.h"

#include <algorithm>
#include <cmath>

namespace tw
{
void Limiter::prepare (double sampleRate)
{
    attack_ = float (1.0 - std::exp (-1.0 / (kAttackMs * 1e-3 * sampleRate)));
    release_ = float (1.0 - std::exp (-1.0 / (kReleaseMs * 1e-3 * sampleRate)));
    reset();
}

void Limiter::process (float* left, float* right, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
        const float target = peak > kThreshold ? kThreshold / peak : 1.0f;
        if (target < gain_)
            gain_ += attack_ * (target - gain_);
        else
        {
            gain_ += release_ * (target - gain_);
            if (gain_ > 1.0f - 1e-6f)
                gain_ = 1.0f; // recovered: bit-transparent again
        }
        if (gain_ < 1.0f)
        {
            left[i] *= gain_;
            if (right != nullptr)
                right[i] *= gain_;
        }
    }
}
} // namespace tw
