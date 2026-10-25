// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "helpers/test_helpers.h"

#include <BankLocator.h>
#include <Parameters.h>
#include <PluginProcessor.h>
#include <PresetManager.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <core/params.h>

namespace
{
bool banksAvailable() { return tw::findBank (1) != juce::File(); }

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

TEST_CASE ("The instruments are the presets of the engine", "[parameters]")
{
    CHECK (tw::instrumentNames().size() == pmp::Params::presetCount());
    for (int i = 0; i < pmp::Params::presetCount(); ++i)
        CHECK (tw::instrumentId (i) == juce::String (pmp::Params::presetId (i)));

    PluginProcessor plugin;
    for (const auto& id : tw::allParameterIds())
        CHECK (plugin.state().getParameter (id) != nullptr);
    CHECK (int (plugin.getParameters().size()) == tw::allParameterIds().size());
}

TEST_CASE ("Banks are found in the source tree", "[banks]")
{
    if (!juce::File (TONEWERK_TEST_BANKS_DIR).getChildFile ("mark1_hybrid.bank").existsAsFile()
        || juce::File (TONEWERK_TEST_BANKS_DIR).getChildFile ("mark1_hybrid.bank").getSize() < 4096)
        SKIP ("banks/ is not checked out (Git LFS)");
    for (int slot = 0; slot < pmp::Engine::kHybridSlots; ++slot)
    {
        INFO ("slot " << slot);
        CHECK (tw::findBank (slot).getFileName() == tw::bankFileName (slot));
    }
}

TEST_CASE ("A note sounds, at the bank's rate and resampled", "[audio]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        DYNAMIC_SECTION ("host rate " << rate)
        {
            PluginProcessor plugin;
            plugin.setRateAndBufferSizeDetails (rate, 256);
            plugin.prepareToPlay (rate, 256);
            REQUIRE (waitForInstrument (plugin, 256));
            INFO (plugin.instrumentStatus());
            const auto [l, r] = playNote (plugin, rate, 256, 60, 1.0);
            CHECK (l > 1e-3);
            CHECK (r > 1e-3);
            CHECK (std::isfinite (l));
        }
    }
}

TEST_CASE ("Every instrument builds and plays or reports why not", "[audio][instruments]")
{
    if (!banksAvailable())
        SKIP ("no banks found");
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000.0, 512);
    plugin.prepareToPlay (48000.0, 512);
    for (int i = 0; i < pmp::Params::presetCount(); ++i)
    {
        INFO ("instrument " << pmp::Params::presetId (i));
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

// writes a PNG of the editor to $TONEWERK_SNAPSHOT (hidden: ./Tests "[.snapshot]")
TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("TONEWERK_SNAPSHOT", {});
    if (!juce::File::isAbsolutePath (path))
        SKIP ("TONEWERK_SNAPSHOT not set");
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);
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
    if (!banksAvailable())
        SKIP ("no banks found");
    for (const double rate : { 44100.0, 48000.0 })
    {
        PluginProcessor plugin;
        constexpr int block = 64;
        plugin.setRateAndBufferSizeDetails (rate, block);
        plugin.prepareToPlay (rate, block);
        for (int i = 0; i < pmp::Params::presetCount(); ++i)
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
            WARN (juce::String (rate, 0) << " Hz  " << juce::String (pmp::Params::presetId (i)).paddedRight (' ', 10)
                  << " -40 dB at " << juce::String (first (0.01f), 2) << " ms, -20 dB at " << juce::String (first (0.1f), 2)
                  << " ms, peak at " << juce::String (1000.0 * double (peakAt) / rate, 2) << " ms");
        }
    }
}
