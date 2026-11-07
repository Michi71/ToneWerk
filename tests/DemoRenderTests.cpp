// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Plays a MIDI file through the plugin and writes what it sounds like (hidden):
//
//   TONEWERK_RENDER_MIDI=demo/ToneWerk-Demo.mid TONEWERK_RENDER_WAV=/path/demo.wav ./Builds/Tests "[.render]"
//
// A program change loads its factory preset as the plugin's timer would; the time the loader then needs to build the
// instrument is measured against the time the file leaves before its next note (it plays in real time in a host).
#include "helpers/test_helpers.h"

#include <Parameters.h>
#include <PluginProcessor.h>
#include <PresetManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE ("Render a MIDI file through the plugin", "[.render]")
{
    const auto midiPath = juce::SystemStats::getEnvironmentVariable ("TONEWERK_RENDER_MIDI", {});
    const auto wavPath = juce::SystemStats::getEnvironmentVariable ("TONEWERK_RENDER_WAV", {});
    if (!juce::File::isAbsolutePath (juce::File::getCurrentWorkingDirectory().getChildFile (midiPath).getFullPathName()) || midiPath.isEmpty())
        SKIP ("TONEWERK_RENDER_MIDI not set");
    const juce::File midiFile = juce::File::getCurrentWorkingDirectory().getChildFile (midiPath);
    juce::FileInputStream in (midiFile);
    REQUIRE (in.openedOk());
    juce::MidiFile file;
    REQUIRE (file.readFrom (in));
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence seq;
    for (int t = 0; t < file.getNumTracks(); ++t)
        seq.addSequence (*file.getTrack (t), 0.0);
    seq.updateMatchedPairs();

    constexpr double rate = 48000.0;
    constexpr int block = 256;
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (rate, block);
    plugin.prepareToPlay (rate, block);
    plugin.seedHumanize (1);
    auto waitReady = [&plugin] {
        juce::AudioBuffer<float> scratch (2, block);
        juce::MidiBuffer none;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        while (!plugin.instrumentReady() && juce::Time::getMillisecondCounterHiRes() - t0 < 30000.0)
        {
            plugin.processBlock (scratch, none);
            juce::Thread::sleep (1);
        }
        return juce::Time::getMillisecondCounterHiRes() - t0;
    };
    REQUIRE (waitReady() < 30000.0);

    const double length = seq.getEndTime() + 3.0;
    const int total = int (length * rate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    const auto names = plugin.presets().factoryNames();
    int next = 0, programs = 0, late = 0;
    double programAt = -1.0, buildMs = 0.0;
    juce::String report;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::MidiBuffer midi;
        bool programChange = false;
        for (; next < seq.getNumEvents(); ++next)
        {
            const auto& m = seq.getEventPointer (next)->message;
            const int at = int (std::lround (m.getTimeStamp() * rate)) - pos;
            if (at >= n)
                break;
            if (m.isMetaEvent())
                continue;
            midi.addEvent (m, juce::jmax (0, at));
            programChange = programChange || m.isProgramChange();
            if (m.isNoteOn() && programAt >= 0.0)
            {
                // the first note after a switch: the instrument must have been built in the time the file leaves
                const double leadMs = (m.getTimeStamp() - programAt) * 1000.0;
                report << "\n  " << plugin.presets().currentName().paddedRight (' ', 20) << " built in "
                       << juce::String (buildMs, 0) << " ms, lead " << juce::String (leadMs, 0) << " ms";
                if (buildMs > leadMs)
                {
                    ++late;
                    report << "  LATE";
                }
                programAt = -1.0;
            }
            if (m.isProgramChange())
                programAt = m.getTimeStamp();
        }
        juce::AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, n);
        plugin.processBlock (chunk, midi);
        if (programChange)
        {
            ++programs;
            plugin.dispatchMidi(); // the plugin's timer: loads the factory preset
            buildMs = waitReady();
            CHECK (plugin.presets().currentIsFactory());
        }
    }
    WARN ("programs " << programs << ", factory presets " << names.size() << report);
    CHECK (programs == names.size());
    CHECK (late == 0);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, out.getMagnitude (ch, 0, total));
    CHECK (std::isfinite (peak));
    CHECK (peak < 1.0f);
    WARN ("length " << juce::String (length, 1) << " s, peak " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS");

    if (wavPath.isNotEmpty())
    {
        const juce::File wav = juce::File::getCurrentWorkingDirectory().getChildFile (wavPath);
        wav.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (wav);
        REQUIRE (stream->openedOk());
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        REQUIRE (writer->writeFromAudioSampleBuffer (out, 0, total));
    }
}
