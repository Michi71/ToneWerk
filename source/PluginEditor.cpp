// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "PluginEditor.h"

#include "Parameters.h"

namespace ids = tw::ids;
using juce::String;

namespace
{
String utf8 (const char* s) { return String::fromUTF8 (s); }

// a choice parameter in a ComboBox, attached
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState& state, const char* id, const String& name)
    {
        label_.setText (name, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (label_);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            box_.addItemList (p->choices, 1);
        box_.attachMidiLearn (tw::learnui::of (state), id, name);
        addAndMakeVisible (box_);
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box_);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (18));
        box_.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, juce::jmin (26, r.getHeight())));
    }

private:
    juce::Label label_;
    tw::Learnable<juce::ComboBox> box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;
};

// a bool parameter as a labelled switch, centred in its cell
class Switch : public juce::Component
{
public:
    Switch (juce::AudioProcessorValueTreeState& state, const char* id, const String& name)
    {
        label_.setText (name, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (label_);
        button_.attachMidiLearn (tw::learnui::of (state), id, name);
        addAndMakeVisible (button_);
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, button_);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (18));
        button_.setBounds (r.withSizeKeepingCentre (36, 22));
    }

private:
    juce::Label label_;
    tw::Learnable<juce::ToggleButton> button_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment_;
};
} // namespace

namespace tw
{
namespace learnui
{
    MidiLearn* of (juce::AudioProcessorValueTreeState& state)
    {
        auto* processor = dynamic_cast<PluginProcessor*> (&state.processor);
        return processor != nullptr ? &processor->midiLearn() : nullptr;
    }

    juce::PopupMenu menu (MidiLearn& learn, const String& parameterId)
    {
        juce::PopupMenu m;
        const int cc = learn.ccFor (parameterId);
        if (learn.learning() == parameterId)
            m.addItem ("Cancel MIDI Learn", [&learn] { learn.cancelLearn(); });
        else
            m.addItem ("MIDI Learn", [&learn, parameterId] { learn.beginLearn (parameterId); });
        if (cc >= 0)
            m.addItem ("Forget CC " + String (cc), [&learn, parameterId] { learn.forget (parameterId); });
        else
            m.addItem ("Forget CC", false, false, nullptr);
        m.addSeparator();
        m.addItem ("Clear all", learn.size() > 0, false, [&learn] {
            learn.cancelLearn();
            learn.clear();
        });
        return m;
    }

    void showMenu (MidiLearn& learn, const String& parameterId, juce::Component& target)
    {
        auto m = menu (learn, parameterId);
        m.setLookAndFeel (&target.getLookAndFeel());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition());
    }

    String tooltip (const String& base, int cc, bool learning)
    {
        if (learning)
            return base + " (MIDI Learn: move a controller)";
        return cc >= 0 ? base + " (CC " + String (cc) + ")" : base;
    }

    // the CC in a small tag in the top right corner (a dot on a switch, the tag would cover it); learning: an outline
    void paintHint (juce::Graphics& g, juce::Rectangle<int> bounds, int cc, bool learning, bool compact)
    {
        const auto r = bounds.toFloat();
        if (learning)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, 1.5f);
        }
        if (compact)
        {
            g.setColour (colours::accent);
            g.fillEllipse (r.getRight() - 6.0f, r.getY() + 1.0f, 5.0f, 5.0f);
            return;
        }
        const String text = learning ? String ("LEARN") : "CC " + String (cc);
        const juce::Font font (juce::FontOptions (9.5f, juce::Font::bold));
        const float w = juce::GlyphArrangement::getStringWidth (font, text) + 8.0f;
        const auto tag = juce::Rectangle<float> (r.getRight() - w - 1.0f, r.getY() + 1.0f, w, 12.0f);
        g.setColour (learning ? colours::accent : colours::accentDim);
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (learning ? colours::background : colours::text);
        g.setFont (font);
        g.drawText (text, tag, juce::Justification::centred);
    }
} // namespace learnui

Knob::Knob (juce::AudioProcessorValueTreeState& state, const String& id, const String& name)
    : attachment_ (state, id, slider_)
{
    slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // set here: the text box is created before the knob has a parent with the plugin's look
    slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider_.setColour (juce::Slider::textBoxTextColourId, colours::dim);
    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    slider_.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    if (auto* p = state.getParameter (id))
    {
        slider_.setDoubleClickReturnValue (true, double (p->convertFrom0to1 (p->getDefaultValue())));
        slider_.textFromValueFunction = [p] (double v) { return p->getText (p->convertTo0to1 (float (v)), 16); };
        slider_.updateText();
        slider_.attachMidiLearn (learnui::of (state), id, p->getName (64));
    }
    label_.setText (name, juce::dontSendNotification);
    label_.setJustificationType (juce::Justification::centred);
    label_.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (slider_);
    addAndMakeVisible (label_);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label_.setBounds (r.removeFromTop (18));
    slider_.setBounds (r);
}

