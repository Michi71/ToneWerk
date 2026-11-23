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
            // the effect presets 2-3.5 dB down: a chord at velocity 100 already had the limiter working ("[.presetlevels]")
            { "Mark I Vari-Pan", "mark1", { { ids::fxOn, 1 }, { ids::panOn, 1 }, { ids::panRate, 3.5f }, { ids::panDepth, 70 },
                                              { ids::delayOn, 0 }, { ids::volume, -3 } } },
            { "Mark II Phaser", "mark2", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::phaserOn, 1 }, { ids::phaserRate, 0.6f },
                                             { ids::phaserDepth, 60 }, { ids::delayMix, 15 }, { ids::volume, -2 } } },
            { "Mark V", "mark5", {} },
            { "Mark 7 Chorus", "mark7", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::chorusOn, 1 }, { ids::chorusRate, 0.7f },
                                            { ids::chorusDepth, 2.5f }, { ids::delayMix, 15 }, { ids::volume, -3.5f } } },
            { "Mark 8 (MK8 chain)", "mark8", { { ids::fxOn, 1 }, { ids::volume, -2.5f } } },
            { "Sparkle Top", "sparkletop", {} },
            { "Wurlitzer 200A", "wurli", {} },
            { "Wurlitzer Chorus", "wurli", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::chorusOn, 1 }, { ids::chorusRate, 0.8f },
                                               { ids::chorusDepth, 3 }, { ids::delayMix, 15 }, { ids::volume, -3.5f } } },
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
            // since 0.3: voicing, the velocity curve, transpose and tune in use (new ones go at the end: the place in the
            // list is the MIDI program)
            { "Mark I Bark", "mark1", { { ids::hardness, 60 }, { ids::pickupOffset, 0.35f }, { ids::pickupGap, -30 },
                                         { ids::velocityCurve, -15 }, { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::delayOn, 0 },
                                         { ids::fxDrive, 35 }, { ids::fxTreble, 5 }, { ids::fxBass, -2 }, { ids::volume, -3 } } },
            { "Mark I Ballad", "mark1", { { ids::hardness, -80 }, { ids::pickupGap, 80 }, { ids::velocityCurve, 25 },
                                           { ids::humanize, 60 }, { ids::reverbOn, 1 }, { ids::reverbMix, 22 },
                                           { ids::reverbSize, 55 }, { ids::reverbDecay, 2.0f }, { ids::reverbPredelay, 25 } } },
            { "Mark II Suitcase Tremolo", "mark2", { { ids::fxOn, 1 }, { ids::panOn, 1 }, { ids::panWave, 3 }, { ids::panRate, 5.5f },
                                                      { ids::panDepth, 60 }, { ids::delayOn, 0 }, { ids::fxDrive, 15 },
                                                      { ids::fxBass, 4 }, { ids::volume, -1.5f } } },
            { "Mark V Glass", "mark5", { { ids::hardness, 40 }, { ids::strikePoint, 40 }, { ids::fxOn, 1 }, { ids::panOn, 0 },
                                          { ids::delayOn, 0 }, { ids::fxDrive, 10 }, { ids::chorusOn, 1 }, { ids::chorusRate, 0.5f },
                                          { ids::chorusDepth, 2 }, { ids::reverbOn, 1 }, { ids::reverbMix, 18 },
                                          { ids::reverbDecay, 1.8f }, { ids::volume, -1.5f } } },
            { "Sparkle Top Dream", "sparkletop", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::fxDrive, 10 }, { ids::chorusOn, 1 },
                                                    { ids::chorusRate, 0.4f }, { ids::chorusDepth, 3 }, { ids::delayOn, 1 },
                                                    { ids::delayTime, 0.38f }, { ids::delayFeedback, 40 }, { ids::delayMix, 28 },
                                                    { ids::delayPingPong, 1 }, { ids::reverbOn, 1 }, { ids::reverbMix, 30 },
                                                    { ids::reverbSize, 80 }, { ids::reverbDecay, 4.0f }, { ids::reverbPredelay, 30 },
                                                    { ids::volume, -1.5f } } },
            { "Wurlitzer Overdrive", "wurli", { { ids::hardness, 40 }, { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::delayOn, 0 },
                                                 { ids::fxDrive, 70 }, { ids::fxMid, 3 }, { ids::fxMidFreq, 900 },
                                                 { ids::compAmount, 50 }, { ids::volume, -5 } } },
            { "Wurlitzer Tremolo", "wurli", { { ids::fxOn, 1 }, { ids::panOn, 1 }, { ids::panWave, 3 }, { ids::panRate, 6.0f },
                                               { ids::panDepth, 55 }, { ids::delayOn, 0 }, { ids::fxDrive, 20 },
                                               { ids::reverbOn, 1 }, { ids::reverbMix, 12 }, { ids::reverbSize, 30 },
                                               { ids::reverbDecay, 1.0f }, { ids::volume, -2 } } },
            { "Pianet Echo", "pianet", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::fxDrive, 15 }, { ids::delayOn, 1 },
                                          { ids::delayTime, 0.33f }, { ids::delayFeedback, 35 }, { ids::delayMix, 30 },
                                          { ids::delayPingPong, 1 }, { ids::reverbOn, 1 }, { ids::reverbMix, 15 }, { ids::volume, -3 } } },
            { "Clavinet Octave Bass", "clavinet", { { ids::transpose, -12 }, { ids::hardness, -40 }, { ids::fxOn, 1 }, { ids::panOn, 0 },
                                                     { ids::delayOn, 0 }, { ids::fxDrive, 20 }, { ids::fxBass, 4 },
                                                     { ids::fxTreble, -3 }, { ids::compAmount, 50 } } },
            { "Clavinet Phaser", "clavinet", { { ids::fxOn, 1 }, { ids::panOn, 0 }, { ids::delayOn, 0 }, { ids::fxDrive, 20 },
                                                { ids::phaserOn, 1 }, { ids::phaserRate, 0.4f }, { ids::phaserDepth, 70 },
                                                { ids::volume, -1 } } },
            { "Splendid Grand Intimate", "splendid_hybrid", { { ids::hardness, -100 }, { ids::velocityCurve, 30 }, { ids::humanize, 60 },
                                                               { ids::reverbOn, 1 }, { ids::reverbMix, 14 }, { ids::reverbSize, 30 },
                                                               { ids::reverbDecay, 1.0f }, { ids::reverbDamping, 60 },
                                                               { ids::reverbPredelay, 10 } } },
            { "Salamander Concert Hall", "salamander", { { ids::stretch, 2 }, { ids::reverbOn, 1 }, { ids::reverbMix, 34 },
                                                          { ids::reverbSize, 88 }, { ids::reverbDecay, 3.6f },
                                                          { ids::reverbDamping, 40 }, { ids::reverbPredelay, 35 },
                                                          { ids::volume, -2 } } },
            { "Salamander A=432", "salamander", { { ids::tune, -31.8f }, { ids::reverbOn, 1 }, { ids::reverbMix, 24 },
                                                   { ids::reverbSize, 60 }, { ids::reverbDecay, 2.2f }, { ids::reverbPredelay, 20 } } },
            { "Honky-Tonk Upright", "upright", { { ids::unison, 150 }, { ids::hardness, 80 }, { ids::tune, -12 },
                                                  { ids::reverbOn, 1 }, { ids::reverbMix, 10 }, { ids::reverbSize, 25 },
                                                  { ids::reverbDecay, 0.8f }, { ids::reverbPredelay, 5 } } },
            { "Felt Upright", "upright", { { ids::hardness, -150 }, { ids::decay, -30 }, { ids::velocityCurve, 30 },
                                            { ids::reverbOn, 1 }, { ids::reverbMix, 18 }, { ids::reverbSize, 40 },
                                            { ids::reverbDecay, 1.4f }, { ids::reverbDamping, 65 }, { ids::reverbPredelay, 12 } } },
            // seven Rhodes intros after the patches of a sample pack: effects as in the patch, then voicing, tone controls,
            // Vari-Pan depth and level fitted to the pack's recordings (third-octave spectrum, left/right balance, RMS)
            { "Just The 2 Of Us", "mark1", { { ids::volume, 6 }, { ids::fxOn, 1 }, { ids::fxDrive, 0 },
                                                   { ids::fxEnvelope, 0 }, { ids::fxBass, 1.5f }, { ids::fxMid, -2 },
                                                   { ids::fxMidFreq, 315 }, { ids::fxTreble, -6 }, { ids::panOn, 1 },
                                                   { ids::panRate, 1.49f }, { ids::panDepth, 40 }, { ids::compOn, 0 },
                                                   { ids::chorusOn, 0 }, { ids::phaserOn, 1 }, { ids::phaserRate, 0.33f },
                                                   { ids::phaserDepth, 84 }, { ids::delayOn, 0 }, { ids::reverbOn, 1 },
                                                   { ids::reverbMix, 26.3f }, { ids::reverbSize, 80 },
                                                   { ids::reverbDecay, 2.81f }, { ids::reverbPredelay, 0 },
                                                   { ids::pickupGap, -35 }, { ids::pickupOffset, 0.12f } } },
            { "100 Ways", "mark1", { { ids::volume, -0.9f }, { ids::fxOn, 1 }, { ids::fxDrive, 0 },
                                           { ids::fxEnvelope, 0 }, { ids::fxBass, 4.7f }, { ids::fxMid, 0.8f },
                                           { ids::fxMidFreq, 631 }, { ids::fxTreble, -0.8f }, { ids::panOn, 0 },
                                           { ids::compOn, 0 }, { ids::chorusOn, 0 }, { ids::phaserOn, 1 },
                                           { ids::phaserRate, 0.53f }, { ids::phaserDepth, 71.1f }, { ids::delayOn, 0 },
                                           { ids::reverbOn, 0 }, { ids::hardness, -30 }, { ids::pickupGap, 40 } } },
            { "How Deep Is Your Love", "mark1", { { ids::volume, 3.8f }, { ids::fxOn, 1 }, { ids::fxDrive, 0 },
                                                        { ids::fxEnvelope, 0 }, { ids::fxBass, -5.2f },
                                                        { ids::fxMid, 1.6f }, { ids::fxMidFreq, 763 },
                                                        { ids::fxTreble, 4 }, { ids::panOn, 1 }, { ids::panRate, 1.73f },
                                                        { ids::panDepth, 43.8f }, { ids::compOn, 0 }, { ids::chorusOn, 0 },
                                                        { ids::phaserOn, 1 }, { ids::phaserRate, 0.5f },
                                                        { ids::phaserDepth, 71.1f }, { ids::delayOn, 0 },
                                                        { ids::reverbOn, 1 }, { ids::reverbMix, 26.3f },
                                                        { ids::reverbSize, 80 }, { ids::reverbDecay, 2.81f },
                                                        { ids::reverbPredelay, 0 }, { ids::pickupGap, 30 } } },
            { "You Are The Sunshine Of My Life", "mark1", { { ids::volume, 2.4f }, { ids::fxOn, 1 },
                                                                  { ids::fxDrive, 0 }, { ids::fxEnvelope, 0 },
                                                                  { ids::fxBass, -3.9f }, { ids::fxMid, 0.4f },
                                                                  { ids::fxMidFreq, 763 }, { ids::fxTreble, 4 },
                                                                  { ids::panOn, 1 }, { ids::panRate, 2.44f },
                                                                  { ids::panDepth, 33.7f }, { ids::compOn, 0 },
                                                                  { ids::chorusOn, 0 }, { ids::phaserOn, 0 },
                                                                  { ids::delayOn, 0 }, { ids::reverbOn, 0 },
                                                                  { ids::hardness, -60 }, { ids::pickupGap, 40 },
                                                                  { ids::pickupOffset, -0.26f } } },
            { "Riders On The Storm", "mark5", { { ids::volume, -2.4f }, { ids::fxOn, 1 }, { ids::fxDrive, 12.8f },
                                                      { ids::fxEnvelope, 0 }, { ids::fxBass, 3.4f }, { ids::fxMid, -6.6f },
                                                      { ids::fxMidFreq, 631 }, { ids::fxTreble, -1.3f }, { ids::panOn, 1 },
                                                      { ids::panRate, 3.04f }, { ids::panDepth, 48.9f },
                                                      { ids::compOn, 0 }, { ids::chorusOn, 0 }, { ids::phaserOn, 0 },
                                                      { ids::delayOn, 0 }, { ids::reverbOn, 0 }, { ids::pickupGap, 40 },
                                                      { ids::pickupOffset, -0.26f } } },
            { "Just The Way You Are", "mark7", { { ids::volume, -5 }, { ids::fxOn, 1 }, { ids::fxDrive, 10.3f },
                                                       { ids::fxEnvelope, 0 }, { ids::fxBass, -2.2f },
                                                       { ids::fxMid, 1.9f }, { ids::fxMidFreq, 590 }, { ids::fxTreble, 8 },
                                                       { ids::panOn, 1 }, { ids::panRate, 3.04f },
                                                       { ids::panDepth, 19.6f }, { ids::compOn, 0 }, { ids::chorusOn, 0 },
                                                       { ids::phaserOn, 1 }, { ids::phaserRate, 0.34f },
                                                       { ids::phaserDepth, 71.1f }, { ids::delayOn, 0 },
                                                       { ids::reverbOn, 0 }, { ids::hardness, 90 },
                                                       { ids::pickupGap, -20 }, { ids::pickupOffset, -0.26f } } },
            { "Livin For The City", "sparkletop", { { ids::volume, -6.7f }, { ids::fxOn, 1 },
                                                          { ids::fxDrive, 11.6f }, { ids::fxEnvelope, 0 },
                                                          { ids::fxBass, 9.4f }, { ids::fxMid, -10 },
                                                          { ids::fxMidFreq, 2000 }, { ids::fxTreble, -1.3f },
                                                          { ids::panOn, 1 }, { ids::panRate, 1.7f }, { ids::panDepth, 50 },
                                                          { ids::compOn, 0 }, { ids::chorusOn, 0 }, { ids::phaserOn, 0 },
                                                          { ids::delayOn, 0 }, { ids::reverbOn, 0 }, { ids::hardness, 60 },
                                                          { ids::pickupGap, 30 }, { ids::pickupOffset, 0.06f } } },
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
