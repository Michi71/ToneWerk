// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// PluginEditor.h - preset bar, instrument panel (choice, level, voicing), the effect sections, the keyboard of the instrument
#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <type_traits>

namespace tw
{
// MIDI learn on a control: the right-click menu and the hint of the assigned CC (PluginEditor.cpp)
namespace learnui
{
    // "MIDI Learn" (or "Cancel MIDI Learn"), "Forget CC n", "Clear all"
    juce::PopupMenu menu (MidiLearn& learn, const juce::String& parameterId);
    void showMenu (MidiLearn& learn, const juce::String& parameterId, juce::Component& target);
    juce::String tooltip (const juce::String& base, int cc, bool learning);
    // compact: a dot instead of the tag (the switches)
    void paintHint (juce::Graphics& g, juce::Rectangle<int> bounds, int cc, bool learning, bool compact);
    MidiLearn* of (juce::AudioProcessorValueTreeState& state); // the processor's
} // namespace learnui

// a control (Slider, ToggleButton, ComboBox) whose parameter can be learned: a right click opens the MIDI learn menu instead
// of reaching the control, the tooltip and a small hint show the assigned CC
template <class Base>
class Learnable : public Base, private juce::ChangeListener
{
public:
    using Base::Base;
    ~Learnable() override
    {
        if (learn_ != nullptr)
            learn_->removeChangeListener (this);
    }

    void attachMidiLearn (MidiLearn* learn, const juce::String& parameterId, const juce::String& tooltip)
    {
        if (learn_ != nullptr)
            learn_->removeChangeListener (this);
        learn_ = learn;
        id_ = parameterId;
        tooltip_ = tooltip;
        if (learn_ != nullptr)
            learn_->addChangeListener (this);
        refresh();
    }
    const juce::String& parameterId() const { return id_; }
    void showMidiLearnMenu()
    {
        if (learn_ != nullptr)
            learnui::showMenu (*learn_, id_, *this);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (learn_ != nullptr && e.mods.isPopupMenu())
            showMidiLearnMenu();
        else
            Base::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (learn_ == nullptr || !e.mods.isPopupMenu())
            Base::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (learn_ == nullptr || !e.mods.isPopupMenu())
            Base::mouseUp (e);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        Base::paintOverChildren (g);
        if (cc_ >= 0 || learning_)
            learnui::paintHint (g, this->getLocalBounds(), cc_, learning_, std::is_base_of_v<juce::Button, Base>);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh()
    {
        cc_ = learn_ != nullptr ? learn_->ccFor (id_) : -1;
        learning_ = learn_ != nullptr && learn_->learning() == id_;
        this->setTooltip (learnui::tooltip (tooltip_, cc_, learning_));
        this->repaint();
    }

    MidiLearn* learn_ = nullptr;
    juce::String id_, tooltip_;
    int cc_ = -1;
    bool learning_ = false;
};

// a rotary control with its name above and its value below, attached to a parameter
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name);
    void setTitle (const juce::String& name) { label_.setText (name, juce::dontSendNotification); }
    void resized() override;
    Learnable<juce::Slider>& slider() { return slider_; }

private:
    Learnable<juce::Slider> slider_;
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
    std::unique_ptr<Learnable<juce::ToggleButton>> switch_;
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
    void resetVoicing();
    bool voicingIsNeutral() const;
    void showInstrument(); // the voicing controls of the instrument's family and its keys

    PluginProcessor& processor_;
    tw::LookAndFeel lnf_;
    juce::TooltipWindow tooltips_ { this, 600 };

    // header: preset bar
    juce::TextButton prevPreset_ { "<" }, nextPreset_ { ">" }, savePreset_, deletePreset_;
    juce::ComboBox presetBox_;
    std::vector<tw::PresetManager::Entry> presetEntries_;
    juce::String shownPreset_;
    bool shownFactory_ = false;

    // instrument panel
    tw::Learnable<juce::ComboBox> instrumentBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> instrumentAttachment_;
    juce::Label status_;
    std::unique_ptr<tw::Knob> volume_, humanize_;
    std::map<juce::String, std::unique_ptr<tw::Knob>> voicing_; // every voicing control; the family's are shown
    juce::StringArray shownVoicing_;
    int shownFamily_ = -1;
    std::pair<int, int> shownKeys_ { -1, -1 };
    juce::Rectangle<int> instrumentPanel_, voicingTitle_;
    juce::TextButton resetVoicing_ { "Reset" };

    // effects
    juce::Rectangle<int> fxPanel_;
    tw::Learnable<juce::ToggleButton> fxOn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fxOnAttachment_;
    std::vector<std::unique_ptr<tw::Section>> fxSections_;
    static constexpr size_t kReverbSection = 7; // the last section: not dimmed by the FX switch

    juce::MidiKeyboardComponent keyboard_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
