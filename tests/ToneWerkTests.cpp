// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "helpers/test_helpers.h"

#include <InstrumentVoice.h>
#include <Parameters.h>
#include <PluginProcessor.h>
#include <PresetManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sound_engine.h>

namespace
{
int instrumentCount() { return int (tw::instruments().size()); }

// processes empty blocks until the voice for the current parameters plays (the loader builds it on its thread)
bool waitForInstrument (PluginProcessor& p, int blockSize, double seconds = 30.0)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto until = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < until)
    {
        p.processBlock (buffer, midi);
        if (p.instrumentReady())
            return true;
        juce::Thread::sleep (5);
    }
    return false;
}

// plays one note for `seconds` and returns the RMS of the left and right channel
std::pair<double, double> playNote (PluginProcessor& p, double rate, int blockSize, int note, double seconds)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    double sumL = 0.0, sumR = 0.0;
    long frames = 0;
    const int blocks = int (seconds * rate / blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 7);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
        {
            sumL += double (buffer.getSample (0, i)) * buffer.getSample (0, i);
            sumR += double (buffer.getSample (1, i)) * buffer.getSample (1, i);
        }
        frames += blockSize;
    }
    return { std::sqrt (sumL / double (frames)), std::sqrt (sumR / double (frames)) };
}

void setParam (PluginProcessor& p, const char* id, float value)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
} // namespace

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor plugin;
    CHECK_THAT (plugin.getName().toStdString(), Catch::Matchers::Equals ("ToneWerk"));
    CHECK (plugin.getProgramName (0).isNotEmpty());
    CHECK (plugin.acceptsMidi());
}

TEST_CASE ("The instruments are the presets of the three engines", "[parameters]")
{
    using tw::engines::Family;
    CHECK (tw::instrumentNames().size() == instrumentCount());
    // every preset of every engine is an instrument, and every instrument a preset of its engine
    int presets = 0;
    for (const auto family : { Family::Tines, Family::Reeds, Family::SynthKeys })
        for (int k = 0; k < tw::engines::presetCount (family); ++k, ++presets)
        {
            const auto id = tw::engines::presetId (family, k);
            INFO (id);
            const int index = tw::instrumentIndex (id);
            REQUIRE (index >= 0);
            CHECK (tw::instrument (index).family == family);
        }
    CHECK (presets == instrumentCount());

    PluginProcessor plugin;
    for (const auto& id : tw::allParameterIds())
        CHECK (plugin.state().getParameter (id) != nullptr);
    CHECK (int (plugin.getParameters().size()) == tw::allParameterIds().size());
}

TEST_CASE ("A note sounds at every host rate", "[audio]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        DYNAMIC_SECTION ("host rate " << rate)
        {
            PluginProcessor plugin;
            plugin.setRateAndBufferSizeDetails (rate, 256);
            plugin.prepareToPlay (rate, 256);
            setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
            REQUIRE (waitForInstrument (plugin, 256));
            INFO (plugin.instrumentStatus());
            const auto [l, r] = playNote (plugin, rate, 256, 60, 1.0);
            CHECK (l > 1e-3);
            CHECK (r > 1e-3);
            CHECK (std::isfinite (l));
        }
    }
}

TEST_CASE ("Every instrument builds and plays", "[audio][instruments]")
{
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    for (int i = 0; i < instrumentCount(); ++i)
    {
        INFO ("instrument " << tw::instrumentId (i));
        setParam (plugin, tw::ids::instrument, float (i));
        REQUIRE (waitForInstrument (plugin, 512));
        INFO (plugin.instrumentStatus());
        const auto [l, r] = playNote (plugin, 48000.0, 512, 64, 0.6);
        CHECK (l > 1e-4);
        CHECK (l < 1.0);
    }
}

