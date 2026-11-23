#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# make_video_midi.py - the score of the feature video: a MIDI file, the control moves and the captions, from one timeline.
#
#   python3 demo/make_video_midi.py demo/ToneWerk-Video.mid
#
# writes ToneWerk-Video.mid (the notes, the pedal and the program changes), ToneWerk-Video.automation.txt ("seconds
# parameter_id value": the controls that move, for the "[.video]" render in tests/VideoRenderTests.cpp) and
# ToneWerk-Video.cues.json (captions, highlighted controls and the sections, for demo/make_video.py).
#
# The sections: the hook on a classic Rhodes preset (soft to hard), three more instruments (the Clavinet also through the
# touch wah), a voicing change while notes ring, the effect chain switched in unit by unit.
import json
import sys

from make_demo_midi import BAR, B, E, PPQ, S, Track

BPM = 92
WAH_DB = 11.0  # how far the output comes down while the touch wah is at full
FPS = 30


def seconds(tick):
    return tick / PPQ * 60.0 / BPM


class Score:
    def __init__(self):
        self.t = Track()
        self.automation = []  # (seconds, id, value)
        self.captions = []    # {start, end, title, text}
        self.highlights = []  # {start, end, id}
        self.sections = []    # {start, name}

    def set(self, tick, param, value, offset=0.0):
        self.automation.append((seconds(tick) + offset, param, value))

    def ramp(self, tick, param, a, b, start, length):
        frames = max(1, round(length * FPS))
        for k in range(frames + 1):
            self.automation.append((seconds(tick) + start + k / FPS, param, a + (b - a) * k / frames))
        self.highlights.append({"start": seconds(tick) + start - 0.3, "end": seconds(tick) + start + length + 0.5, "id": param})

    def caption(self, tick0, tick1, title, text=""):
        self.captions.append({"start": seconds(tick0) + 0.15, "end": seconds(tick1) - 0.15, "title": title, "text": text})

    def highlight(self, tick, param, length=1.6):
        self.highlights.append({"start": seconds(tick) - 0.1, "end": seconds(tick) + length, "id": param})

    def switch(self, tick, name, program, after=2 * B):
        """One bar for the switch: what sounds is let go and dies away for two beats, then the program change loads the
        factory preset (a preset that switches the effects while notes still sound lets them through for a moment)."""
        t = self.t
        t.meta(tick, 0x06, name.encode())
        t.cc(tick, 64, 0)
        t.add(tick, [0xB0, 123, 0], 0)
        t.program(tick + after, program)
        self.sections.append({"start": seconds(tick), "name": name})
        return tick + BAR


# --- the phrases ------------------------------------------------------------------------------------------------------------
def hook(s, t0):  # six bars on the Rhodes, from soft to hard: the tone follows the strike
    t = s.t
    bars = [(45, [55, 59, 60, 64]), (41, [57, 60, 64, 67]), (38, [53, 57, 60, 64]), (43, [53, 57, 59, 64]),
            (36, [52, 55, 59, 62]), (40, [56, 62, 67, 71])]
    lines = [[(76, 2, 1), (74, 3, 0.5), (72, 3.5, 0.5)],
             [(76, 0.5, 1), (79, 2, 1.5)],
             [(77, 0.5, 0.5), (76, 1, 1), (72, 2.5, 1)],
             [(74, 0.5, 1), (76, 2, 0.5), (81, 2.5, 1.5)],
             [(79, 0.5, 1), (76, 2, 0.5), (74, 2.5, 0.5), (71, 3, 1)],
             [(79, 0.5, 1), (76, 2, 1), (74, 3, 0.5), (71, 3.5, 0.5)]]
    for bar, (bass, v) in enumerate(bars):
        b0 = t0 + bar * BAR
        vel = 46 + 8 * bar
        t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
        t.note(b0, bass, B + E, vel + 6)
        t.note(b0 + 2 * B + E, bass, B, vel)
        t.chord(b0, v, B + E, vel, spread=14)
        t.chord(b0 + B + E, v, S, vel - 18)
        t.chord(b0 + 2 * B + E, v, B, vel - 8, spread=8)
        for n, start, dur in lines[bar]:
            t.note(b0 + int(start * B), n, int(dur * B) - 20, vel + 12)
    t.cc(t0 + 6 * BAR, 64, 0)
    return t0 + 6 * BAR


def wurli(s, t0):  # three bars of a soul riff: bass on the beat, chords on the offbeats
    t = s.t
    for bar, root in enumerate([48, 53, 55]):
        b0 = t0 + bar * BAR
        t.note(b0, root - 12, E, 96)
        t.note(b0 + B + E, root - 5, E, 84)
        t.note(b0 + 2 * B, root - 2, E, 88)
        for off in (E, B + E, 2 * B + E, 3 * B + E):
            t.chord(b0 + off, [root + 7, root + 10, root + 16], S + 30, 88)
    for n, start, dur in [(72, 10, 1), (74, 11, 0.5), (75, 11.5, 0.5)]:
        t.note(t0 + int(start * B), n, int(dur * B), 98)
    return t0 + 3 * BAR


