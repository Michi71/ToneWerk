// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// PluginProcessor.h - ToneWerk: the Rhodes, reed and hybrid instruments with the MK8 effect chain as a plugin
//
// Threads: the audio thread plays the active InstrumentVoice and the effect chain. A loader thread builds a new voice whenever
// the instrument or the voicing changes (engine preparation is not real-time safe) and hands
// it over through an atomic pointer; the audio thread adopts it at the next block, fades the old one out and hands that back
// for deletion. Nothing is allocated or freed on the audio thread.
//
// MIDI that changes the plugin itself (a learned CC, a program change) is not acted on in processBlock: the audio thread leaves
// it in atomic slots and a timer on the message thread sets the parameters or loads the preset, notifying the host.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <core/mk8_fx.h>

#include "InstrumentVoice.h"
#include "Humanize.h"
#include "Limiter.h"
#include "MidiLearn.h"
#include "PresetManager.h"
#include "Reverb.h"

#include <atomic>
#include <utility>

class PluginProcessor : public juce::AudioProcessor, private juce::Thread, private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; } // the longest reverb

    // One program for the host. The factory presets as host programs gave VST3 an automatable "Program" parameter that loads a
    // preset over every other parameter, and pluginval's state restoration failed on it; a MIDI program change loads the
    // factory preset inside the plugin instead (dispatchMidi).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "ToneWerk"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts_; }
    tw::PresetManager& presets() { return presets_; }
    tw::MidiLearn& midiLearn() { return midiLearn_; }
    // message thread: acts on the learned CCs and the program change the audio thread received (the timer calls it)
    void dispatchMidi();
    juce::MidiKeyboardState& keyboardState() { return keyboardState_; }

    // what the loaded instrument says about itself (engine, rate); message thread
    juce::String instrumentStatus() const;
    // the keys the loaded instrument plays (MIDI notes); any thread
    std::pair<int, int> instrumentKeys() const { return { keyLo_.load(), keyHi_.load() }; }
    // the strike variation's random sequence, for repeatable tests
    void seedHumanize (uint64_t seed) { humanize_.seed (seed); }
    // true when the voice built for the current parameters plays (tests; the audio thread adopts it in processBlock)
    bool instrumentReady() const { return readyGeneration_.load() == wantedSignature(); }

private:
    static constexpr int kChunk = 512;      // largest render call into a voice [host frames]
    static constexpr double kFadeMs = 30.0;   // fade-out of the old voice after a switch
    static constexpr double kSwitchMs = 20.0; // cross-fade of the FX and reverb switches

    void run() override; // the loader thread
    void timerCallback() override { dispatchMidi(); }
    tw::VoiceConfig wantedConfig() const;
    int64_t wantedSignature() const;
    void build (const tw::VoiceConfig& config);

    void adoptPending();
    void retire (std::unique_ptr<tw::InstrumentVoice>& voice);
    void handleMidi (const juce::MidiMessage& m);
    void renderSegment (float* left, float* right, int n);
    void updateEffects();

    juce::AudioProcessorValueTreeState apvts_;
    tw::PresetManager presets_;
    tw::MidiLearn midiLearn_;
    juce::MidiKeyboardState keyboardState_;
    std::atomic<int> pendingProgram_ { -1 }; // audio -> message thread

    // parameters read on the audio and loader threads
    std::atomic<float>* instrument_ = nullptr;
    std::atomic<float>* volume_ = nullptr;
    std::atomic<float>* hardness_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* unison_ = nullptr;
    std::atomic<float>* stretch_ = nullptr;
    std::atomic<float>*hammerMass_ = nullptr, *strikePoint_ = nullptr, *gauge_ = nullptr, *pickupGap_ = nullptr,
                       *pickupOffset_ = nullptr, *tonebarDetune_ = nullptr, *damperRelease_ = nullptr;
    std::atomic<float>* humanizeAmount_ = nullptr;
    struct FxParams
    {
        std::atomic<float>*on, *volume, *drive, *envelope, *bass, *mid, *midFreq, *treble, *panOn, *panWave, *panRate, *panDepth,
            *compOn, *compAmount, *compMakeup, *chorusOn, *chorusRate, *chorusDepth, *phaserOn, *phaserRate, *phaserDepth,
            *delayOn, *delayTime, *delayFeedback, *delayMix, *delayPingPong;
    } fxp_ {};
    struct ReverbParams
    {
        std::atomic<float>*on, *mix, *size, *decay, *damping, *predelay, *width;
    } rvp_ {};

    // loader <-> audio thread
    std::atomic<tw::InstrumentVoice*> pending_ { nullptr };
    std::atomic<tw::InstrumentVoice*> retired_ { nullptr };
    std::atomic<double> hostRate_ { 48000.0 };
    std::atomic<bool> rebuild_ { false };
    std::atomic<int64_t> readyGeneration_ { -1 };

    // loader thread only (read under the lock or atomically)
    mutable juce::CriticalSection statusLock_;
    juce::String status_;
    std::atomic<int> keyLo_ { 21 }, keyHi_ { 108 };

    // audio thread only
    std::unique_ptr<tw::InstrumentVoice> active_, fading_;
    float fadeGain_ = 0.0f, fadeStep_ = 0.0f;
    int sustain_ = 0;
    pmp::Mk8Effects fx_;
    tw::Reverb reverb_;
    tw::Limiter limiter_;
    tw::Humanize humanize_ { uint64_t (juce::Time::getHighResolutionTicks()) };
    float fxGain_ = 0.0f, reverbGain_ = 0.0f, switchStep_ = 0.001f; // cross-fades of the stage switches
    juce::SmoothedValue<float> gain_ { 1.0f };
    juce::AudioBuffer<float> scratch_; // fading voice (0, 1), right channel of a mono output (2), a stage fading in or out (3, 4)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
