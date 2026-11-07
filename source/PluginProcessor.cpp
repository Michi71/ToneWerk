// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

#include <core/dsp.h>

namespace ids = tw::ids;

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("ToneWerk loader"),
      apvts_ (*this, nullptr, "ToneWerk", tw::createParameterLayout()),
      presets_ (apvts_),
      midiLearn_ (apvts_)
{
    auto raw = [this] (const char* id) {
        auto* p = apvts_.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    instrument_ = raw (ids::instrument);
    volume_ = raw (ids::volume);
    hardness_ = raw (ids::hardness);
    decay_ = raw (ids::decay);
    unison_ = raw (ids::unison);
    stretch_ = raw (ids::stretch);
    hammerMass_ = raw (ids::hammerMass);
    strikePoint_ = raw (ids::strikePoint);
    gauge_ = raw (ids::gauge);
    pickupGap_ = raw (ids::pickupGap);
    pickupOffset_ = raw (ids::pickupOffset);
    tonebarDetune_ = raw (ids::tonebarDetune);
    damperRelease_ = raw (ids::damperRelease);
    humanizeAmount_ = raw (ids::humanize);
    fxp_ = { raw (ids::fxOn), raw (ids::fxVolume), raw (ids::fxDrive), raw (ids::fxEnvelope), raw (ids::fxBass), raw (ids::fxMid),
        raw (ids::fxMidFreq), raw (ids::fxTreble), raw (ids::panOn), raw (ids::panWave), raw (ids::panRate), raw (ids::panDepth),
        raw (ids::compOn), raw (ids::compAmount), raw (ids::compMakeup), raw (ids::chorusOn), raw (ids::chorusRate),
        raw (ids::chorusDepth), raw (ids::phaserOn), raw (ids::phaserRate), raw (ids::phaserDepth), raw (ids::delayOn),
        raw (ids::delayTime), raw (ids::delayFeedback), raw (ids::delayMix), raw (ids::delayPingPong) };
    rvp_ = { raw (ids::reverbOn), raw (ids::reverbMix), raw (ids::reverbSize), raw (ids::reverbDecay), raw (ids::reverbDamping),
        raw (ids::reverbPredelay), raw (ids::reverbWidth) };

    // a fresh instance is the first factory preset (the parameter defaults)
    apvts_.state.setProperty (tw::PresetManager::kNameProperty, presets_.factoryNames()[0], nullptr);
    apvts_.state.setProperty ("presetFactory", true, nullptr);

    // the delay lines are allocated for the longest time once
    fx_.setDelayTime (4.0);
    fx_.init (48000.0);
    scratch_.setSize (5, kChunk);
    reverb_.prepare (48000.0);
    startThread (juce::Thread::Priority::normal);
    startTimer (10);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    stopThread (4000);
    delete pending_.exchange (nullptr);
    delete retired_.exchange (nullptr);
}

// --- loader thread ------------------------------------------------------------------------------------------------------

tw::VoiceConfig PluginProcessor::wantedConfig() const
{
    tw::VoiceConfig c;
    c.instrument = int (std::lround (instrument_->load()));
    auto& v = c.voicing;
    v.hardness = hardness_->load();
    v.decay = decay_->load();
    v.unison = unison_->load();
    v.stretch = stretch_->load();
    v.hammerMass = hammerMass_->load();
    v.strikePoint = strikePoint_->load();
    v.gauge = gauge_->load();
    v.pickupGap = pickupGap_->load();
    v.pickupOffset = pickupOffset_->load();
    v.tonebarDetune = tonebarDetune_->load();
    v.damperRelease = damperRelease_->load();
    return c;
}

static int64_t signatureOf (const tw::VoiceConfig& c)
{
    auto h = int64_t (c.instrument) * 1000003;
    for (float v : c.voicingValues())
        h = h * 31 + int64_t (std::lround (v * 100.0f));
    return h;
}

int64_t PluginProcessor::wantedSignature() const { return signatureOf (wantedConfig()); }

void PluginProcessor::build (const tw::VoiceConfig& config)
{
    auto voice = std::make_unique<tw::InstrumentVoice> (config);
    voice->prepare (hostRate_.load(), kChunk);
    {
        const juce::ScopedLock sl (statusLock_);
        status_ = voice->status();
    }
    keyLo_.store (voice->lowestKey());
    keyHi_.store (voice->highestKey());
    delete pending_.exchange (voice.release());
}

void PluginProcessor::run()
{
    tw::VoiceConfig built, last;
    bool haveBuilt = false;
    auto changedAt = juce::Time::getMillisecondCounter();
    while (!threadShouldExit())
    {
        wait (25);
        delete retired_.exchange (nullptr);

        const auto want = wantedConfig();
        const auto now = juce::Time::getMillisecondCounter();
        if (!(want == last))
        {
            last = want;
            changedAt = now;
        }
        const bool rebuild = rebuild_.exchange (false);
        if (haveBuilt && want == built && !rebuild)
            continue;
        // an instrument switch at once; the voicing knobs once they rest for a moment (every change rebuilds the engine)
        const bool switched = !haveBuilt || want.instrument != built.instrument;
        if (!switched && !rebuild && now - changedAt < 250)
            continue;
        build (want);
        built = want;
        haveBuilt = true;
    }
}

juce::String PluginProcessor::instrumentStatus() const
{
    const juce::ScopedLock sl (statusLock_);
    return status_;
}

// --- audio thread -------------------------------------------------------------------------------------------------------

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate_.store (sampleRate);
    fx_.setDelayTime (4.0);
    fx_.init (sampleRate);
    fxGain_ = 0.0f;
    reverb_.prepare (sampleRate);
    reverbGain_ = 0.0f;
    limiter_.prepare (sampleRate);
    switchStep_ = float (1.0 / (kSwitchMs * 1e-3 * sampleRate));
    gain_.reset (sampleRate, 0.02);
    gain_.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volume_->load(), -48.0f));
    fadeStep_ = float (1.0 / (kFadeMs * 1e-3 * sampleRate));
    fading_.reset(); // not real time here
    if (active_ && !juce::exactlyEqual (active_->hostRate(), sampleRate))
        active_->prepare (sampleRate, kChunk);
    if (auto* p = pending_.load(); p != nullptr && !juce::exactlyEqual (p->hostRate(), sampleRate))
        rebuild_.store (true); // adoptPending() refuses it, the loader builds one for this rate
}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && layouts.getMainInputChannels() == 0;
}