Section::Section (const String& title, juce::AudioProcessorValueTreeState& state, const char* switchId) : title_ (title)
{
    if (switchId != nullptr)
    {
        switch_ = std::make_unique<Learnable<juce::ToggleButton>>();
        switch_->attachMidiLearn (learnui::of (state), switchId, title + " on/off");
        addAndMakeVisible (*switch_);
        switchAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, switchId, *switch_);
    }
}

void Section::add (std::unique_ptr<juce::Component> c, float weight)
{
    addAndMakeVisible (*c);
    items_.emplace_back (std::move (c), weight);
}

float Section::weight() const
{
    float w = 0.0f;
    for (const auto& i : items_)
        w += i.second;
    return w;
}

void Section::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel.brighter (0.03f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (title_, getLocalBounds().reduced (10, 0).removeFromTop (28), juce::Justification::centredLeft);
}

void Section::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto head = r.removeFromTop (24);
    if (switch_ != nullptr)
        switch_->setBounds (head.removeFromRight (40));
    const float total = juce::jmax (1.0f, weight());
    const float unit = float (r.getWidth()) / total;
    float x = float (r.getX());
    for (auto& [c, w] : items_)
    {
        c->setBounds (juce::Rectangle<int> (int (x), r.getY(), int (unit * w), r.getHeight()).reduced (2, 0));
        x += unit * w;
    }
}
} // namespace tw