TEST_CASE ("The effect chain changes the sound and stays bounded", "[audio][fx]")
{
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    REQUIRE (waitForInstrument (plugin, 512));
    const auto dry = playNote (plugin, 48000.0, 512, 55, 0.8);
    setParam (plugin, tw::ids::fxOn, 1.0f);
    setParam (plugin, tw::ids::fxDrive, 100.0f);
    const auto wet = playNote (plugin, 48000.0, 512, 55, 0.8);
    CHECK (std::abs (wet.first - dry.first) > 1e-4);
    CHECK (wet.first < 1.0);
}

TEST_CASE ("The reverb lengthens the release", "[audio][reverb]")
{
    // energy from 0.2 to 1.2 s after the note-off, with the reverb off and on
    auto tail = [] (bool reverb) {
        PluginProcessor plugin;
        plugin.setRateAndBufferSizeDetails (48000.0, 512);
        plugin.prepareToPlay (48000.0, 512);
        setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
        REQUIRE (waitForInstrument (plugin, 512));
        setParam (plugin, tw::ids::reverbOn, reverb ? 1.0f : 0.0f);
        setParam (plugin, tw::ids::reverbDecay, 3.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        double energy = 0.0;
        for (int b = 0; b < int (2.0 * 48000.0 / 512); ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (b == int (0.3 * 48000.0 / 512))
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buffer.clear();
            plugin.processBlock (buffer, midi);
            const double t = double (b) * 512.0 / 48000.0 - 0.3;
            if (t >= 0.2 && t < 1.2)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        energy += double (buffer.getSample (ch, i)) * buffer.getSample (ch, i);
        }
        return energy;
    };
    const double dry = tail (false), wet = tail (true);
    WARN ("release tail: reverb off " << 10.0 * std::log10 (dry + 1e-30) << " dB, on " << 10.0 * std::log10 (wet + 1e-30) << " dB");
    CHECK (wet > 2.0 * dry);
}

// A switch flipped while a chord sounds: the largest sample-to-sample step within 5 ms of the switch against the largest one in the
// half second before it. A hard cut between the dry and the processed signal shows up as a step far above the signal's own.
static double switchClick (const char* switchId, bool on)
{
    PluginProcessor plugin;
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    plugin.setRateAndBufferSizeDetails (rate, block);
    plugin.prepareToPlay (rate, block);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    REQUIRE (waitForInstrument (plugin, block));
    setParam (plugin, switchId, on ? 0.0f : 1.0f);
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> outL;
    const int switchBlock = int (1.0 * rate / block);
    for (int b = 0; b < switchBlock + int (0.2 * rate / block); ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const int note : { 48, 55, 60, 64 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
        if (b == switchBlock)
            setParam (plugin, switchId, on ? 1.0f : 0.0f);
        buffer.clear();
        plugin.processBlock (buffer, midi);
        outL.insert (outL.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }
    const size_t at = size_t (switchBlock * block), win = size_t (0.005 * rate);
    double before = 0.0, around = 0.0;
    for (size_t i = at - size_t (0.5 * rate); i < at; ++i)
        before = std::max (before, double (std::abs (outL[i] - outL[i - 1])));
    for (size_t i = at; i < at + win; ++i)
        around = std::max (around, double (std::abs (outL[i] - outL[i - 1])));
    return around / before;
}

TEST_CASE ("Switching the effects while playing does not click", "[audio][fx]")
{
    for (const char* id : { tw::ids::fxOn, tw::ids::reverbOn })
        for (const bool on : { true, false })
        {
            const double ratio = switchClick (id, on);
            WARN (id << (on ? " on" : " off") << ": largest step at the switch / before = " << ratio);
            CHECK (ratio < 1.5);
        }
}

TEST_CASE ("User presets round-trip", "[presets]")
{
    PluginProcessor plugin;
    auto& presets = plugin.presets();
    const auto dir = juce::File::createTempFile ("tonewerk-presets");
    presets.setFolder (dir);

    setParam (plugin, tw::ids::instrument, 3.0f);
    setParam (plugin, tw::ids::fxOn, 1.0f);
    setParam (plugin, tw::ids::delayTime, 1.25f);
    REQUIRE (presets.save ("Test Preset"));
    CHECK (presets.userNames().contains ("Test Preset"));
    CHECK (presets.currentName() == "Test Preset");

    REQUIRE (presets.loadFactory (0));
    CHECK (presets.currentIsFactory());
    CHECK (plugin.state().getRawParameterValue (tw::ids::delayTime)->load() != 1.25f);

    REQUIRE (presets.loadUser ("Test Preset"));
    CHECK (int (plugin.state().getRawParameterValue (tw::ids::instrument)->load()) == 3);
    CHECK (plugin.state().getRawParameterValue (tw::ids::fxOn)->load() > 0.5f);
    CHECK (std::abs (plugin.state().getRawParameterValue (tw::ids::delayTime)->load() - 1.25f) < 0.01f);

    // the plugin state carries the preset name
    juce::MemoryBlock block;
    plugin.getStateInformation (block);
    PluginProcessor other;
    other.setStateInformation (block.getData(), int (block.getSize()));
    CHECK (other.presets().currentName() == "Test Preset");
    CHECK (int (other.state().getRawParameterValue (tw::ids::instrument)->load()) == 3);

    REQUIRE (presets.remove ("Test Preset"));
    CHECK (!presets.userNames().contains ("Test Preset"));
    dir.deleteRecursively();
}

TEST_CASE ("Every factory preset loads", "[presets]")
{
    PluginProcessor plugin;
    const auto names = plugin.presets().factoryNames();
    REQUIRE (!names.isEmpty());
    for (int i = 0; i < names.size(); ++i)
    {
        INFO (names[i]);
        CHECK (plugin.presets().loadFactory (i));
        CHECK (plugin.presets().currentName() == names[i]);
    }
}

TEST_CASE ("The editor opens", "[editor]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);
        CHECK (editor->getWidth() > 0);
    });
}

// writes a PNG of the editor to $TONEWERK_SNAPSHOT, with the instrument $TONEWERK_SNAPSHOT_INSTRUMENT (an engine preset id;
// default the first) (hidden: ./Tests "[.snapshot]")
TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("TONEWERK_SNAPSHOT", {});
    if (!juce::File::isAbsolutePath (path))
        SKIP ("TONEWERK_SNAPSHOT not set");
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);
    if (const auto id = juce::SystemStats::getEnvironmentVariable ("TONEWERK_SNAPSHOT_INSTRUMENT", {}); id.isNotEmpty())
    {
        REQUIRE (tw::instrumentIndex (id) >= 0);
        setParam (plugin, tw::ids::instrument, float (tw::instrumentIndex (id)));
    }
    REQUIRE (waitForInstrument (plugin, 512));
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditorAndMakeActive());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File file (path);
    file.deleteFile();
    juce::FileOutputStream out (file);
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, out));
    plugin.editorBeingDeleted (editor.get());
}

