// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// reeds_engine.cpp - the reed engine (Wurlitzer, Pianet) behind tw::engines::SoundEngine; built with pmp renamed to pmp_reeds
#include "families.h"

#include <core/engine.h>

#include <algorithm>

#include <cmath>

namespace tw::engines
{
namespace
{
    constexpr double kRate = 44100.0; // the calibration rate of the presets (auto-voicing, the A/B against the recordings)

    class ReedsEngine final : public SoundEngine
    {
    public:
        explicit ReedsEngine (const pmp::Params& p) : engine_ (p) {} // prepared at 44.1 kHz

        double rateFor (double) const override { return kRate; }
        void prepare (double rate) override
        {
            if (rate != engine_.sampleRate())
                engine_.setSampleRate (rate);
        }
        void noteOn (int note, int velocity) override { engine_.noteOn (note, velocity); }
        void noteOff (int note) override { engine_.noteOff (note); }
        void controller (int cc, int value) override { engine_.processMidiController (cc, value); }
        void allNotesOff() override { engine_.allNotesOff(); }
        void process (float* out, int frames) override { engine_.process (out, frames); }
        int activeKeys() const override { return engine_.activeKeys(); }
        int lowestKey() const override { return std::max (pmp::kFirstKey, int (std::lround (engine_.params().keysLo))); }
        int highestKey() const override { return std::min (pmp::kLastKey, int (std::lround (engine_.params().keysHi))); }

    private:
        pmp::Engine engine_;
    };
} // namespace

std::unique_ptr<SoundEngine> createReedsEngine (const Setup& setup, std::string* error)
{
    pmp::Params p;
    if (!p.loadPreset (setup.preset, error))
        return nullptr;
    const auto& v = setup.voicing;
    p.voicing.hammerHardness = v.hardness;
    p.voicing.hammerMass = v.hammerMass;
    p.voicing.strikePoint = v.strikePoint;
    p.voicing.reedGauge = v.gauge;
    p.voicing.pickupGap = v.pickupGap;
    p.voicing.pickupOffset = v.pickupOffset;
    p.voicing.damperRelease = v.damperRelease;
    if (!setup.strikeScatter)
        p.humanSpeed = 0.0f;
    // A harder or heavier hammer on a thinner reed stiffens the contact beyond what the engine's two sub-steps per sample
    // integrate: combined extremes ran away to inf (and a NaN reed position indexes the Wurlitzer's pickup table out of
    // bounds on x86). Within the plugin's ranges (mass up to +100 %, gauge down to -30 %) eight sub-steps keep every corner
    // finite; 32 leave a margin of four for other compilers and maths libraries. Neutral and softer voicings keep the
    // calibrated two, so the instruments as calibrated are unchanged.
    if (v.hardness > 0.0f || v.hammerMass > 0.0f || v.gauge < 0.0f)
        p.hammerSubsteps = std::max (p.hammerSubsteps, 32.0f);
    p.trimDb += float (setup.trimDb);
    p.maxVoices = float (std::max (0, setup.maxVoices));
    return std::make_unique<ReedsEngine> (p);
}

int reedsPresetCount() { return pmp::Params::presetCount(); }
std::string reedsPresetId (int index) { return pmp::Params::presetId (index); }
} // namespace tw::engines