// --- the editor ----------------------------------------------------------------------------------------------------------

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p),
      keyboard_ (p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf_);
    auto& state = p.state();

    // header
    savePreset_.setButtonText ("Save");
    deletePreset_.setButtonText ("Delete");
    prevPreset_.setTooltip ("Previous preset");
    nextPreset_.setTooltip ("Next preset");
    savePreset_.setTooltip ("Save the current settings as a user preset");
    deletePreset_.setTooltip ("Delete the user preset (moves it to the trash)");
    for (auto* b : { &prevPreset_, &nextPreset_, &savePreset_, &deletePreset_ })
        addAndMakeVisible (*b);
    prevPreset_.onClick = [this] { processor_.presets().step (-1); };
    nextPreset_.onClick = [this] { processor_.presets().step (1); };
    savePreset_.onClick = [this] { savePresetDialog(); };
    deletePreset_.onClick = [this] { deletePreset(); };
    presetBox_.setTextWhenNothingSelected ("(no preset)");
    presetBox_.onChange = [this] {
        const int index = presetBox_.getSelectedId() - 1;
        if (index >= 0 && index < int (presetEntries_.size()))
            processor_.presets().load (presetEntries_[size_t (index)]);
    };
    addAndMakeVisible (presetBox_);

    // instrument
    instrumentBox_.addItemList (tw::instrumentNames(), 1);
    instrumentBox_.attachMidiLearn (&p.midiLearn(), ids::instrument, "Instrument");
    addAndMakeVisible (instrumentBox_);
    instrumentAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::instrument,
        instrumentBox_);
    status_.setFont (juce::FontOptions (12.5f));
    status_.setColour (juce::Label::textColourId, tw::colours::dim);
    status_.setJustificationType (juce::Justification::topLeft);
    status_.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (status_);
    volume_ = std::make_unique<tw::Knob> (state, ids::volume, "Volume");
    humanize_ = std::make_unique<tw::Knob> (state, ids::humanize, "Humanize");
    for (auto* k : { volume_.get(), humanize_.get() })
        addAndMakeVisible (*k);
    for (const auto& id : tw::allVoicingIds())
    {
        auto knob = std::make_unique<tw::Knob> (state, id, tw::voicingLabel (id, tw::engines::Family::Tines));
        addChildComponent (*knob);
        voicing_[id] = std::move (knob);
    }
    resetVoicing_.setTooltip ("Reset the voicing to the instrument as calibrated (all controls 0)");
    resetVoicing_.onClick = [this] { resetVoicing(); };
    addAndMakeVisible (resetVoicing_);

    // effects
    fxOn_.setButtonText ("FX");
    fxOn_.attachMidiLearn (&p.midiLearn(), ids::fxOn, "Effect chain on/off");
    addAndMakeVisible (fxOn_);
    fxOnAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::fxOn, fxOn_);
    auto knob = [&state] (const char* id, const char* name) { return std::make_unique<tw::Knob> (state, id, utf8 (name)); };
    auto section = [&] (const char* title, const char* switchId) {
        fxSections_.push_back (std::make_unique<tw::Section> (utf8 (title), state, switchId));
        addAndMakeVisible (*fxSections_.back());
        return fxSections_.back().get();
    };
    auto* pre = section ("Preamp", nullptr);
    pre->add (knob (ids::fxDrive, "Drive"));
    pre->add (knob (ids::fxEnvelope, "Envelope"));
    pre->add (knob (ids::fxVolume, "Level"));
    auto* eq = section ("Equalizer", nullptr);
    eq->add (knob (ids::fxBass, "Bass"));
    eq->add (knob (ids::fxMid, "Mid"));
    eq->add (knob (ids::fxMidFreq, "Mid Freq"));
    eq->add (knob (ids::fxTreble, "Treble"));
    auto* pan = section ("Vari-Pan", ids::panOn);
    pan->add (std::make_unique<ChoiceBox> (state, ids::panWave, "Wave"), 1.3f);
    pan->add (knob (ids::panRate, "Rate"));
    pan->add (knob (ids::panDepth, "Depth"));
    auto* comp = section ("Compressor", ids::compOn);
    comp->add (knob (ids::compAmount, "Amount"));
    comp->add (knob (ids::compMakeup, "Make-up"));
    auto* chorus = section ("Chorus", ids::chorusOn);
    chorus->add (knob (ids::chorusRate, "Rate"));
    chorus->add (knob (ids::chorusDepth, "Depth"));
    auto* phaser = section ("Phaser", ids::phaserOn);
    phaser->add (knob (ids::phaserRate, "Rate"));
    phaser->add (knob (ids::phaserDepth, "Depth"));
    auto* delay = section ("Delay", ids::delayOn);
    delay->add (knob (ids::delayTime, "Time"));
    delay->add (knob (ids::delayFeedback, "Feedback"));
    delay->add (knob (ids::delayMix, "Mix"));
    delay->add (std::make_unique<Switch> (state, ids::delayPingPong, "Ping-Pong"));
    auto* reverb = section ("Reverb", ids::reverbOn); // after the chain, not under the FX switch
    reverb->add (knob (ids::reverbMix, "Mix"));
    reverb->add (knob (ids::reverbSize, "Size"));
    reverb->add (knob (ids::reverbDecay, "Decay"));
    reverb->add (knob (ids::reverbDamping, "Damping"));
    reverb->add (knob (ids::reverbPredelay, "Pre-Delay"));
    reverb->add (knob (ids::reverbWidth, "Width"));

    // keyboard: the keys of the instrument (showInstrument)
    keyboard_.setAvailableRange (21, 108);
    keyboard_.setOctaveForMiddleC (4);
    keyboard_.setScrollButtonsVisible (false);
    keyboard_.setVelocity (0.8f, true);
    keyboard_.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, tw::colours::accent.withAlpha (0.85f));
    keyboard_.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, tw::colours::accent.withAlpha (0.3f));
    keyboard_.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3a3a));
    keyboard_.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.35f));
    keyboard_.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, tw::colours::panel);
    addAndMakeVisible (keyboard_);

    setResizable (true, true);
    setResizeLimits (920, 600, 1800, 1100);
    getConstrainer()->setFixedAspectRatio (0.0);
    setSize (1100, 680);
    refreshPresetList();
    timerCallback();
    startTimerHz (8);
}

PluginEditor::~PluginEditor() { setLookAndFeel (nullptr); }

