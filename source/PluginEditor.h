// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// PluginEditor.h - preset bar, instrument panel (choice, level, voicing), the effect sections, the keyboard E1-E7
#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace tw
{
// a rotary control with its name above and its value below, attached to a parameter
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name);
    void resized() override;

private:
    juce::Slider slider_;
    juce::Label label_;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment_;
};

// a framed panel with a title and an optional switch in the title bar; the controls are laid out in one row
class Section : public juce::Component
{
public:
    Section (const juce::String& title, juce::AudioProcessorValueTreeState& state, const char* switchId = nullptr);
    void add (std::unique_ptr<juce::Component> c, float weight = 1.0f);
    float weight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title_;
    std::unique_ptr<juce::ToggleButton> switch_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAttachment_;
    std::vector<std::pair<std::unique_ptr<juce::Component>, float>> items_;
};
} // namespace tw

class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetList();
    void confirm (const juce::String& title, const juce::String& message, const juce::String& yes, std::function<void()> then);
    void savePresetDialog();
    void deletePreset();
    void showBankMenu();
    void resetVoicing();
    bool voicingIsNeutral() const;

    PluginProcessor& processor_;
    tw::LookAndFeel lnf_;
    juce::TooltipWindow tooltips_ { this, 600 };

    // header: preset bar and bank menu
    juce::TextButton prevPreset_ { "<" }, nextPreset_ { ">" }, savePreset_, deletePreset_, banks_;
    juce::ComboBox presetBox_;
    std::vector<tw::PresetManager::Entry> presetEntries_;
    juce::String shownPreset_;
    bool shownFactory_ = false;

    // instrument panel
    juce::ComboBox instrumentBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> instrumentAttachment_;
    juce::Label status_;
    std::unique_ptr<tw::Knob> volume_, humanize_, hardness_, decay_, unison_, stretch_;
    juce::Rectangle<int> instrumentPanel_, voicingTitle_;
    juce::TextButton resetVoicing_ { "Reset" };

    // effects
    juce::Rectangle<int> fxPanel_;
    juce::ToggleButton fxOn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fxOnAttachment_;
    std::vector<std::unique_ptr<tw::Section>> fxSections_;
    static constexpr size_t kReverbSection = 7; // the last section: not dimmed by the FX switch

    juce::MidiKeyboardComponent keyboard_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
