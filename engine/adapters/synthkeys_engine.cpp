// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// synthkeys_engine.cpp - the synthkeys engine (the hybrid voices) behind tw::engines::SoundEngine; built with pmp renamed to
// pmp_synthkeys
#include "families.h"

#include <core/engine.h>

#include <algorithm>
#include <cmath>

namespace tw::engines
{
namespace
{
    class SynthKeysEngine final : public SoundEngine
    {
    public:
        SynthKeysEngine (const pmp::Params& p, const pmp::HybridVoicing& v) : engine_ (p), voicing_ (v)
        {
            engine_.setHybridVoicing (voicing_);
        }

        // the engine carries its bank to any rate itself (its own resampler): no second resampler in the plugin
        double rateFor (double hostRate) const override { return hostRate; }
        void prepare (double rate) override
        {
            if (rate == engine_.sampleRate())
                return;
            engine_.setSampleRate (rate);
            engine_.setHybridVoicing (voicing_); // the player is rebuilt for the rate
        }
        void noteOn (int note, int velocity) override { engine_.noteOn (note, velocity); }
        void noteOff (int note) override { engine_.noteOff (note); }
        void controller (int cc, int value) override { engine_.processMidiController (cc, value); }
        void allNotesOff() override { engine_.allNotesOff(); }
        void process (float* out, int frames) override { engine_.process (out, frames); }
        int lowestKey() const override { return std::max (pmp::kFirstKey, int (std::lround (engine_.params().keysLo))); }
        int highestKey() const override { return std::min (pmp::kLastKey, int (std::lround (engine_.params().keysHi))); }

    private:
        pmp::Engine engine_;
        pmp::HybridVoicing voicing_;
    };
} // namespace

std::unique_ptr<SoundEngine> createSynthKeysEngine (const Setup& setup, std::string* error)
{
    pmp::Params p;
    if (!p.loadPreset (setup.preset, error))
        return nullptr;
    p.trimDb += float (setup.trimDb);
    pmp::HybridVoicing v;
    v.hardness = setup.voicing.hardness;
    v.decay = setup.voicing.decay;
    v.unison = setup.voicing.unison;
    v.stretch = setup.voicing.stretch;
    return std::make_unique<SynthKeysEngine> (p, v);
}

int synthKeysPresetCount() { return pmp::Params::presetCount(); }
std::string synthKeysPresetId (int index) { return pmp::Params::presetId (index); }
} // namespace tw::engines
