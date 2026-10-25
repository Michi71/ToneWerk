// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "InstrumentVoice.h"

#include "BankLocator.h"
#include "Parameters.h"

#include <cmath>
#include <cstring>

namespace tw
{
BankImage::BankImage (const juce::File& f, int s) : slot (s), file (f)
{
    juce::FileInputStream in (f);
    if (!in.openedOk())
        return;
    const auto length = in.getTotalLength();
    if (length <= 0)
        return;
    auto* mem = static_cast<uint8_t*> (pmp::alignedAlloc (size_t (length)));
    if (mem == nullptr)
        return;
    if (in.read (mem, int (length)) != int (length))
    {
        pmp::alignedFree (mem);
        return;
    }
    data = mem;
    size = size_t (length);
}

BankImage::~BankImage()
{
    if (data != nullptr)
        pmp::alignedFree (data);
}

InstrumentVoice::InstrumentVoice (const VoiceConfig& config, std::shared_ptr<BankImage> bank)
    : config_ (config), bank_ (std::move (bank))
{
    const auto id = instrumentId (config.instrument);
    pmp::Params p;
    p.loadPreset (id.toStdString());
    p.voicing.hammerHardness = config.hardness;
    p.voicing.decay = config.decay;
    p.voicing.unison = config.unison;
    p.voicing.stretch = config.stretch;
    p.fxOn = 0.0f;     // the effect chain runs in the plugin at the host rate
    p.softClip = 0.0f; // so does the soft clip, behind it

    const bool wantsBank = p.hybridOn > 0.5f;
    const int slot = int (std::lround (p.hybridBank));
    const bool hasBank = wantsBank && bank_ && bank_->valid();
    const bool hasModel = pmp::Engine::hybridSlotHasModel (slot);
    if (wantsBank && !hasBank && hasModel)
        p.hybridOn = 0.0f; // the physical model plays instead

    engine_ = std::make_unique<pmp::Engine> (p);
    std::string err;
    if (hasBank && !engine_->setHybridBank (bank_->data, bank_->size, &err, slot))
        status_ = "Bank damaged (" + juce::String (err) + ")";
    const int bankRate = hasBank ? engine_->hybridBankRate (slot) : 0;
    engineRate_ = bankRate > 0 ? double (bankRate) : 48000.0;
    engine_->setSampleRate (engineRate_);

    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    const auto rate = juce::String (engineRate_ / 1000.0, 1) + " kHz";
    switch (engine_->hybridState())
    {
        case pmp::Engine::HybridState::Active:
            status_ = "Hybrid" + dot + bank_->file.getFileName() + dot + rate;
            break;
        case pmp::Engine::HybridState::Off:
            status_ = wantsBank ? "Bank " + bankFileName (slot) + " missing" + dot + "physical model" + dot + rate
                                : "Physical model" + dot + rate;
            break;
        case pmp::Engine::HybridState::NoBank:
        case pmp::Engine::HybridState::RateMismatch:
            silent_ = true;
            if (status_.isEmpty())
                status_ = "Bank " + bankFileName (slot) + " missing" + dot + "silent";
            break;
    }
}

void InstrumentVoice::prepare (double hostRate, int maxFrames)
{
    hostRate_ = hostRate;
    maxFrames_ = juce::jmax (1, maxFrames);
    ratio_ = engineRate_ / hostRate_;
    const int capacity = int (std::ceil (maxFrames_ * ratio_)) + 64;
    fifoL_.assign (size_t (capacity), 0.0f);
    fifoR_.assign (size_t (capacity), 0.0f);
    fifoCount_ = 0;
    interpL_.reset();
    interpR_.reset();
}

void InstrumentVoice::render (float* left, float* right, int n)
{
    jassert (n <= maxFrames_);
    if (std::abs (ratio_ - 1.0) < 1e-9)
    {
        engine_->processStereo (left, right, n);
        return;
    }
    // the interpolators take at most ceil(n * ratio) + 1 input samples for n outputs
    const int need = juce::jmin (int (std::ceil (n * ratio_)) + 2, int (fifoL_.size()));
    if (fifoCount_ < need)
    {
        engine_->processStereo (fifoL_.data() + fifoCount_, fifoR_.data() + fifoCount_, need - fifoCount_);
        fifoCount_ = need;
    }
    const int used = interpL_.process (ratio_, fifoL_.data(), left, n);
    interpR_.process (ratio_, fifoR_.data(), right, n);
    const int rest = juce::jmax (0, fifoCount_ - used);
    if (rest > 0 && used > 0)
    {
        std::memmove (fifoL_.data(), fifoL_.data() + used, size_t (rest) * sizeof (float));
        std::memmove (fifoR_.data(), fifoR_.data() + used, size_t (rest) * sizeof (float));
    }
    fifoCount_ = rest;
}
} // namespace tw
