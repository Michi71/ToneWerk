// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// engine.cpp - the synthkeys hybrid instrument: bank players, pedal, output
#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "denormals.h"
#include "hybrid_splendid_bank.h"
#include "hybrid_clav_bank.h"
#include "hybrid_upright_bank.h"
#include "hybrid_salamander_bank.h"

namespace pmp {
namespace {

// The embedded banks an instrument can select (hybrid.bank).
struct BankImage { const uint8_t* data; size_t size; };
BankImage bankImage(int id) {
    if (id == 1) return {kHybridClavBank, kHybridClavBankSize};
    if (id == 2) return {kHybridUprightBank, kHybridUprightBankSize};
    if (id == 3) return {kHybridSalamanderBank, kHybridSalamanderBankSize};
    return {kHybridSplendidBank, kHybridSplendidBankSize};
}

} // namespace

Engine::Engine() { prepare(); }

Engine::Engine(const Params& params) : params_(params) { prepare(); }

void Engine::setParams(const Params& params) { params_ = params; }

void Engine::setSampleRate(double fs) {
    fs_ = fs;
    prepare();
}

void Engine::prepare() {
    trim_ = real(dbToGain(double(params_.trimDb)));
    hpf_.setup(7.0, fs_);             // DC guard behind the voice (the data is DC-free)
    hpf_.reset();
    updateHybrid();
}

// The embedded bank decides which player runs. The player runs at the bank's own rate
// (22050 or 44100 Hz); a host at another rate gets the player's output through the
// resampler - no guessing about the bank's rate, it plays as fitted.
void Engine::updateHybrid() {
    if (hybrid_) hybrid_->reset();
    hybridActive_ = false;
    const BankImage img = bankImage(int(params_.hybridBank + 0.5f));
    if (img.data != hybridBankData_ || img.size != hybridBankSize_) {
        hybrid_.reset();                       // the player points into the old image
        hybridBank_ = HybridBank();
        hybridBankData_ = img.data;
        hybridBankSize_ = img.size;
    }
    if (!hybridBank_.valid()) {
        std::string err;
        if (!hybridBank_.attach(img.data, img.size, &err)) {
            std::fprintf(stderr, "hybrid bank: %s\n", err.c_str());
            hybrid_.reset();
            return;
        }
    }
    if (!hybrid_) hybrid_.reset(new HybridPlayer);
    HybridParams hp;
    hp.rampMs = params_.hybridRampMs;                       // 0: the bank's
    const int bankRate = hybridBank_.rate();
    if (!hybrid_->prepare(bankRate, hybridBank_, hp)) return;
    hybrid_->setSlowPhase(double(params_.hybridSlowShare), double(params_.hybridSlowT60));
    hybrid_->setMaxKeys(maxVoices());
    HybridResonance res;
    res.on = params_.resOn >= 0.5f;
    res.gain = double(params_.resGain);
    res.modes = std::max(1, int(params_.resModes + 0.5f));
    res.tiltDb = double(params_.resTiltDb);
    res.undamped = params_.resUndamped >= 0.5f;
    hybrid_->setResonance(res);
    hybridResample_ = std::fabs(fs_ - bankRate) > 0.5;
    if (hybridResample_) hybridRes_.setup(bankRate, fs_);
    hybridRes_.reset();
    hybHave_ = hybPos_ = 0;
    hybridActive_ = true;
}

int Engine::maxVoices() const {
    const int n = int(params_.maxVoices + 0.5f);
    return n <= 0 ? kNumKeys : std::min(n, kNumKeys);
}

int Engine::activeKeys() const {
    return hybridActive_ && hybrid_ ? hybrid_->sustainingKeys() : 0;
}

void Engine::noteOn(int note, int velocity) {
    if (!hybridActive_ || !playable(note)) return;
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    hybrid_->noteOn(note, velocity);
}

void Engine::noteOff(int note) {
    if (!inRange(note)) return;
    if (hybridActive_) hybrid_->noteOff(note);
}

void Engine::processMidiController(int cc, int value) {
    switch (cc) {
        case 7:
            volume_ = real(value / 127.0);
            break;
        case 64: {
            // half pedal: the position is continuous; the player's damper follows the rail
            pedalPos_ = real(std::min(std::max(value, 0), 127)) / 127;
            sustain_ = pedalPos_ >= real(0.5);
            if (hybridActive_) hybrid_->controller(64, value);
            break;
        }
        case 120:
            allSoundOff();
            break;
        case 123:
            allNotesOff();
            break;
        default:
            break;
    }
}

void Engine::allNotesOff() {
    sustain_ = false;
    pedalPos_ = 0;
    if (hybridActive_) hybrid_->controller(123, 0);
}

void Engine::allSoundOff() {
    allNotesOff();
    // the engine's allSoundOff is immediate (bit-silent); the player's own allSoundOff
    // fades for 5 ms and then resets - which would also kill a note struck right after
    // the call (pmp-render's renderNote does exactly that)
    if (hybrid_) {
        hybrid_->reset();
        hybridRes_.reset();
        hybHave_ = hybPos_ = 0;
    }
    hpf_.reset();
}

void Engine::setHybridVoicing(const HybridVoicing& v) {
    if (hybridActive_) hybrid_->setVoicing(v);
}

void Engine::setHybridResonance(const HybridResonance& r) {
    if (hybridActive_) hybrid_->setResonance(r);
}

// The output: the DC guard behind the voice, then the level (out.gain, volume, out.trimDb).
// No effects, no clip: the application behind the instrument does that. The hybrid voice
// renders through its own player - at the bank's rate, resampled to fs_ in fixed chunks,
// so the output does not depend on the host's block size - and takes the same output chain.
void Engine::process(float* out, int nFrames) {
    ScopedFlushDenormals noDenormals;
    const real gain = real(params_.masterGain) * volume_ * trim_;
    if (!hybridActive_) {                     // no bank: silence
        std::fill(out, out + nFrames, 0.0f);
        return;
    }
    if (!hybridResample_) {
        hybrid_->process(out, nFrames);
        for (int i = 0; i < nFrames; ++i) out[i] = float(hpf_.process(out[i]) * gain);
        return;
    }
    int done = 0;
    while (done < nFrames) {
        if (hybPos_ >= hybHave_) {            // the leftover outputs are used up: refill
            hybrid_->process(hybIn_, kHybridIn);
            hybPos_ = hybHave_ = 0;
            hybHave_ = hybridRes_.process(hybIn_, kHybridIn, hybOut_);
        }
        const int m = std::min(nFrames - done, hybHave_ - hybPos_);
        for (int i = 0; i < m; ++i) out[done + i] = float(hpf_.process(hybOut_[hybPos_ + i]) * gain);
        done += m;
        hybPos_ += m;
    }
}

} // namespace pmp