// how long a note takes from its note-on to the attack, per instrument (hidden: ./Tests "[.latency]")
TEST_CASE ("Note onset latency", "[.latency]")
{
    for (const double rate : { 44100.0, 48000.0 })
    {
        PluginProcessor plugin;
        constexpr int block = 64;
        plugin.setRateAndBufferSizeDetails (rate, block);
        plugin.prepareToPlay (rate, block);
        setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
        for (int i = 0; i < instrumentCount(); ++i)
        {
            setParam (plugin, tw::ids::instrument, float (i));
            REQUIRE (waitForInstrument (plugin, block));
            // let the switch fade settle, then play from silence
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer none;
            for (int b = 0; b < int (0.2 * rate / block); ++b)
                plugin.processBlock (buffer, none);
            std::vector<float> out;
            for (int b = 0; b < int (0.5 * rate / block); ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                buffer.clear();
                plugin.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            }
            float peak = 0.0f;
            size_t peakAt = 0;
            for (size_t k = 0; k < out.size(); ++k)
                if (std::abs (out[k]) > peak)
                    peak = std::abs (out[k]), peakAt = k;
            auto first = [&] (float fraction) {
                for (size_t k = 0; k < out.size(); ++k)
                    if (std::abs (out[k]) >= fraction * peak)
                        return 1000.0 * double (k) / rate;
                return -1.0;
            };
            WARN (juce::String (rate, 0) << " Hz  " << tw::instrumentId (i).paddedRight (' ', 10)
                  << " -40 dB at " << juce::String (first (0.01f), 2) << " ms, -20 dB at " << juce::String (first (0.1f), 2)
                  << " ms, peak at " << juce::String (1000.0 * double (peakAt) / rate, 2) << " ms");
        }
    }
}

