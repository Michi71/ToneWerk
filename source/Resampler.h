// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Resampler.h - short windowed-sinc resampler for the engine output (bank rate -> host rate), stereo
//
// A Kaiser-windowed sinc of 2 * kHalfTaps taps (scaled by the ratio when downsampling), read from a table with linear
// interpolation between kPhases phases per input sample. The kernel is centred on the output instant, so the only delay is
// the lookahead of kHalfTaps input samples the engine has to render ahead (0.54 ms at 44.1 kHz; JUCE's WindowedSinc needs
// 100 samples). The input arrives through write pointers into an internal buffer: prepare() allocates, the rest is
// real-time safe.
#pragma once

#include <vector>

namespace tw
{
class Resampler
{
public:
    static constexpr int kHalfTaps = 24;  // input samples on each side of the output instant (upsampling)
    static constexpr int kPhases = 512;   // table resolution per input sample
    static constexpr double kBeta = 7.0;  // Kaiser window: ~70 dB stopband
    static constexpr double kCutoff = 0.9; // passband edge: this fraction of the lower Nyquist frequency

    // ratio = input rate / output rate; maxOut = the most output frames of one process() call. Allocates.
    void prepare (double ratio, int maxOut);
    void reset();

    double ratio() const { return ratio_; }
    int lookahead() const { return half_; } // input samples rendered ahead of the output instant

    // the input frames to append before process (n) can produce n output frames, and where to write them
    int inputNeeded (int n) const;
    float* writeLeft() { return left_.data() + count_; }
    float* writeRight() { return right_.data() + count_; }
    void appended (int frames) { count_ += frames; }

    void process (float* left, float* right, int n);

private:
    float tap (double t) const; // the kernel at t input samples from the output instant

    double ratio_ = 1.0, pos_ = 0.0, tableScale_ = 1.0;
    int half_ = kHalfTaps, count_ = 0;
    std::vector<float> table_;  // kernel from 0 to half_ (symmetric), kPhases points per input sample
    std::vector<float> left_, right_;
};
} // namespace tw
