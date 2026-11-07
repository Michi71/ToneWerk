// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include <MidiLearn.h>
#include <Parameters.h>
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <PresetManager.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
// one block with the given MIDI, then what the processor's timer does on the message thread
void send (PluginProcessor& p, std::initializer_list<juce::MidiMessage> messages)
{
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    int at = 0;
    for (const auto& m : messages)
        midi.addEvent (m, at++);
    p.processBlock (buffer, midi);
    p.dispatchMidi();
}

juce::MidiMessage cc (int number, int value) { return juce::MidiMessage::controllerEvent (1, number, value); }

float normalised (PluginProcessor& p, const char* id) { return p.state().getParameter (id)->getValue(); }

bool near (float a, float b) { return std::abs (a - b) < 1e-5f; }
} // namespace

TEST_CASE ("MIDI learn assigns the next CC and the CC sets the parameter", "[midi]")
{
    PluginProcessor plugin;
    auto& learn = plugin.midiLearn();
    learn.beginLearn (tw::ids::fxDrive);
    CHECK (learn.learning() == tw::ids::fxDrive);
    // the learning CC is taken, not applied
    const float before = normalised (plugin, tw::ids::fxDrive);
    send (plugin, { cc (21, 100) });
    CHECK (learn.learning().isEmpty());
    CHECK (learn.ccFor (tw::ids::fxDrive) == 21);
    CHECK (learn.parameterFor (21) == tw::ids::fxDrive);
    CHECK (near (normalised (plugin, tw::ids::fxDrive), before));

    // 0..127 -> 0..1; several values in one block: the last one counts
    send (plugin, { cc (21, 127) });
    CHECK (near (normalised (plugin, tw::ids::fxDrive), 1.0f));
    send (plugin, { cc (21, 10), cc (21, 0) });
    CHECK (near (normalised (plugin, tw::ids::fxDrive), 0.0f));
    send (plugin, { cc (21, 64) });
    CHECK (std::abs (normalised (plugin, tw::ids::fxDrive) - 64.0f / 127.0f) < 1e-3f);

    // a learned CC is not passed on to the instrument, an unassigned one is
    CHECK (learn.handleController (21, 5));
    CHECK (!learn.handleController (22, 5));
    plugin.dispatchMidi();

    // a switch and a choice: above the middle is on, the range is spread over the choices
    learn.assign (30, tw::ids::reverbOn);
    learn.assign (31, tw::ids::panWave);
    send (plugin, { cc (30, 127), cc (31, 127) });
    CHECK (plugin.state().getRawParameterValue (tw::ids::reverbOn)->load() > 0.5f);
    CHECK (near (normalised (plugin, tw::ids::panWave), 1.0f));
    send (plugin, { cc (30, 0) });
    CHECK (plugin.state().getRawParameterValue (tw::ids::reverbOn)->load() < 0.5f);
}

TEST_CASE ("One CC per parameter and one parameter per CC", "[midi]")
{
    PluginProcessor plugin;
    auto& learn = plugin.midiLearn();
    learn.assign (21, tw::ids::fxDrive);
    learn.assign (22, tw::ids::fxDrive); // the parameter moves to CC 22
    CHECK (learn.ccFor (tw::ids::fxDrive) == 22);
    CHECK (learn.parameterFor (21).isEmpty());
    learn.assign (22, tw::ids::volume); // CC 22 now sets the volume
    CHECK (learn.ccFor (tw::ids::fxDrive) == -1);
    CHECK (learn.ccFor (tw::ids::volume) == 22);
    CHECK (learn.size() == 1);

    learn.assign (23, tw::ids::delayMix);
    learn.forget (tw::ids::volume);
    CHECK (learn.ccFor (tw::ids::volume) == -1);
    CHECK (learn.size() == 1);
    learn.clear();
    CHECK (learn.size() == 0);
    learn.assign (24, "no_such_parameter");
    CHECK (learn.size() == 0);
}

TEST_CASE ("CC64 and the channel mode messages cannot be learned; CC1 and CC7 can", "[midi]")
{
    PluginProcessor plugin;
    auto& learn = plugin.midiLearn();
    for (const int reserved : { 64, 120, 121, 123, 127 })
    {
        INFO ("CC " << reserved);
        CHECK (!tw::MidiLearn::learnable (reserved));
        learn.beginLearn (tw::ids::volume);
        send (plugin, { cc (reserved, 0) });
        CHECK (learn.learning() == tw::ids::volume); // still waiting
        CHECK (learn.ccFor (tw::ids::volume) == -1);
        learn.assign (reserved, tw::ids::volume);
        CHECK (learn.size() == 0);
        CHECK (!learn.handleController (reserved, 0));
    }
    send (plugin, { cc (1, 0) });
    CHECK (learn.ccFor (tw::ids::volume) == 1);
    learn.assign (7, tw::ids::reverbMix);
    CHECK (learn.ccFor (tw::ids::reverbMix) == 7);
    learn.cancelLearn();
    CHECK (learn.learning().isEmpty());
}