def clav(s, t0, shifts, touch=False):  # a sixteenth-note riff, one bar per shift
    """touch: accents hard and ghost notes soft, for the touch wah (the harder the strike, the further the filter opens)."""
    t = s.t
    riff = [(52, 80), (None, 0), (55, 60), (52, 72), (None, 0), (57, 66), (55, 56), (52, 76),
            (None, 0), (50, 64), (52, 74), (None, 0), (55, 68), (None, 0), (57, 62), (58, 76)]
    for bar, shift in enumerate(shifts):
        b0 = t0 + bar * BAR
        for k, (n, v) in enumerate(riff):
            if n is not None:
                t.note(b0 + k * S, n + shift, S - 20, 44 + (v - 56) * 2.7 if touch else v)
        t.chord(b0 + 3 * B + E, [64 + shift, 67 + shift, 70 + shift], S, 104 if touch else 70)
    return t0 + len(shifts) * BAR


def grand(s, t0):  # four bars: two chords that ring out, then single high notes over a quiet bass
    t = s.t
    t.cc(t0, 64, 127)
    for bar, (bass, v) in enumerate([(36, [55, 60, 64, 67, 71]), (33, [57, 60, 64, 67, 72])]):
        b0 = t0 + bar * BAR
        if bar:
            t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
        t.note(b0, bass, BAR - 30, 80)
        t.note(b0, bass + 12, BAR - 30, 72)
        t.chord(b0 + E, v, BAR - E - 30, 84, spread=50)
    b0 = t0 + 2 * BAR
    t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
    t.note(b0, 29, 2 * BAR - 30, 62)
    t.chord(b0, [53, 57, 60], 2 * BAR - 30, 56, spread=40)
    for n, start, vel in [(84, 1, 80), (88, 2.5, 74), (91, 4, 72), (96, 6, 66)]:
        t.note(b0 + int(start * B), n, 2 * B, vel)
    return t0 + 4 * BAR


def voicing(s, t0):  # four bars: a phrase and a chord, the controls move while the chord rings, the same again
    t = s.t
    phrase = [64, 67, 69, 72]
    chord = [41, 48, 57, 60, 64, 67]
    t.cc(t0, 64, 127)
    for k, n in enumerate(phrase):
        t.note(t0 + k * E, n, E - 20, 96)
    t.chord(t0 + 2 * B, chord, 2 * BAR, 92, spread=20)
    # second bar: the hammer hardens, the pickup moves off the rest position; the chord keeps the voicing it was struck with
    s.ramp(t0 + BAR, "voicing_hardness", 0, 120, 0.15, 1.0)
    s.ramp(t0 + BAR, "voicing_pickup_offset", 0, 0.4, 1.25, 0.8)
    b0 = t0 + 2 * BAR
    for k, n in enumerate(phrase):
        t.note(b0 + k * E, n, E - 20, 96)
    for k, n in enumerate(phrase):
        t.note(b0 + 2 * B + k * E, n + 12, E - 20, 96)
    b0 = t0 + 3 * BAR
    t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
    t.chord(b0, chord, BAR - 60, 92, spread=20)
    t.cc(t0 + 4 * BAR - 30, 64, 0)
    return t0 + 4 * BAR


def effects(s, t0):  # seven bars: the same figure while the units come in
    t = s.t
    bars = [(38, [53, 57, 60, 64]), (43, [53, 57, 59, 64]), (36, [52, 55, 59, 62]), (45, [55, 59, 61, 65]),
            (38, [53, 57, 60, 64]), (43, [53, 57, 59, 64])]
    for bar, (bass, v) in enumerate(bars):
        b0 = t0 + bar * BAR
        t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
        t.note(b0, bass, BAR - E, 84)
        t.chord(b0, v, 2 * B, 78, spread=12)
        for k, n in enumerate([v[1] + 12, v[2] + 12, v[3] + 12, v[2] + 12]):
            t.note(b0 + B + k * E, n, E + 40, 80 - 4 * k)
        t.chord(b0 + 3 * B + E, v, E, 66, spread=8)
    b0 = t0 + 6 * BAR
    t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
    t.note(b0, 36, 2 * BAR, 84)
    t.chord(b0, [52, 55, 59, 62, 67], 2 * BAR, 80, spread=40)
    t.note(b0 + 2 * B, 79, BAR, 78)
    t.cc(b0 + 2 * BAR, 64, 0)
    return t0 + 7 * BAR


