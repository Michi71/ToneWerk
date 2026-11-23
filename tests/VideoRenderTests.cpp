// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// Plays a MIDI file through the plugin and writes what it sounds and looks like, frame by frame (hidden):
//
//   TONEWERK_VIDEO_MIDI=demo/ToneWerk-Video.mid TONEWERK_VIDEO_DIR=/path/out ./Builds/Tests "[.video]"
//
// writes /path/out/audio.wav (48 kHz, 24 bit), /path/out/f00000.png ... (one picture of the editor per video frame) and
// /path/out/controls.txt (where each control is in the picture). Sound and picture come from the same run, so they are in
// sync by construction. Optional:
//
//   TONEWERK_VIDEO_AUTOMATION  a text file of "seconds parameter_id value" lines (the value in the parameter's own unit):
//                              the controls move in the picture as they act on the sound
//   TONEWERK_VIDEO_FPS         frames per second (30)
//   TONEWERK_VIDEO_SCALE       scale of the editor's picture (1.4: 1540 x 952 from the editor's 1100 x 680)
//
// A program change loads its factory preset as the plugin's timer would. The render is not in real time: where the
// instrument has to be rebuilt it waits for the loader without processing audio, so nothing that still sounds is lost and a
// voicing change takes effect 250 ms after the control came to rest, as it does in a host.
#include "helpers/test_helpers.h"

#include <Parameters.h>
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <PresetManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
struct AutomationPoint
{
    double seconds;
    juce::String id;
    float value;
};

std::vector<AutomationPoint> readAutomation (const juce::File& file)
{
    std::vector<AutomationPoint> points;
    juce::StringArray lines;
    file.readLines (lines);
    for (const auto& line : lines)
    {
        const auto tokens = juce::StringArray::fromTokens (line.upToFirstOccurrenceOf ("#", false, false), " \t", {});
        if (tokens.size() >= 3)
            points.push_back ({ tokens[0].getDoubleValue(), tokens[1], tokens[2].getFloatValue() });
    }
    std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.seconds < b.seconds; });
    return points;
}

template <class Control>
void addControls (juce::Component& c, juce::Component& editor, float scale, juce::String& out)
{
    if (auto* control = dynamic_cast<tw::Learnable<Control>*> (&c); control != nullptr && control->parameterId().isNotEmpty() && c.isVisible())
    {
        const auto r = editor.getLocalArea (&c, c.getLocalBounds()).toFloat() * scale;
        out << control->parameterId() << " " << juce::roundToInt (r.getX()) << " " << juce::roundToInt (r.getY()) << " "
            << juce::roundToInt (r.getWidth()) << " " << juce::roundToInt (r.getHeight()) << "\n";
    }
    for (auto* child : c.getChildren())
        addControls<Control> (*child, editor, scale, out);
}
} // namespace

