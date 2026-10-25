// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// LookAndFeel.h - the ToneWerk look: dark panels, warm accent, rotary knobs with a value arc, LED switches
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tw
{
namespace colours
{
    inline const juce::Colour background { 0xff17181c };
    inline const juce::Colour panel { 0xff22242a };
    inline const juce::Colour panelEdge { 0xff31343c };
    inline const juce::Colour text { 0xffe9e6df };
    inline const juce::Colour dim { 0xff8c8a85 };
    inline const juce::Colour accent { 0xffe8913a };
    inline const juce::Colour accentDim { 0xff6b4a2a };
    inline const juce::Colour knob { 0xff2e3138 };
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
        float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted,
        bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh,
        juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};
} // namespace tw