void PluginProcessor::retire (std::unique_ptr<tw::InstrumentVoice>& voice)
{
    tw::InstrumentVoice* expected = nullptr;
    if (retired_.compare_exchange_strong (expected, voice.get()))
        voice.release();
}

void PluginProcessor::adoptPending()
{
    if (fading_ != nullptr || pending_.load() == nullptr)
        return; // one switch at a time: the previous fade is still running or not collected yet
    std::unique_ptr<tw::InstrumentVoice> next (pending_.exchange (nullptr));
    if (next == nullptr)
        return;
    if (!juce::exactlyEqual (next->hostRate(), hostRate_.load()))
    {
        retire (next);
        if (next != nullptr) // the retire slot is busy: try again next block
            pending_.store (next.release());
        rebuild_.store (true);
        return;
    }
    next->controller (64, sustain_);
    fading_ = std::move (active_);
    fadeGain_ = 1.0f;
    active_ = std::move (next);
    readyGeneration_.store (signatureOf (active_->config()));
}

void PluginProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isController())
    {
        if (m.getControllerNumber() == 64)
            sustain_ = m.getControllerValue();
        if (midiLearn_.handleController (m.getControllerNumber(), m.getControllerValue()))
            return; // learned: it sets a parameter instead
    }
    else if (m.isProgramChange())
    {
        pendingProgram_.store (m.getProgramChangeNumber());
        return;
    }
    if (active_ == nullptr)
        return;
    if (m.isNoteOn())
        active_->noteOn (m.getNoteNumber(), humanize_.apply (m.getVelocity(), double (humanizeAmount_->load()) * 0.01));
    else if (m.isNoteOff())
        active_->noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff())
        active_->allNotesOff();
    else if (m.isController())
        active_->controller (m.getControllerNumber(), m.getControllerValue());
}

