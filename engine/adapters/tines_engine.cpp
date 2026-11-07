// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// tines_engine.cpp - the pmtines engine (the Rhodes) behind tw::engines::SoundEngine; built with pmp renamed to pmp_tines
#include "families.h"

#include <core/engine.h>

#include <algorithm>

namespace tw::engines
{
namespace
{
    constexpr double kRate = 44100.0; // the calibration rate of the presets (auto-voicing, the A/B against the recordings)

    class TinesEngine final : public SoundEngine
    {
    public:
        explicit TinesEngine (const pmp::Params& p) : engine_ (p) {} // prepared at 44.1 kHz

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
        int lowestKey() const override { return pmp::kFirstKey; }
        int highestKey() const override { return pmp::kLastKey; }

    private:
        pmp::Engine engine_;
    };
} // namespace

std::unique_ptr<SoundEngine> createTinesEngine (const Setup& setup, std::string* error)
{
    pmp::Params p;
    if (!p.loadPreset (setup.preset, error))
        return nullptr;
    const auto& v = setup.voicing;
    p.voicing.hammerHardness = v.hardness;
    p.voicing.hammerMass = v.hammerMass;
    p.voicing.strikePoint = v.strikePoint;
    p.voicing.tineGauge = v.gauge;
    p.voicing.pickupGap = v.pickupGap;
    p.voicing.pickupOffset = v.pickupOffset;
    p.voicing.tonebarDetune = v.tonebarDetune;
    p.voicing.damperRelease = v.damperRelease;
    if (!setup.strikeScatter)
        p.humanSpeed = 0.0f;
    // A harder or heavier hammer on a thinner tine stiffens the contact beyond what the engine's two sub-steps per sample
    // integrate: combined extremes (hardness +200, mass +200, gauge -50) ran away to inf. Sixteen keep every corner of the
    // voicing ranges stable (tests "Random voicing combinations"); neutral and softer voicings keep the calibrated two.
    if (v.hardness > 0.0f || v.hammerMass > 0.0f || v.gauge < 0.0f)
        p.hammerSubsteps = std::max (p.hammerSubsteps, 16.0f);
    p.trimDb += float (setup.trimDb);
    return std::make_unique<TinesEngine> (p);
}

int tinesPresetCount() { return pmp::Params::presetCount(); }
std::string tinesPresetId (int index) { return pmp::Params::presetId (index); }
} // namespace tw::engines