// every instrument at the voicing extremes: level, peak and brightness against neutral (hidden: ./Tests "[.voicing]")
TEST_CASE ("Voicing extremes per instrument", "[.voicing]")
{
    struct Setting
    {
        const char* id;
        float value;
    };
    const std::vector<std::vector<Setting>> settings {
        {}, { { tw::ids::hardness, -200 } }, { { tw::ids::hardness, 200 } }, { { tw::ids::decay, -90 } }, { { tw::ids::decay, 300 } },
        { { tw::ids::unison, -100 } }, { { tw::ids::unison, 200 } }, { { tw::ids::stretch, -20 } }, { { tw::ids::stretch, 20 } },
        { { tw::ids::hammerMass, -80 } }, { { tw::ids::hammerMass, 200 } }, { { tw::ids::strikePoint, -50 } },
        { { tw::ids::strikePoint, 100 } }, { { tw::ids::gauge, -50 } }, { { tw::ids::gauge, 100 } }, { { tw::ids::pickupGap, -80 } },
        { { tw::ids::pickupGap, 300 } }, { { tw::ids::pickupOffset, -1 } }, { { tw::ids::pickupOffset, 1 } },
        { { tw::ids::tonebarDetune, -50 } }, { { tw::ids::tonebarDetune, 50 } }, { { tw::ids::damperRelease, -90 } },
        { { tw::ids::damperRelease, 300 } }
    };
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    for (int i = 0; i < instrumentCount(); ++i)
    {
        double rms0 = 0.0, bright0 = 0.0;
        for (const auto& set : settings)
        {
            for (const auto& id : tw::allVoicingIds())
                setParam (plugin, id.toRawUTF8(), 0.0f);
            setParam (plugin, tw::ids::instrument, float (i));
            if (!set.empty() && !tw::voicingIds (tw::instrument (i).family).contains (set[0].id))
                continue; // not a control of this instrument
            for (const auto& s : set)
                setParam (plugin, s.id, s.value);
            REQUIRE (waitForInstrument (plugin, 512));
            juce::AudioBuffer<float> buffer (2, 512);
            double sum = 0.0, diff = 0.0, peak = 0.0;
            float last = 0.0f;
            for (int b = 0; b < int (1.0 * 48000.0 / 512); ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (const int note : { 40, 52, 64, 76 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                buffer.clear();
                plugin.processBlock (buffer, midi);
                for (int k = 0; k < 512; ++k)
                {
                    const float x = buffer.getSample (0, k);
                    sum += double (x) * x;
                    diff += double (x - last) * (x - last); // first difference: a crude brightness measure
                    peak = std::max (peak, double (std::abs (x)));
                    last = x;
                }
            }
            const double rms = std::sqrt (sum / 48000.0), bright = std::sqrt (diff / std::max (sum, 1e-30));
            if (set.empty())
                rms0 = rms, bright0 = bright;
            WARN (tw::instrumentId (i).paddedRight (' ', 10)
                  << (set.empty() ? juce::String ("neutral") : juce::String (set[0].id) + "=" + juce::String (set[0].value)).paddedRight (' ', 24)
                  << " level " << juce::String (20.0 * std::log10 (rms / rms0), 1) << " dB, peak " << juce::String (peak, 3)
                  << ", brightness " << juce::String (20.0 * std::log10 (bright / bright0), 1) << " dB"
                  << (std::isfinite (rms) ? "" : "  NOT FINITE"));
        }
    }
}

TEST_CASE ("The voicing reset button sets the voicing back to neutral", "[editor]")
{
    PluginProcessor plugin;
    for (const char* id : { tw::ids::hardness, tw::ids::decay, tw::ids::unison, tw::ids::stretch, tw::ids::hammerMass, tw::ids::damperRelease })
        setParam (plugin, id, std::string (id) == tw::ids::stretch ? 10.0f : 50.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditorAndMakeActive());
    juce::TextButton* reset = nullptr;
    for (auto* c : editor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Reset")
            reset = b;
    REQUIRE (reset != nullptr);
    CHECK (reset->isEnabled());
    reset->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50); // triggerClick is asynchronous
    for (const auto& id : tw::allVoicingIds())
        CHECK (std::abs (plugin.state().getRawParameterValue (id)->load()) < 1e-4f);
    CHECK (!reset->isEnabled());
    plugin.editorBeingDeleted (editor.get());
}

// the level of every instrument before the soft clip: volume -24 dB, peaks and RMS referred back to 0 dB (hidden: "[.levels]")
TEST_CASE ("Instrument levels before the soft clip", "[.levels]")
{
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    setParam (plugin, tw::ids::volume, -24.0f);
    struct Play
    {
        const char* name;
        std::vector<int> notes;
        int velocity;
    };
    const std::vector<Play> plays { { "C4 v100", { 60 }, 100 }, { "C4 v127", { 60 }, 127 },
        { "chord4 v100", { 48, 55, 60, 64 }, 100 }, { "chord4 v127", { 48, 55, 60, 64 }, 127 },
        { "chord8 v127", { 36, 43, 48, 52, 55, 60, 64, 67 }, 127 } };
    for (int i = 0; i < instrumentCount(); ++i)
    {
        setParam (plugin, tw::ids::instrument, float (i));
        REQUIRE (waitForInstrument (plugin, 512));
        juce::String line = tw::instrumentId (i).paddedRight (' ', 16);
        for (const auto& play : plays)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            for (int b = 0; b < int (0.5 * 48000.0 / 512); ++b) // the previous notes off and gone
            {
                juce::MidiBuffer off;
                if (b == 0)
                    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                plugin.processBlock (buffer, off);
            }
            double peak = 0.0, sum = 0.0, early = 0.0;
            constexpr int earlyBlocks = int (0.2 * 48000.0 / 512);
            for (int b = 0; b < int (1.0 * 48000.0 / 512); ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (const int note : play.notes)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) play.velocity), 0);
                buffer.clear();
                plugin.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < 512; ++k)
                    {
                        const double x = buffer.getSample (ch, k);
                        peak = std::max (peak, std::abs (x));
                        sum += x * x;
                        early += b < earlyBlocks ? x * x : 0.0;
                    }
            }
            // peak / RMS over 1 s / RMS over the first 200 ms (the attack, where a decaying instrument is heard)
            const double peakDb = 20.0 * std::log10 (peak) + 24.0, rmsDb = 10.0 * std::log10 (sum / (2.0 * 48000.0)) + 24.0;
            const double earlyDb = 10.0 * std::log10 (early / (2.0 * earlyBlocks * 512.0)) + 24.0;
            line << "  " << play.name << " " << juce::String (peakDb, 1) << "/" << juce::String (rmsDb, 1) << "/"
                 << juce::String (earlyDb, 1);
        }
        WARN (line);
    }
}