TEST_CASE ("Render a MIDI file as sound and pictures of the editor", "[.video]")
{
    const auto env = [] (const char* name, const char* fallback = "") {
        return juce::SystemStats::getEnvironmentVariable (name, fallback);
    };
    const auto midiPath = env ("TONEWERK_VIDEO_MIDI");
    const auto dirPath = env ("TONEWERK_VIDEO_DIR");
    if (midiPath.isEmpty() || dirPath.isEmpty())
        SKIP ("TONEWERK_VIDEO_MIDI or TONEWERK_VIDEO_DIR not set");
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File dir = cwd.getChildFile (dirPath);
    REQUIRE (dir.createDirectory().wasOk());
    const int fps = juce::jlimit (1, 120, env ("TONEWERK_VIDEO_FPS", "30").getIntValue());
    const float scale = juce::jlimit (0.25f, 4.0f, env ("TONEWERK_VIDEO_SCALE", "1.4").getFloatValue());

    juce::FileInputStream in (cwd.getChildFile (midiPath));
    REQUIRE (in.openedOk());
    juce::MidiFile file;
    REQUIRE (file.readFrom (in));
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence seq;
    for (int t = 0; t < file.getNumTracks(); ++t)
        seq.addSequence (*file.getTrack (t), 0.0);
    seq.updateMatchedPairs();

    std::vector<AutomationPoint> automation;
    if (const auto path = env ("TONEWERK_VIDEO_AUTOMATION"); path.isNotEmpty())
    {
        REQUIRE (cwd.getChildFile (path).existsAsFile());
        automation = readAutomation (cwd.getChildFile (path));
    }

    constexpr double rate = 48000.0;
    constexpr int block = 200;
    REQUIRE (int (rate) % fps == 0);
    const int perFrame = int (rate) / fps;
    REQUIRE (perFrame % block == 0);

    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (rate, block);
    plugin.prepareToPlay (rate, block);
    plugin.seedHumanize (1);
    auto* messages = juce::MessageManager::getInstance();
    // the loader builds in its own time: wait without processing audio (true when the voice is there to be adopted)
    auto waitBuilt = [&] {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        while (!plugin.instrumentReady() && !plugin.instrumentBuilt() && juce::Time::getMillisecondCounterHiRes() - t0 < 30000.0)
            messages->runDispatchLoopUntil (5);
        return plugin.instrumentReady() || plugin.instrumentBuilt();
    };
    {
        juce::AudioBuffer<float> scratch (2, block);
        juce::MidiBuffer none;
        REQUIRE (waitBuilt());
        plugin.processBlock (scratch, none); // adopts the first instrument
        REQUIRE (plugin.instrumentReady());
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    messages->runDispatchLoopUntil (100);

    const double length = seq.getEndTime() + 3.0;
    const int frames = int (std::ceil (length * fps));
    const int total = frames * perFrame;
    juce::AudioBuffer<float> out (2, total);
    out.clear();

    const int restFrames = int (std::ceil (0.27 * fps)); // a control at rest for longer than the loader's 250 ms
    int next = 0, changedFrame = -1000, rebuilds = 0;
    size_t nextPoint = 0;
    juce::PNGImageFormat png;
    for (int frame = 0; frame < frames; ++frame)
    {
        const double now = double (frame) / fps;
        // the controls first (the message thread, between two blocks)
        for (; nextPoint < automation.size() && automation[nextPoint].seconds <= now + 1e-9; ++nextPoint)
        {
            const auto& point = automation[nextPoint];
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (plugin.state().getParameter (point.id));
            INFO (point.id);
            REQUIRE (param != nullptr);
            const float target = param->convertTo0to1 (point.value);
            if (!juce::approximatelyEqual (param->getValue(), target))
            {
                param->setValueNotifyingHost (target);
                changedFrame = frame;
            }
        }
        if (!plugin.instrumentReady() && !plugin.instrumentBuilt() && frame - changedFrame >= restFrames)
        {
            REQUIRE (waitBuilt());
            ++rebuilds;
        }

        for (int pos = frame * perFrame; pos < (frame + 1) * perFrame; pos += block)
        {
            juce::MidiBuffer midi;
            bool programChange = false;
            for (; next < seq.getNumEvents(); ++next)
            {
                const auto& m = seq.getEventPointer (next)->message;
                const int at = int (std::lround (m.getTimeStamp() * rate)) - pos;
                if (at >= block)
                    break;
                if (m.isMetaEvent())
                    continue;
                midi.addEvent (m, juce::jmax (0, at));
                programChange = programChange || m.isProgramChange();
            }
            juce::AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, block);
            plugin.processBlock (chunk, midi);
            if (programChange)
            {
                plugin.dispatchMidi(); // the plugin's timer: loads the factory preset
                CHECK (plugin.presets().currentIsFactory());
                if (!plugin.instrumentReady())
                {
                    REQUIRE (waitBuilt());
                    ++rebuilds;
                }
                changedFrame = -1000;
            }
        }

        // the editor's timers (keyboard 20 Hz, meter 30 Hz) and the parameter attachments catch up, then the picture
        messages->runDispatchLoopUntil (55);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        if (frame == 0)
        {
            juce::String controls;
            addControls<juce::Slider> (*editor, *editor, scale, controls);
            addControls<juce::ToggleButton> (*editor, *editor, scale, controls);
            addControls<juce::ComboBox> (*editor, *editor, scale, controls);
            controls << "editor 0 0 " << image.getWidth() << " " << image.getHeight() << "\n";
            REQUIRE (dir.getChildFile ("controls.txt").replaceWithText (controls));
        }
        const auto target = dir.getChildFile (juce::String::formatted ("f%05d.png", frame));
        target.deleteFile();
        juce::FileOutputStream stream (target);
        REQUIRE (stream.openedOk());
        REQUIRE (png.writeImageToStream (image, stream));
    }
    plugin.editorBeingDeleted (editor.get());
    editor.reset();

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, out.getMagnitude (ch, 0, total));
    CHECK (std::isfinite (peak));
    CHECK (peak < 1.0f);
    WARN (frames << " frames at " << fps << " fps, " << juce::String (length, 1) << " s, " << rebuilds << " instruments built, peak "
                 << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS");

    const juce::File wav = dir.getChildFile ("audio.wav");
    wav.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (wav);
    REQUIRE (stream->openedOk());
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
    REQUIRE (writer != nullptr);
    stream.release();
    REQUIRE (writer->writeFromAudioSampleBuffer (out, 0, total));
}