void PluginProcessor::renderSegment (float* left, float* right, int n)
{
    if (active_ != nullptr)
        active_->render (left, right, n);
    else
    {
        juce::FloatVectorOperations::clear (left, n);
        juce::FloatVectorOperations::clear (right, n);
    }
    if (fading_ == nullptr)
        return;
    float* fl = scratch_.getWritePointer (0);
    float* fr = scratch_.getWritePointer (1);
    fading_->render (fl, fr, n);
    for (int i = 0; i < n; ++i)
    {
        left[i] += fadeGain_ * fl[i];
        right[i] += fadeGain_ * fr[i];
        fadeGain_ = juce::jmax (0.0f, fadeGain_ - fadeStep_);
    }
    if (fadeGain_ <= 0.0f)
        retire (fading_); // stays (silent) when the loader has not collected the previous one yet
}

void PluginProcessor::updateEffects()
{
    auto on = [] (std::atomic<float>* p) { return p->load() > 0.5f; };
    fx_.setVolume (fxp_.volume->load());
    fx_.setDrive (fxp_.drive->load());
    fx_.setEnvelope (fxp_.envelope->load());
    fx_.setBass (fxp_.bass->load());
    fx_.setMiddle (fxp_.mid->load());
    fx_.setMiddleFreq (fxp_.midFreq->load());
    fx_.setTreble (fxp_.treble->load());
    fx_.setPanOn (on (fxp_.panOn));
    fx_.setPanWave (int (std::lround (fxp_.panWave->load())));
    fx_.setPanRate (fxp_.panRate->load());
    fx_.setPanDepth (fxp_.panDepth->load());
    fx_.setCompressorOn (on (fxp_.compOn));
    fx_.setCompAmount (fxp_.compAmount->load());
    fx_.setCompMakeup (fxp_.compMakeup->load());
    fx_.setChorusOn (on (fxp_.chorusOn));
    fx_.setChorusRate (fxp_.chorusRate->load());
    fx_.setChorusDepth (fxp_.chorusDepth->load());
    fx_.setPhaserOn (on (fxp_.phaserOn));
    fx_.setPhaserRate (fxp_.phaserRate->load());
    fx_.setPhaserDepth (fxp_.phaserDepth->load());
    fx_.setDelayOn (on (fxp_.delayOn));
    fx_.setDelayTime (fxp_.delayTime->load());
    fx_.setDelayFeedback (fxp_.delayFeedback->load());
    fx_.setDelayMix (fxp_.delayMix->load());
    fx_.setDelayPingPong (on (fxp_.delayPingPong));
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboardState_.processNextMidiBuffer (midi, 0, n, true);
    adoptPending();

    const bool mono = buffer.getNumChannels() == 1;
    float* left = buffer.getWritePointer (0);
    float* right = mono ? nullptr : buffer.getWritePointer (1);
    auto it = midi.begin();
    int pos = 0;
    while (pos < n)
    {
        for (; it != midi.end() && (*it).samplePosition <= pos; ++it)
            handleMidi ((*it).getMessage());
        const int next = juce::jmin (it != midi.end() ? juce::jmax (pos + 1, (*it).samplePosition) : n, n, pos + kChunk);
        float* r = mono ? scratch_.getWritePointer (2) : right + pos;
        renderSegment (left + pos, r, next - pos);
        if (mono)
            for (int i = 0; i < next - pos; ++i)
                left[pos + i] = 0.5f * (left[pos + i] + r[i]);
        pos = next;
    }
    for (; it != midi.end(); ++it)
        handleMidi ((*it).getMessage());

    // the MK8 effect chain (stereo only), then the reverb on its own switch (the grands want it without the MK8 preamp), both at
    // the host rate. A switch cross-fades the stage in or out over kSwitchMs: the hard switch clicked (measured: the largest
    // step 4.7 times the chord's own when the chain came in)
    const bool fxOn = fxp_.on->load() > 0.5f && !mono;
    const bool reverbOn = rvp_.on->load() > 0.5f;
    if (fxOn || fxGain_ > 0.0f)
        updateEffects();
    if (reverbOn || reverbGain_ > 0.0f)
    {
        tw::Reverb::Settings rs;
        rs.mix = rvp_.mix->load() * 0.01f;
        rs.size = rvp_.size->load() * 0.01f;
        rs.decay = rvp_.decay->load();
        rs.damping = rvp_.damping->load() * 0.01f;
        rs.predelayMs = rvp_.predelay->load();
        rs.width = rvp_.width->load() * 0.01f;
        reverb_.setSettings (rs);
    }
    auto stage = [this] (float* l, float* r, int m, float& gain, bool on, auto&& process, auto&& restart) {
        if (!on && gain <= 0.0f)
            return;
        if (on && gain <= 0.0f)
            restart(); // from the bypass: no stale state
        if (on && gain >= 1.0f)
        {
            process (l, r, m);
            return;
        }
        float* wl = scratch_.getWritePointer (3);
        float* wr = scratch_.getWritePointer (4);
        juce::FloatVectorOperations::copy (wl, l, m);
        juce::FloatVectorOperations::copy (wr, r, m);
        process (wl, wr, m);
        const float target = on ? 1.0f : 0.0f;
        for (int i = 0; i < m; ++i)
        {
            gain = on ? juce::jmin (target, gain + switchStep_) : juce::jmax (target, gain - switchStep_);
            l[i] += gain * (wl[i] - l[i]);
            r[i] += gain * (wr[i] - r[i]);
        }
    };
    for (int done = 0; done < n; done += kChunk)
    {
        const int m = juce::jmin (kChunk, n - done);
        float* l = left + done;
        float* r = mono ? scratch_.getWritePointer (2) : right + done;
        if (mono)
            juce::FloatVectorOperations::copy (r, l, m);
        stage (l, r, m, fxGain_, fxOn, [this] (float* a, float* b, int k) { fx_.process (a, b, k); }, [this] { fx_.reset(); });
        stage (l, r, m, reverbGain_, reverbOn, [this] (float* a, float* b, int k) { reverb_.process (a, b, k); },
            [this] { reverb_.reset(); });
        if (mono)
            for (int i = 0; i < m; ++i)
                l[i] = 0.5f * (l[i] + r[i]);
    }

    // volume, the limiter, the soft clip as the last safeguard
    gain_.setTargetValue (juce::Decibels::decibelsToGain (volume_->load(), -48.0f));
    for (int i = 0; i < n; ++i)
    {
        const float g = gain_.getNextValue();
        left[i] *= g;
        if (!mono)
            right[i] *= g;
    }
    limiter_.process (left, mono ? nullptr : right, n);
    for (int i = 0; i < n; ++i)
    {
        left[i] = pmp::softClip (left[i]);
        if (!mono)
            right[i] = pmp::softClip (right[i]);
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

// --- message thread: MIDI that changes the plugin --------------------------------------------------------------------------

void PluginProcessor::dispatchMidi()
{
    midiLearn_.dispatch();
    if (const int program = pendingProgram_.exchange (-1); program >= 0 && program < getNumPrograms())
    {
        // a program change from the keyboard loads the preset even when it is the current one (back to its saved state)
        lastProgram_ = program;
        presets_.loadFactory (program);
    }
}

int PluginProcessor::getNumPrograms() { return presets_.factoryNames().size(); }

int PluginProcessor::getCurrentProgram()
{
    if (presets_.currentIsFactory())
        if (const int index = presets_.factoryNames().indexOf (presets_.currentName()); index >= 0)
            return index;
    return lastProgram_; // a user preset or none: the program last chosen
}

void PluginProcessor::setCurrentProgram (int index)
{
    // hosts echo the current program back after restoring a project: that must not reset the parameters to the preset
    if (index < 0 || index >= getNumPrograms() || index == getCurrentProgram())
        return;
    lastProgram_ = index;
    presets_.loadFactory (index);
}

const juce::String PluginProcessor::getProgramName (int index) { return presets_.factoryNames()[index]; }

// --- state ---------------------------------------------------------------------------------------------------------------

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts_.state.getType()))
        apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PluginProcessor(); }