// loud chords at volume 0 dB: the share of output samples inside the soft clip's range (|x| > 0.9), per instrument
static double clippedShare (PluginProcessor& plugin, const std::vector<int>& notes, int velocity)
{
    juce::AudioBuffer<float> buffer (2, 512);
    for (int b = 0; b < int (0.5 * 48000.0 / 512); ++b)
    {
        juce::MidiBuffer off;
        if (b == 0)
            off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        plugin.processBlock (buffer, off);
    }
    long clipped = 0, total = 0;
    for (int b = 0; b < int (1.0 * 48000.0 / 512); ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const int note : notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        buffer.clear();
        plugin.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 512; ++k, ++total)
                clipped += std::abs (buffer.getSample (ch, k)) > 0.9f ? 1 : 0;
    }
    return 100.0 * double (clipped) / double (total);
}

TEST_CASE ("Loud chords stay out of the soft clip", "[audio][levels]")
{
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    setParam (plugin, tw::ids::humanize, 0.0f); // measurements at the nominal velocity
    for (const char* id : { "wurli", "clavinet", "mark8", "salamander" })
    {
        REQUIRE (tw::instrumentIndex (id) >= 0);
        setParam (plugin, tw::ids::instrument, float (tw::instrumentIndex (id)));
        REQUIRE (waitForInstrument (plugin, 512));
        const double c4 = clippedShare (plugin, { 48, 55, 60, 64 }, 100), c8 = clippedShare (plugin, { 36, 43, 48, 52, 55, 60, 64, 67 }, 127);
        WARN (id << ": samples in the soft clip, chord4 v100 " << c4 << " %, chord8 v127 " << c8 << " %");
        CHECK (c4 < 0.01); // before the limiter and the level trims: up to 0.7 % (clavinet)
        CHECK (c8 < 0.5);  // before: 1.5-13.7 %
    }
}

