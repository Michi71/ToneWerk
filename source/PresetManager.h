// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// PresetManager.h - factory presets (in the code) and user presets (XML files, one per preset)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tw
{
class PresetManager
{
public:
    static constexpr const char* kExtension = ".twpreset";
    static constexpr const char* kNameProperty = "presetName";

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    // ~/Documents/ToneWerk/Presets (created on demand)
    static juce::File defaultFolder();
    void setFolder (const juce::File& folder) { folder_ = folder; }
    const juce::File& folder() const { return folder_; }

    struct Entry
    {
        juce::String name;
        bool factory = false;
    };
    // the factory presets first, then the user presets in alphabetical order
    std::vector<Entry> entries() const;
    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;

    // message thread: set the parameters (notifying the host) and remember the name. The MIDI assignments (MidiLearn) stay,
    // unless a user preset was saved with its own
    bool load (const Entry& entry);
    bool loadFactory (int index);
    bool loadUser (const juce::String& name);
    // the current parameters as a user preset; an existing one of the same name is overwritten
    bool save (const juce::String& name);
    bool remove (const juce::String& name);
    bool exists (const juce::String& name) const;

    // the next or previous entry after the current one (wraps)
    bool step (int direction);

    juce::String currentName() const;
    bool currentIsFactory() const;

private:
    juce::File fileFor (const juce::String& name) const;
    void resetToDefaults();
    void setCurrent (const juce::String& name, bool factory);

    juce::AudioProcessorValueTreeState& state_;
    juce::File folder_;
};
} // namespace tw