void PluginEditor::paint (juce::Graphics& g)
{
    using namespace tw::colours;
    g.fillAll (background);

    // header
    auto header = getLocalBounds().removeFromTop (56);
    g.setColour (panel.darker (0.25f));
    g.fillRect (header);
    g.setColour (panelEdge);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, float (getWidth()));
    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("Tone", header.withX (18).withWidth (70), juce::Justification::centredLeft);
    g.setColour (text);
    g.drawText ("Werk", header.withX (78).withWidth (80), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (String ("v") + VERSION, header.withX (146).withWidth (60), juce::Justification::centredLeft);

    // panels
    for (const auto& r : { instrumentPanel_, fxPanel_ })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 9.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);
    }
    g.setColour (text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("Instrument", instrumentPanel_.reduced (14, 0).removeFromTop (34), juce::Justification::centredLeft);
    g.drawText ("Effects", fxPanel_.reduced (14, 0).removeFromTop (34), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (dim);
    g.drawText ("Voicing", voicingTitle_, juce::Justification::centredLeft);
    g.setColour (panelEdge);
    g.drawHorizontalLine (voicingTitle_.getY(), float (voicingTitle_.getX()), float (voicingTitle_.getRight()));
}

void PluginEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (56).reduced (12, 12);
    header.removeFromLeft (200);
    deletePreset_.setBounds (header.removeFromRight (82));
    header.removeFromRight (6);
    savePreset_.setBounds (header.removeFromRight (92));
    header.removeFromRight (10);
    nextPreset_.setBounds (header.removeFromRight (32));
    header.removeFromRight (4);
    auto presetArea = header.removeFromRight (juce::jmin (340, header.getWidth() - 40));
    prevPreset_.setBounds (presetArea.removeFromLeft (32));
    presetArea.removeFromLeft (4);
    presetBox_.setBounds (presetArea);

    const int keyboardHeight = juce::jlimit (110, 180, getHeight() / 5);
    auto keys = r.removeFromBottom (keyboardHeight).reduced (12, 0).withTrimmedBottom (10);
    keyboard_.setBounds (keys);
    int whiteKeys = 0;
    for (int note = keyboard_.getRangeStart(); note <= keyboard_.getRangeEnd(); ++note)
        whiteKeys += juce::MidiMessage::isMidiNoteBlack (note) ? 0 : 1;
    keyboard_.setKeyWidth (float (keys.getWidth()) / float (juce::jmax (1, whiteKeys)));

    r = r.reduced (12, 12);
    instrumentPanel_ = r.removeFromLeft (juce::jlimit (340, 460, getWidth() * 8 / 25));
    r.removeFromLeft (12);
    fxPanel_ = r;

    // instrument panel: choice and status, the level, the voicing controls of the family four in a row
    auto ip = instrumentPanel_.reduced (14, 0);
    ip.removeFromTop (36);
    instrumentBox_.setBounds (ip.removeFromTop (34));
    ip.removeFromTop (6);
    status_.setBounds (ip.removeFromTop (36));
    ip.removeFromBottom (10);
    auto vol = ip.removeFromTop (juce::jmin (130, ip.getHeight() / 3));
    volume_->setBounds (vol.removeFromLeft (vol.getWidth() / 2));
    humanize_->setBounds (vol);
    ip.removeFromTop (8);
    voicingTitle_ = ip.removeFromTop (26);
    resetVoicing_.setBounds (voicingTitle_.withLeft (voicingTitle_.getRight() - 58).reduced (0, 3));
    const int columns = 4;
    const int voicingRows = juce::jmax (2, (shownVoicing_.size() + columns - 1) / columns);
    const int rowH = ip.getHeight() / voicingRows;
    for (int i = 0; i < shownVoicing_.size(); ++i)
    {
        const int row = i / columns, column = i % columns;
        const int x0 = ip.getX() + ip.getWidth() * column / columns, x1 = ip.getX() + ip.getWidth() * (column + 1) / columns;
        voicing_[shownVoicing_[i]]->setBounds (x0, ip.getY() + row * rowH, x1 - x0, rowH);
    }

    // effects: three rows of sections, widths after their number of controls
    auto fp = fxPanel_.reduced (12, 0);
    auto fxHead = fp.removeFromTop (36);
    fxOn_.setBounds (fxHead.removeFromRight (70).withSizeKeepingCentre (70, 24));
    fp.removeFromBottom (12);
    const std::vector<std::vector<int>> rows { { 0, 1 }, { 2, 3, 4, 5 }, { 6, 7 } };
    const int gap = 8;
    const int h = (fp.getHeight() - gap * int (rows.size() - 1)) / int (rows.size());
    for (const auto& row : rows)
    {
        auto line = fp.removeFromTop (h);
        fp.removeFromTop (gap);
        float total = 0.0f;
        for (int i : row)
            total += fxSections_[size_t (i)]->weight();
        const float unit = float (line.getWidth() - gap * int (row.size() - 1)) / total;
        for (size_t k = 0; k < row.size(); ++k)
        {
            auto& s = *fxSections_[size_t (row[k])];
            const int w = k + 1 == row.size() ? line.getWidth() : int (unit * s.weight());
            s.setBounds (line.removeFromLeft (w));
            line.removeFromLeft (gap);
        }
    }
}

