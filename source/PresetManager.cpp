// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "PresetManager.h"

#include "MidiLearn.h"
#include "Parameters.h"

namespace tw
{
namespace
{
    constexpr const char* kFactoryProperty = "presetFactory";

    struct Factory
    {
        const char* name;
        const char* instrument; // engine preset id (Parameters.cpp, instruments())
        std::vector<std::pair<const char*, float>> values;
    };

    // the effect values not named stay at their defaults (Parameters.cpp)
    const std::vector<Factory>& factoryPresets()
    {
        static const std::vector<Factory> presets {
            { "Mark I Stage", "mark1", {} },
            { "Mark I Vari-Pan", "mark1", { { ids::fxOn, 1 }, { ids::panOn, 1 }, { ids::panRate, 3.5f }, { ids::panDepth, 70 },
                                              { ids::delayOn, 0 } } },
            { "Mark II Phaser", "mark2", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::phaserOn, 1 }, { ids::phaserRate, 0.6f },
                                             { ids::phaserDepth, 60 }, { ids::delayMix, 15 } } },
            { "Mark V", "mark5", {} },
            { "Mark 7 Chorus", "mark7", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::chorusOn, 1 }, { ids::chorusRate, 0.7f },
                                            { ids::chorusDepth, 2.5f }, { ids::delayMix, 15 } } },
            { "Mark 8 (MK8 chain)", "mark8", { { ids::fxOn, 1 } } },
            { "Sparkle Top", "sparkletop", {} },
            { "Wurlitzer 200A", "wurli", {} },
            { "Wurlitzer Chorus", "wurli", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::chorusOn, 1 }, { ids::chorusRate, 0.8f },
                                               { ids::chorusDepth, 3 }, { ids::delayMix, 15 } } },
            { "Pianet T", "pianet", {} },
            // 4 dB down: drive, compressor make-up and the mid boost brought it to the soft clip even when played softly
            { "Clavinet Funk", "clavinet", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::delayOn, 0 }, { ids::fxDrive, 40 },
                                               { ids::compAmount, 60 }, { ids::fxMid, 4 }, { ids::fxMidFreq, 1200 },
                                               { ids::volume, -4 } } },
            // the pianos in a room
            { "Splendid Grand", "splendid_hybrid", { { ids::reverbOn, 1 }, { ids::reverbMix, 24 }, { ids::reverbSize, 60 },
                                                       { ids::reverbDecay, 2.2f }, { ids::reverbDamping, 45 }, { ids::reverbPredelay, 20 } } },
            { "Salamander Grand", "salamander", { { ids::reverbOn, 1 }, { ids::reverbMix, 28 }, { ids::reverbSize, 65 },
                                                    { ids::reverbDecay, 2.4f }, { ids::reverbDamping, 45 }, { ids::reverbPredelay, 22 } } },
            { "Upright Piano", "upright", { { ids::reverbOn, 1 }, { ids::reverbMix, 15 }, { ids::reverbSize, 35 },
                                              { ids::reverbDecay, 1.2f }, { ids::reverbDamping, 55 }, { ids::reverbPredelay, 8 } } },
        };
        return presets;
    }

    void setParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : state_ (state), folder_ (defaultFolder()) {}

juce::File PresetManager::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ToneWerk").getChildFile ("Presets");
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (const auto& f : factoryPresets())
        names.add (f.name);
    return names;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray names;
    for (const auto& f : folder_.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

std::vector<PresetManager::Entry> PresetManager::entries() const
{
    std::vector<Entry> all;
    for (const auto& n : factoryNames())
        all.push_back ({ n, true });
    for (const auto& n : userNames())
        all.push_back ({ n, false });
    return all;
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return folder_.getChildFile (juce::File::createLegalFileName (name.trim()) + kExtension);
}

bool PresetManager::exists (const juce::String& name) const { return fileFor (name).existsAsFile(); }

void PresetManager::resetToDefaults()
{
    for (auto* p : state_.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            r->setValueNotifyingHost (r->getDefaultValue());
}

void PresetManager::setCurrent (const juce::String& name, bool factory)
{
    state_.state.setProperty (kNameProperty, name, nullptr);
    state_.state.setProperty (kFactoryProperty, factory, nullptr);
}

bool PresetManager::loadFactory (int index)
{
    const auto& all = factoryPresets();
    if (index < 0 || index >= int (all.size()))
        return false;
    const auto& f = all[size_t (index)];
    resetToDefaults();
    setParameter (state_, ids::instrument, float (juce::jmax (0, instrumentIndex (f.instrument))));
    for (const auto& [id, value] : f.values)
        setParameter (state_, id, value);
    setCurrent (f.name, true);
    return true;
}

bool PresetManager::loadUser (const juce::String& name)
{
    const auto xml = juce::XmlDocument::parse (fileFor (name));
    if (xml == nullptr || !xml->hasTagName (state_.state.getType()))
        return false;
    // through the parameters, not replaceState: the host hears every change, parameters missing in an old file get defaults
    const auto tree = juce::ValueTree::fromXml (*xml);
    resetToDefaults();
    for (const auto& id : allParameterIds())
    {
        const auto child = tree.getChildWithProperty ("id", id);
        if (child.isValid() && child.hasProperty ("value"))
            setParameter (state_, id, float (child.getProperty ("value")));
    }
    // a preset saved with MIDI assignments brings them along; one without keeps the current ones
    if (const auto map = tree.getChildWithName (MidiLearn::kTreeType); map.isValid())
    {
        if (const auto old = state_.state.getChildWithName (MidiLearn::kTreeType); old.isValid())
            state_.state.removeChild (old, nullptr);
        state_.state.appendChild (map.createCopy(), nullptr);
    }
    setCurrent (name.trim(), false);
    return true;
}

bool PresetManager::load (const Entry& entry)
{
    return entry.factory ? loadFactory (factoryNames().indexOf (entry.name)) : loadUser (entry.name);
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = name.trim();
    if (clean.isEmpty() || !folder_.createDirectory())
        return false;
    auto tree = state_.copyState();
    tree.setProperty (kNameProperty, clean, nullptr);
    tree.removeProperty (kFactoryProperty, nullptr);
    const auto xml = tree.createXml();
    if (xml == nullptr || !xml->writeTo (fileFor (clean)))
        return false;
    setCurrent (clean, false);
    return true;
}

bool PresetManager::remove (const juce::String& name)
{
    const auto f = fileFor (name);
    if (!f.existsAsFile() || !f.moveToTrash())
        return false;
    if (!currentIsFactory() && currentName() == name.trim())
        setCurrent ({}, false);
    return true;
}

bool PresetManager::step (int direction)
{
    const auto all = entries();
    if (all.empty())
        return false;
    int current = -1;
    for (int i = 0; i < int (all.size()); ++i)
        if (all[size_t (i)].name == currentName() && all[size_t (i)].factory == currentIsFactory())
            current = i;
    const int n = int (all.size());
    const int next = current < 0 ? (direction > 0 ? 0 : n - 1) : ((current + direction) % n + n) % n;
    return load (all[size_t (next)]);
}

juce::String PresetManager::currentName() const { return state_.state.getProperty (kNameProperty).toString(); }

bool PresetManager::currentIsFactory() const { return bool (state_.state.getProperty (kFactoryProperty, false)); }
} // namespace tw
