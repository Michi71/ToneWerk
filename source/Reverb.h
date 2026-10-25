// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Reverb.h - stereo feedback-delay-network reverb, the last stage of the effect chain
//
// Pre-delay -> early reflections (taps on the pre-delay lines) and four Schroeder allpasses per channel -> an FDN of eight
// delay lines mixed by a normalised Hadamard matrix. Each line carries a one-pole absorption filter whose gains at DC and at
// Nyquist give the set decay time and its high-frequency share (Jot): the RT60 is the same for every line and frequency
// dependent through the damping. The line lengths glide when the size changes (no clicks) and are slowly modulated against
// metallic ringing. prepare() allocates; setSettings() and process() are real-time safe.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace tw
{
class Reverb
{
public:
    struct Settings
    {
        float mix = 0.25f;       // 0..1, equal-power cross-fade between dry and wet
        float size = 0.5f;       // 0..1: line lengths, reflection times and diffusion scale 0.45..1.55
        float decay = 2.0f;      // RT60 [s] at low frequencies
        float damping = 0.5f;    // 0..1: the RT60 at Nyquist is (1 - 0.85 damping) of the low one
        float predelayMs = 20.0f; // 0..200 ms
        float width = 1.0f;      // 0..1: stereo width of the wet signal
    };

    static constexpr int kLines = 8;
    static constexpr float kMaxPredelayMs = 200.0f;

    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s);
    void process (float* left, float* right, int n); // in place

private:
    struct Line
    {
        std::vector<float> buf;
        uint32_t mask = 0, pos = 0;
        void init (int minLength);
        void clear();
        void write (float x)
        {
            buf[pos] = x;
            pos = (pos + 1) & mask;
        }
        float tap (int delay) const { return buf[(pos - 1u - uint32_t (delay)) & mask]; }
        float read (float delay) const; // fractional (linear interpolation) on top of tap()
    };
    struct Allpass
    {
        Line line;
        int delay = 1;
        float g = 0.7f;
        float process (float x);
    };

    void updateTargets();

    double fs_ = 48000.0;
    Settings set_ {};
    bool dirty_ = true;

    Line preL_, preR_;
    std::array<Allpass, 4> diffL_ {}, diffR_ {};
    std::array<Line, kLines> lines_ {};
    std::array<float, kLines> length_ {}, lengthTarget_ {}; // samples, gliding
    std::array<float, kLines> b0_ {}, a1_ {}, state_ {};    // absorption filters
    std::array<float, kLines> apState_ {};                  // allpass interpolation of the modulated read
    std::array<double, kLines> lfoPhase_ {}, lfoInc_ {};
    float modDepth_ = 0.0f, glide_ = 0.0f;
    float predelay_ = 0.0f, predelayTarget_ = 0.0f;
    std::array<float, 8> erDelay_ {};
    float dry_ = 1.0f, wet_ = 0.0f, dryTarget_ = 1.0f, wetTarget_ = 0.0f, width_ = 1.0f, widthTarget_ = 1.0f;
    float smooth_ = 0.0f;
};
} // namespace tw