void PluginEditor::showInstrument()
{
    const int index = int (std::lround (processor_.state().getRawParameterValue (ids::instrument)->load()));
    const auto family = tw::instrument (index).family;
    const auto keys = processor_.instrumentKeys();
    if (int (family) == shownFamily_ && keys == shownKeys_)
        return;
    shownFamily_ = int (family);
    shownKeys_ = keys;
    shownVoicing_ = tw::voicingIds (family);
    for (auto& [id, knob] : voicing_)
    {
        knob->setVisible (shownVoicing_.contains (id));
        knob->setTitle (tw::voicingLabel (id, family));
    }
    if (keys.first <= keys.second)
        keyboard_.setAvailableRange (keys.first, keys.second);
    resized();
}

void PluginEditor::timerCallback()
{
    showInstrument();
    status_.setText (processor_.instrumentStatus(), juce::dontSendNotification);
    resetVoicing_.setEnabled (!voicingIsNeutral());
    const bool fx = fxOn_.getToggleState();
    for (size_t i = 0; i < fxSections_.size(); ++i)
        fxSections_[i]->setAlpha (fx || i == kReverbSection ? 1.0f : 0.45f);

    auto& presets = processor_.presets();
    if (presets.currentName() != shownPreset_ || presets.currentIsFactory() != shownFactory_)
        refreshPresetList();
}

void PluginEditor::refreshPresetList()
{
    auto& presets = processor_.presets();
    presetEntries_ = presets.entries();
    shownPreset_ = presets.currentName();
    shownFactory_ = presets.currentIsFactory();
    presetBox_.clear (juce::dontSendNotification);
    int selected = 0;
    bool userHeading = false;
    presetBox_.addSectionHeading ("Factory presets");
    for (size_t i = 0; i < presetEntries_.size(); ++i)
    {
        const auto& e = presetEntries_[i];
        if (!e.factory && !userHeading)
        {
            presetBox_.addSectionHeading ("User presets");
            userHeading = true;
        }
        presetBox_.addItem (e.name, int (i) + 1);
        if (e.name == shownPreset_ && e.factory == shownFactory_)
            selected = int (i) + 1;
    }
    presetBox_.setSelectedId (selected, juce::dontSendNotification);
    deletePreset_.setEnabled (!shownFactory_ && shownPreset_.isNotEmpty());
}

void PluginEditor::confirm (const String& title, const String& message, const String& yes, std::function<void()> then)
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle (title)
                             .withMessage (message)
                             .withButton (yes, 1)
                             .withButton ("Cancel", 0)
                             .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [action = std::move (then)] (int result) {
        if (result == 1)
            action();
    });
}

void PluginEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    const auto& presets = processor_.presets();
    w->addTextEditor ("name", presets.currentIsFactory() ? String() : presets.currentName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->setLookAndFeel (&lnf_);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result) {
        const auto name = w->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;
        auto& pm = processor_.presets();
        auto write = [this, name] {
            if (!processor_.presets().save (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                    "The preset could not be saved:\n" + processor_.presets().folder().getFullPathName(), "OK", this);
            refreshPresetList();
        };
        if (!pm.exists (name))
        {
            write();
            return;
        }
        confirm ("Save preset", utf8 ("\xe2\x80\x9c") + name + utf8 ("\xe2\x80\x9d already exists. Overwrite it?"), "Overwrite", write);
    }),
        true);
}

void PluginEditor::deletePreset()
{
    auto& presets = processor_.presets();
    if (presets.currentIsFactory() || presets.currentName().isEmpty())
        return;
    const auto name = presets.currentName();
    confirm ("Delete preset", "Move " + utf8 ("\xe2\x80\x9c") + name + utf8 ("\xe2\x80\x9d to the trash?"), "Delete", [this, name] {
            processor_.presets().remove (name);
            refreshPresetList();
        });
}

bool PluginEditor::voicingIsNeutral() const
{
    for (const auto& id : tw::allVoicingIds())
        if (const auto* p = processor_.state().getParameter (id); p != nullptr && std::abs (p->getValue() - p->getDefaultValue()) > 1e-6f)
            return false;
    return true;
}

// every voicing control back to its default in one go: one rebuild of the instrument (the loader waits for the controls to rest)
void PluginEditor::resetVoicing()
{
    for (const auto& id : tw::allVoicingIds())
        if (auto* p = processor_.state().getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    resetVoicing_.setEnabled (false);
}
