// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// PluginProcessor.h - ToneWerk: the pmpiano hybrid instruments as a plugin
//
// Threads: the audio thread plays the active InstrumentVoice and the effect chain. A loader thread builds a new voice whenever
// the instrument, the voicing or the bank folder changes (bank loading and engine preparation are not real-time safe) and hands
// it over through an atomic pointer; the audio thread adopts it at the next block, fades the old one out and hands that back
// for deletion. Nothing is allocated or freed on the audio thread.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <core/mk8_fx.h>

#include "InstrumentVoice.h"
#include "PresetManager.h"
#include "Reverb.h"

#include <atomic>
#include <deque>

class PluginProcessor : public juce::AudioProcessor, private juce::Thread
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "ToneWerk"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts_; }
    tw::PresetManager& presets() { return presets_; }
    juce::MidiKeyboardState& keyboardState() { return keyboardState_; }

    // what the loaded instrument says about itself (bank, rate, fallback); message thread
    juce::String instrumentStatus() const;
    // the bank folder changed: reload the instrument from it
    void reloadBanks() { bankGeneration_.fetch_add (1); }
    // true when the voice built for the current parameters plays (tests; the audio thread adopts it in processBlock)
    bool instrumentReady() const { return readyGeneration_.load() == wantedSignature(); }

private:
    static constexpr int kChunk = 512;      // largest render call into a voice [host frames]
    static constexpr double kFadeMs = 30.0;   // fade-out of the old voice after a switch
    static constexpr double kSwitchMs = 20.0; // cross-fade of the FX and reverb switches

    void run() override; // the loader thread
    tw::VoiceConfig wantedConfig() const;
    int64_t wantedSignature() const;
    std::shared_ptr<tw::BankImage> bankFor (int slot, int generation);
    void build (const tw::VoiceConfig& config);

    void adoptPending();
    void retire (std::unique_ptr<tw::InstrumentVoice>& voice);
    void handleMidi (const juce::MidiMessage& m);
    void renderSegment (float* left, float* right, int n);
    void updateEffects();

    juce::AudioProcessorValueTreeState apvts_;
    tw::PresetManager presets_;
    juce::MidiKeyboardState keyboardState_;

    // parameters read on the audio and loader threads
    std::atomic<float>* instrument_ = nullptr;
    std::atomic<float>* volume_ = nullptr;
    std::atomic<float>* hardness_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* unison_ = nullptr;
    std::atomic<float>* stretch_ = nullptr;
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
    std::atomic<int> bankGeneration_ { 0 };
    std::atomic<bool> rebuild_ { false };
    std::atomic<int64_t> readyGeneration_ { -1 };

    // loader thread only
    std::deque<std::shared_ptr<tw::BankImage>> bankCache_;
    int bankCacheGeneration_ = -1;
    mutable juce::CriticalSection statusLock_;
    juce::String status_;

    // audio thread only
    std::unique_ptr<tw::InstrumentVoice> active_, fading_;
    float fadeGain_ = 0.0f, fadeStep_ = 0.0f;
    int sustain_ = 0;
    pmp::Mk8Effects fx_;
    tw::Reverb reverb_;
    float fxGain_ = 0.0f, reverbGain_ = 0.0f, switchStep_ = 0.001f; // cross-fades of the stage switches
    juce::SmoothedValue<float> gain_ { 1.0f };
    juce::AudioBuffer<float> scratch_; // fading voice (0, 1), right channel of a mono output (2), a stage fading in or out (3, 4)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