TEST_CASE ("The MIDI assignments are part of the plugin state and of user presets", "[midi][presets]")
{
    PluginProcessor plugin;
    plugin.midiLearn().assign (21, tw::ids::fxDrive);
    plugin.midiLearn().assign (1, tw::ids::volume);

    juce::MemoryBlock block;
    plugin.getStateInformation (block);
    PluginProcessor other;
    other.midiLearn().assign (40, tw::ids::delayMix); // replaced by the restored state
    other.setStateInformation (block.getData(), int (block.getSize()));
    CHECK (other.midiLearn().ccFor (tw::ids::fxDrive) == 21);
    CHECK (other.midiLearn().ccFor (tw::ids::volume) == 1);
    CHECK (other.midiLearn().ccFor (tw::ids::delayMix) == -1);
    send (other, { cc (21, 127) });
    CHECK (near (normalised (other, tw::ids::fxDrive), 1.0f));

    // a state without assignments (an older project) clears them
    PluginProcessor fresh;
    juce::MemoryBlock empty;
    fresh.getStateInformation (empty);
    other.setStateInformation (empty.getData(), int (empty.getSize()));
    CHECK (other.midiLearn().size() == 0);

    // a user preset brings its assignments along; a factory preset keeps the current ones
    auto& presets = plugin.presets();
    const auto dir = juce::File::createTempFile ("tonewerk-midi-presets");
    presets.setFolder (dir);
    REQUIRE (presets.save ("With CCs"));
    plugin.midiLearn().clear();
    plugin.midiLearn().assign (50, tw::ids::reverbMix);
    REQUIRE (presets.loadFactory (1));
    CHECK (plugin.midiLearn().ccFor (tw::ids::reverbMix) == 50);
    REQUIRE (presets.loadUser ("With CCs"));
    CHECK (plugin.midiLearn().ccFor (tw::ids::fxDrive) == 21);
    CHECK (plugin.midiLearn().ccFor (tw::ids::volume) == 1);
    CHECK (plugin.midiLearn().ccFor (tw::ids::reverbMix) == -1);
    dir.deleteRecursively();
}

TEST_CASE ("A MIDI program change loads the factory preset of its number", "[midi][presets]")
{
    PluginProcessor plugin;
    const auto names = plugin.presets().factoryNames();
    // one program for the host: the factory presets as host programs broke pluginval's state restoration (VST3 "Program")
    CHECK (plugin.getNumPrograms() == 1);

    send (plugin, { juce::MidiMessage::programChange (1, 3) });
    CHECK (plugin.presets().currentName() == names[3]);
    CHECK (plugin.presets().currentIsFactory());

    // the preset's instrument and effects (Clavinet Funk: the drive)
    const int clav = names.indexOf ("Clavinet Funk");
    REQUIRE (clav >= 0);
    send (plugin, { juce::MidiMessage::programChange (1, clav) });
    CHECK (int (plugin.state().getRawParameterValue (tw::ids::instrument)->load()) == tw::instrumentIndex ("clavinet"));
    CHECK (std::abs (plugin.state().getRawParameterValue (tw::ids::fxDrive)->load() - 40.0f) < 0.01f);

    // beyond the list: nothing happens
    send (plugin, { juce::MidiMessage::programChange (1, 127) });
    CHECK (plugin.presets().currentName() == names[clav]);

    // the same program again restores the preset after an edit
    auto* volume = plugin.state().getParameter (tw::ids::volume);
    volume->setValueNotifyingHost (0.2f);
    send (plugin, { juce::MidiMessage::programChange (1, clav) });
    CHECK (!near (volume->getValue(), 0.2f));
}

TEST_CASE ("The MIDI learn menu", "[midi][editor]")
{
    PluginProcessor plugin;
    auto& learn = plugin.midiLearn();
    auto texts = [&] (const char* id) {
        juce::StringArray items;
        const auto menu = tw::learnui::menu (learn, id);
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (!it.getItem().isSeparator)
                items.add (it.getItem().text + (it.getItem().isEnabled ? "" : " (disabled)"));
        return items;
    };
    CHECK (texts (tw::ids::fxDrive) == juce::StringArray ("MIDI Learn", "Forget CC (disabled)", "Clear all (disabled)"));
    learn.assign (21, tw::ids::fxDrive);
    CHECK (texts (tw::ids::fxDrive) == juce::StringArray ("MIDI Learn", "Forget CC 21", "Clear all"));
    learn.beginLearn (tw::ids::fxDrive);
    CHECK (texts (tw::ids::fxDrive)[0] == "Cancel MIDI Learn");
    CHECK (texts (tw::ids::volume)[0] == "MIDI Learn");

    // the menu's actions
    learn.cancelLearn();
    const auto menu = tw::learnui::menu (learn, tw::ids::fxDrive);
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text.startsWith ("Forget"))
            it.getItem().action();
    CHECK (learn.ccFor (tw::ids::fxDrive) == -1);
    CHECK (tw::learnui::tooltip ("Drive", 21, false) == "Drive (CC 21)");
}
