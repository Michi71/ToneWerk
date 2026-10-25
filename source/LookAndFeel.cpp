// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "LookAndFeel.h"

namespace tw
{
LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    setColour (juce::ComboBox::backgroundColourId, knob);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, accent);
    setColour (juce::TextButton::buttonColourId, knob);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, panelEdge);
    setColour (juce::TextEditor::backgroundColourId, knob);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, panelEdge);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float start,
    float end, juce::Slider& slider)
{
    using namespace colours;
    const auto bounds = juce::Rectangle<float> (float (x), float (y), float (width), float (height)).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float track = juce::jmax (2.5f, radius * 0.12f);
    const float arcRadius = radius - track * 0.5f;
    const float angle = start + pos * (end - start);
    const bool enabled = slider.isEnabled();

    juce::Path back;
    back.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (panelEdge);
    g.strokePath (back, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar ranges (EQ, voicing) start their arc at zero
    const auto& range = slider.getNormalisableRange();
    const bool bipolar = range.start < 0.0 && range.end > 0.0;
    const float from = bipolar ? start + float (slider.valueToProportionOfLength (0.0)) * (end - start) : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? accent : dim);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float body = radius - track * 2.2f;
    g.setGradientFill (juce::ColourGradient (knob.brighter (0.25f), centre.x, centre.y - body, knob.darker (0.4f), centre.x,
        centre.y + body, false));
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre));
    g.setColour (panelEdge.brighter (0.2f));
    g.drawEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre), 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -body * 0.92f, 3.0f, body * 0.45f, 1.5f);
    g.setColour (text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace colours;
    auto bounds = b.getLocalBounds().toFloat();
    const float h = juce::jmin (16.0f, bounds.getHeight());
    const auto led = juce::Rectangle<float> (h * 1.8f, h).withY (bounds.getCentreY() - h * 0.5f).withX (bounds.getX() + 2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : knob);
    g.fillRoundedRectangle (led, h * 0.5f);
    g.setColour (highlighted ? text.withAlpha (0.6f) : panelEdge);
    g.drawRoundedRectangle (led, h * 0.5f, 1.0f);
    const float d = h - 4.0f;
    g.setColour (on ? text : dim);
    g.fillEllipse (on ? led.getRight() - d - 2.0f : led.getX() + 2.0f, led.getY() + 2.0f, d, d);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? text : dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (b.getButtonText(), bounds.withLeft (led.getRight() + 6.0f), juce::Justification::centredLeft);
    }
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted,
    bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = background;
    if (down)
        c = colours::accentDim;
    else if (highlighted)
        c = c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, float (width), float (height)).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    const float s = juce::jmin (8.0f, float (height) * 0.3f);
    const float ax = float (width) - s - 10.0f, ay = float (height) * 0.5f - s * 0.25f;
    juce::Path arrow;
    arrow.addTriangle (ax, ay, ax + s, ay, ax + s * 0.5f, ay + s * 0.55f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::FontOptions (juce::jmin (16.0f, float (box.getHeight()) * 0.5f));
}

juce::Font LookAndFeel::getPopupMenuFont() { return juce::FontOptions (15.0f); }

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (14.0f, float (buttonHeight) * 0.55f));
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (juce::FontOptions (11.5f));
    l->setColour (juce::Label::textColourId, colours::dim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}
} // namespace tw
