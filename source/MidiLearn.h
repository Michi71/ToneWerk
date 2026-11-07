// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// MidiLearn.h - MIDI controllers assigned to plugin parameters, learned from the next incoming CC
//
// The assignment lives in the plugin state as a child of the parameter tree (<MIDI_LEARN><MAP cc="21" param="volume"/>), so
// projects and user presets carry it; a listener on that tree keeps the lookup tables below in step with it, whichever way
// the tree changes (setStateInformation, a user preset, the editor's menu).
//
// Threads: the audio thread only reads and writes atomics (handleController). A CC value or a learned CC waits in an atomic
// slot until the message thread picks it up (dispatch, from the processor's timer) and sets the parameter, notifying the host.
// Values of one CC arriving between two dispatches collapse into the last one.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace tw
{
class MidiLearn : public juce::ChangeBroadcaster, private juce::ValueTree::Listener
{
public:
    static constexpr const char* kTreeType = "MIDI_LEARN";
    static constexpr const char* kMapType = "MAP";
    static constexpr int kControllers = 128;

    explicit MidiLearn (juce::AudioProcessorValueTreeState& state);
    ~MidiLearn() override;

    // CC64 (sustain) and the channel mode messages 120-127 (all sound off, reset, all notes off, ...) keep their meaning
    static bool learnable (int cc) noexcept { return cc >= 0 && cc < kControllers && cc != 64 && cc < 120; }

    // --- message thread
    // the next learnable CC that arrives is assigned to the parameter (replacing what either had before)
    void beginLearn (const juce::String& parameterId);
    void cancelLearn();
    juce::String learning() const { return learnId_; }

    void assign (int cc, const juce::String& parameterId);
    void forget (const juce::String& parameterId);
    void clear();
    int ccFor (const juce::String& parameterId) const; // -1: none
    juce::String parameterFor (int cc) const;          // empty: none
    int size() const;

    // picks up what the audio thread left: completes a learn, sets the parameters of the CCs that moved
    void dispatch();

    // --- audio thread
    // true when the CC is taken here (learning, or assigned): it is not passed on to the instrument
    bool handleController (int cc, int value) noexcept;

private:
    juce::ValueTree tree (bool create);
    void rebuild();
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { rebuild(); }

    juce::AudioProcessorValueTreeState& state_; // listened to: replaceState redirects state_.state

    std::array<std::atomic<juce::RangedAudioParameter*>, kControllers> parameters_ {};
    std::array<std::atomic<int>, kControllers> values_ {}; // pending value 0..127, -1: none
    std::atomic<bool> anyValue_ { false };
    std::atomic<bool> learningActive_ { false };
    std::atomic<int> learnedCc_ { -1 };
    juce::String learnId_; // message thread
};
} // namespace tw