def main(path):
    s = Score()
    t = s.t
    tempo = 60_000_000 // BPM
    t.meta(0, 0x51, tempo.to_bytes(3, "big"))
    t.meta(0, 0x58, bytes([4, 2, 24, 8]))
    t.meta(0, 0x03, b"ToneWerk video")

    # the hook: the editor flies in during the first bar
    a = s.switch(0, "Just The 2 Of Us", 29, after=10)
    b = hook(s, a)
    s.caption(a, a + 3 * BAR, "Rhodes Mark I", "A physical model: tine, tonebar, hammer and pickup")
    s.caption(a + 3 * BAR, b, "From soft to hard", "The tone follows every strike")

    a = s.switch(b, "Wurlitzer 200A", 7)
    b = wurli(s, a)
    s.caption(a - 2 * B, b, "Wurlitzer 200A", "Reed piano, a physical model too")

    a = s.switch(b, "Clavinet Funk", 10)
    w = clav(s, a, [-7, -2])  # a fifth below the riff as written: the low register of the instrument
    s.caption(a - 2 * B, w, "Hohner Clavinet D6", "Through the MK8 preamp and compressor")
    # the preamp's envelope goes to full during the last beat: the same riff through the touch wah
    s.ramp(w, "fx_envelope", 0, 100, -0.62, 0.5)
    for k in range(16):  # the wah at full is much louder: the output comes down with it, below the limiter
        s.set(w, "volume", -4 - WAH_DB * k / 15, offset=-0.62 + k / FPS)
    b = clav(s, w, [-7, -2, -7], touch=True)
    s.caption(w, b, "Touch wah", "Envelope at full: the harder you play, the further the filter opens")

    a = s.switch(b, "Splendid Grand", 11)
    b = grand(s, a)
    s.caption(a - 2 * B, b, "Splendid Grand Piano", "Hybrid: a sampled attack turns into 48 computed modes per key")

    a = s.switch(b, "Mark I Stage", 0)
    b = voicing(s, a)
    s.caption(a - 2 * B, a + BAR, "Voicing while you play", "Hammer hardness and pickup offset")
    s.caption(a + BAR, b, "Seamless voicing", "Ringing notes keep their character, new notes get the new settings")

    a = s.switch(b, "Mark I Stage", 0)
    # everything off first (after the preset has loaded) and 3 dB down as in the effect presets, then one unit per bar
    for param, value in [("fx_on", 0), ("pan_on", 0), ("chorus_on", 0), ("comp_on", 0), ("phaser_on", 0), ("delay_on", 0), ("reverb_on", 0),
                         ("pan_rate", 3.5), ("pan_depth", 70), ("reverb_mix", 30), ("reverb_size", 65), ("reverb_decay", 2.4), ("volume", -3)]:
        s.set(a - BAR + 2 * B, param, value, offset=0.2)
    b = effects(s, a)
    s.caption(a - 2 * B, a + BAR, "MK8 effect chain", "Dry first")
    steps = [(1, ["fx_on", "pan_on"], "Vari-Pan", "Moves the sound between left and right"),
             (2, ["chorus_on"], "+ Chorus", ""),
             (3, ["phaser_on"], "+ Phaser", ""),
             (4, ["reverb_on"], "+ Reverb", "A feedback delay network")]
    for k, (bar, params, title, text) in enumerate(steps):
        for param in params:
            s.set(a + bar * BAR, param, 1)
            s.highlight(a + bar * BAR, param)
        until = a + (steps[k + 1][0] if k + 1 < len(steps) else bar + 1) * BAR
        s.caption(a + bar * BAR, until, title, text)
    s.caption(a + 5 * BAR, a + 6 * BAR, "Light on the CPU",
              "Every key held with the pedal: 4-9 % of one core (Apple M4 Max, 48 kHz)")
    end = b + BAR
    s.sections.append({"start": seconds(a + 6 * BAR), "name": "End"})
    t.meta(end, 0x06, b"End")

    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + PPQ.to_bytes(2, "big")
    with open(path, "wb") as f:
        f.write(header + t.encode())
    stem = path[:-4] if path.endswith(".mid") else path
    with open(stem + ".automation.txt", "w") as f:
        f.write("# seconds parameter_id value (demo/make_video_midi.py)\n")
        for sec, param, value in sorted(s.automation, key=lambda p: p[0]):
            f.write(f"{sec:.4f} {param} {value:.4f}\n")
    with open(stem + ".cues.json", "w") as f:
        json.dump({"fps": FPS, "bpm": BPM, "length": seconds(end) + 3.0, "end_card": seconds(a + 6 * BAR) + 1.0,
                   "captions": s.captions, "highlights": s.highlights, "sections": s.sections}, f, indent=1)
        f.write("\n")
    print(f"{path}: {seconds(end):.1f} s at {BPM} bpm, {len(s.automation)} control points, {len(s.captions)} captions")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ToneWerk-Video.mid")