// random combinations of every voicing control over its whole range, as a host's automation sets them: every instrument builds
// and plays finite (pluginval's editor automation crashed on Linux)
TEST_CASE ("Random voicing combinations build and play", "[audio][voicing]")
{
    juce::Random random (20261107);
    PluginProcessor plugin; // the parameter ranges
    for (int i = 0; i < instrumentCount(); ++i)
        for (int round = 0; round < 12; ++round)
        {
            tw::VoiceConfig config;
            config.instrument = i;
            auto value = [&] (const char* id) {
                auto* p = dynamic_cast<juce::RangedAudioParameter*> (plugin.state().getParameter (id));
                const float extreme = random.nextBool() ? 0.0f : 1.0f;
                return p->convertFrom0to1 (round % 2 == 0 ? random.nextFloat() : extreme);
            };
            auto& v = config.voicing;
            v.hardness = value (tw::ids::hardness);
            v.decay = value (tw::ids::decay);
            v.unison = value (tw::ids::unison);
            v.stretch = value (tw::ids::stretch);
            v.hammerMass = value (tw::ids::hammerMass);
            v.strikePoint = value (tw::ids::strikePoint);
            v.gauge = value (tw::ids::gauge);
            v.pickupGap = value (tw::ids::pickupGap);
            v.pickupOffset = value (tw::ids::pickupOffset);
            v.tonebarDetune = value (tw::ids::tonebarDetune);
            v.damperRelease = value (tw::ids::damperRelease);
            INFO (tw::instrumentId (i) << " round " << round << ": hardness " << v.hardness << " mass " << v.hammerMass << " strike "
                                       << v.strikePoint << " gauge " << v.gauge << " gap " << v.pickupGap << " offset " << v.pickupOffset
                                       << " tonebar " << v.tonebarDetune << " damper " << v.damperRelease << " decay " << v.decay
                                       << " unison " << v.unison << " stretch " << v.stretch);
            tw::InstrumentVoice voice (config);
            voice.prepare (48000.0, 512);
            REQUIRE (!voice.silent());
            std::vector<float> l (512), r (512);
            double sum = 0.0;
            for (int b = 0; b < 40; ++b)
            {
                if (b == 0)
                    for (const int note : { 30, 45, 60, 72, 88 })
                        voice.noteOn (note, 127);
                if (b == 20)
                    voice.allNotesOff();
                voice.render (l.data(), r.data(), 512);
                for (const float x : l)
                    sum += double (x) * x;
            }
            CHECK (std::isfinite (sum));
            CHECK (!voice.failed());
        }
}
