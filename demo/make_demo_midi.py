#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# make_demo_midi.py - a MIDI file that walks through ToneWerk's factory presets (program change 0..13) with a short phrase each,
# for a video of the editor. About three minutes at 108 bpm: per preset one bar for the switch, four bars of music, one bar
# to ring out. Markers name the presets (a DAW shows them in its timeline).
#
#   python3 demo/make_demo_midi.py demo/ToneWerk-Demo.mid
import struct
import sys

PPQ = 480
BPM = 108
BAR = 4 * PPQ


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


class Track:
    def __init__(self):
        self.events = []  # (tick, order, bytes)

    def add(self, tick, data, order=1):
        self.events.append((int(tick), order, bytes(data)))

    def meta(self, tick, kind, payload, order=0):
        self.add(tick, bytes([0xFF, kind]) + vlq(len(payload)) + payload, order)

    def note(self, tick, note, length, vel):
        assert 21 <= note <= 108, note
        self.add(tick, [0x90, note, max(1, min(127, int(vel)))], 2)
        self.add(tick + length, [0x80, note, 64], 0)  # note-offs first at a shared tick

    def chord(self, tick, notes, length, vel, spread=0):
        for i, n in enumerate(notes):
            self.note(tick + i * spread, n, length, vel - 4 * i)

    def cc(self, tick, num, val):
        self.add(tick, [0xB0, num, val], 1)

    def program(self, tick, prog):
        self.add(tick, [0xC0, prog], 1)

    def encode(self):
        out, last = b"", 0
        for tick, _, data in sorted(self.events, key=lambda e: (e[0], e[1])):
            out += vlq(tick - last) + data
            last = tick
        out += vlq(0) + b"\xFF\x2F\x00"
        return b"MTrk" + struct.pack(">I", len(out)) + out


B = PPQ  # one beat
E = PPQ // 2  # an eighth
S = PPQ // 4  # a sixteenth


# --- the phrases: t0 = start of the four bars of music --------------------------------------------------------------------
def rhodes_251(t, t0):  # ii-V-I in F, rootless voicings, a little melody
    t.cc(t0, 64, 127)
    for bar, (bass, voicing) in enumerate([(43, [60, 64, 65, 69]), (36, [58, 62, 64, 69]), (41, [57, 60, 64, 67]), (41, [57, 60, 64, 67])]):
        b0 = t0 + bar * BAR
        t.note(b0, bass, BAR - E, 84)
        t.chord(b0 + E, voicing, B + E, 78, spread=12)
        t.chord(b0 + 2 * B + E, voicing, B, 64, spread=8)
        if bar == 1:
            t.cc(b0, 64, 0); t.cc(b0 + 10, 64, 127)
    mel = [(72, 0, 1), (74, 1, 1), (76, 2, 2), (77, 4, 1.5), (76, 5.5, 0.5), (74, 6, 2), (72, 8, 3), (69, 12, 4)]
    for n, start, dur in mel:
        t.note(t0 + int(start * B), n, int(dur * B) - 20, 92)
    t.cc(t0 + 4 * BAR, 64, 0)


def sustained_pan(t, t0):  # long chords: the Vari-Pan moves them across the stereo field
    for bar, notes in enumerate([[50, 57, 60, 64, 69], [43, 53, 57, 59, 64], [48, 55, 59, 62, 64], [41, 57, 60, 64, 67]]):
        t.chord(t0 + bar * BAR, notes, BAR - 30, 76, spread=30)


def neo_soul(t, t0):  # Ebmaj9 - Dm7 - Cm9 - Bb6/9, offbeat pushes
    chords = [(39, [58, 62, 65, 67]), (38, [57, 60, 64, 65]), (36, [55, 58, 62, 63]), (34, [55, 57, 60, 62])]
    for bar, (bass, v) in enumerate(chords):
        b0 = t0 + bar * BAR
        t.note(b0, bass, 2 * B, 88)
        t.chord(b0, v, B + E, 80, spread=10)
        t.chord(b0 + 2 * B + E, v, B, 70, spread=10)
        t.note(b0 + 3 * B + E, bass + 12, E, 76)


def ballad(t, t0, top=True):  # a slow ballad with the pedal and a top line
    t.cc(t0, 64, 127)
    seq = [(36, [52, 55, 60, 64]), (33, [52, 57, 60, 64]), (41, [53, 57, 60, 65]), (43, [53, 55, 59, 62])]
    for bar, (bass, v) in enumerate(seq):
        b0 = t0 + bar * BAR
        t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
        t.note(b0, bass, BAR - 40, 80)
        t.note(b0, bass + 12, BAR - 40, 70)
        t.chord(b0 + B, v, 3 * B - 40, 66, spread=40)
        if top:
            for k, n in enumerate([v[-1] + 3, v[-1] + 5, v[-1] + 7]):
                t.note(b0 + int((k * 1.0 + 1) * B), n, B - 30, 88 - 6 * k)
    t.cc(t0 + 4 * BAR, 64, 0)


def arpeggios(t, t0):  # rolling eighths under a chorus
    for bar, notes in enumerate([[45, 52, 57, 60, 64, 67], [41, 48, 53, 57, 60, 64], [43, 50, 55, 59, 62, 67], [40, 47, 52, 55, 59, 64]]):
        b0 = t0 + bar * BAR
        pattern = notes + notes[-2:0:-1]
        for k in range(8):
            t.note(b0 + k * E, pattern[k % len(pattern)], E + 60, 70 + (12 if k % 4 == 0 else 0))


def stabs(t, t0):  # funky stabs on the MK8 chain
    v = [[53, 57, 60, 64], [51, 55, 58, 62]]
    for bar in range(4):
        b0 = t0 + bar * BAR
        chord = v[bar % 2]
        t.note(b0, 36 + (bar % 2) * 3, B, 84)
        for pos, length, vel in [(0, S, 84), (E + S, S, 70), (B + E, E, 80), (2 * B + S, S, 66), (3 * B, E, 86), (3 * B + E + S, S, 74)]:
            t.chord(b0 + pos, chord, length, vel)


def sparkle(t, t0):  # bell-like line high up
    line = [79, 76, 72, 74, 76, 79, 81, 79, 76, 74, 72, 69, 72, 74, 76, 72]
    for k, n in enumerate(line):
        t.note(t0 + k * B, n, B + E, 84 + (10 if k % 4 == 0 else 0))
    for bar, v in enumerate([[60, 64, 67], [57, 60, 64], [53, 57, 60], [55, 59, 62]]):
        t.chord(t0 + bar * BAR, v, BAR - 30, 60)


def soul_riff(t, t0):  # a Wurlitzer soul riff: bass on the beat, chords on the offbeats
    for bar in range(4):
        b0 = t0 + bar * BAR
        root = [48, 48, 53, 55][bar]
        third = 4 if bar != 2 else 4
        t.note(b0, root - 12, E, 96)
        t.note(b0 + B + E, root - 12 + 7, E, 84)
        t.note(b0 + 2 * B, root - 12 + 10, E, 88)
        for off in (E, B + E, 2 * B + E, 3 * B + E):
            t.chord(b0 + off, [root + 7, root + 10, root + 12 + third], S + 30, 88)
    for n, s, d in [(72, 14, 1), (74, 15, 0.5), (75, 15.5, 0.5)]:
        t.note(t0 + int(s * B), n, int(d * B), 96)


def pads(t, t0):  # sustained voicings under the chorus
    for bar, v in enumerate([[48, 55, 62, 64, 67], [45, 52, 59, 60, 64], [41, 48, 55, 57, 64], [43, 50, 57, 59, 65]]):
        t.chord(t0 + bar * BAR, v, BAR - 30, 72, spread=20)
        t.note(t0 + bar * BAR + 2 * B, v[-1] + 5, 2 * B - 30, 80)


def pop_eighths(t, t0):  # the Pianet: straight eighth chords, no pedal (it has none)
    for bar, (bass, v) in enumerate([(40, [52, 56, 59]), (37, [52, 56, 61]), (45, [57, 61, 64]), (47, [54, 59, 63])]):
        b0 = t0 + bar * BAR
        t.note(b0, bass, B, 90)
        t.note(b0 + 2 * B, bass + 7, B, 82)
        for k in range(8):
            t.chord(b0 + k * E, v, E - 40, 82 if k % 2 == 0 else 66)


def clav_funk(t, t0):  # sixteenth-note clavinet riff
    riff = [(52, 80), (None, 0), (55, 60), (52, 72), (None, 0), (57, 66), (55, 56), (52, 76),
            (None, 0), (50, 64), (52, 74), (None, 0), (55, 68), (None, 0), (57, 62), (58, 76)]
    for bar in range(4):
        b0 = t0 + bar * BAR
        shift = [0, 0, 5, 3][bar]
        for k, (n, v) in enumerate(riff):
            if n is not None:
                t.note(b0 + k * S, n + shift, S - 20, v)
        t.chord(b0 + 3 * B + E, [64 + shift, 67 + shift, 70 + shift], S, 70)


def alberti(t, t0):  # the Splendid: an Alberti bass under a classical line
    left = [[48, 55, 52, 55], [47, 55, 50, 55], [45, 52, 48, 52], [43, 50, 47, 50]]
    for bar, fig in enumerate(left):
        for k in range(8):
            t.note(t0 + bar * BAR + k * E, fig[k % 4], E - 20, 76 if k % 4 else 86)
    line = [(72, 0, 2), (76, 2, 1), (79, 3, 1), (77, 4, 1.5), (76, 5.5, 0.5), (74, 6, 2), (72, 8, 1), (74, 9, 1),
            (76, 10, 1), (77, 11, 1), (79, 12, 2), (74, 14, 1), (71, 15, 1)]
    for n, s, d in line:
        t.note(t0 + int(s * B), n, int(d * B) - 30, 100)


def grand_swell(t, t0):  # the Salamander: wide voicings with the pedal, a crescendo
    t.cc(t0, 64, 127)
    seq = [(33, [57, 64, 69, 72, 76]), (29, [57, 60, 65, 69, 72]), (36, [55, 60, 64, 67, 72]), (31, [55, 59, 62, 67, 71])]
    for bar, (bass, v) in enumerate(seq):
        b0 = t0 + bar * BAR
        t.cc(b0, 64, 0); t.cc(b0 + 15, 64, 127)
        t.note(b0, bass, BAR, 78 + 8 * bar)
        t.note(b0, bass + 12, BAR, 72 + 8 * bar)
        t.chord(b0 + E, v, 3 * B, 72 + 10 * bar, spread=60)
        t.note(b0 + 2 * B + E, v[-1] + 2, B, 90 + 8 * bar)
    t.cc(t0 + 4 * BAR + 2 * B, 64, 0)


def upright_tune(t, t0):  # the upright: a simple stride-like tune
    for bar, (root, v) in enumerate([(36, [52, 55, 60]), (41, [53, 57, 60]), (43, [53, 55, 59]), (36, [52, 55, 60])]):
        b0 = t0 + bar * BAR
        for beat in range(4):
            if beat % 2 == 0:
                t.note(b0 + beat * B, root - (0 if beat == 0 else 5), E + 40, 96)
            else:
                t.chord(b0 + beat * B, v, E + 20, 84)
    tune = [(72, 0, 1), (72, 1, 0.5), (74, 1.5, 0.5), (76, 2, 1), (72, 3, 1), (77, 4, 1.5), (76, 5.5, 0.5), (74, 6, 2),
            (74, 8, 1), (76, 9, 0.5), (77, 9.5, 0.5), (79, 10, 1), (74, 11, 1), (76, 12, 1), (74, 13, 1), (72, 14, 2)]
    for n, s, d in tune:
        t.note(t0 + int(s * B), n, int(d * B) - 30, 104)


# --- the walk through the presets (program = the factory preset's place in the list) ---------------------------------------
PRESETS = [
    ("Mark I Stage", rhodes_251),
    ("Mark I Vari-Pan", sustained_pan),
    ("Mark II Phaser", neo_soul),
    ("Mark V", ballad),
    ("Mark 7 Chorus", arpeggios),
    ("Mark 8 (MK8 chain)", stabs),
    ("Sparkle Top", sparkle),
    ("Wurlitzer 200A", soul_riff),
    ("Wurlitzer Chorus", pads),
    ("Pianet T", pop_eighths),
    ("Clavinet Funk", clav_funk),
    ("Splendid Grand", alberti),
    ("Salamander Grand", grand_swell),
    ("Upright Piano", upright_tune),
]
LEAD_BARS = 1   # the switch: program change, then silence while the instrument is built
MUSIC_BARS = 4
TAIL_BARS = 1   # the last notes ring out before the next switch


def main(path):
    t = Track()
    tempo = 60_000_000 // BPM
    t.meta(0, 0x51, tempo.to_bytes(3, "big"))
    t.meta(0, 0x58, bytes([4, 2, 24, 8]))
    t.meta(0, 0x03, b"ToneWerk demo")
    segment = (LEAD_BARS + MUSIC_BARS + TAIL_BARS) * BAR
    for i, (name, phrase) in enumerate(PRESETS):
        s0 = i * segment
        t.meta(s0, 0x06, name.encode())          # marker
        t.cc(s0, 64, 0)
        t.add(s0, [0xB0, 123, 0], 0)             # all notes off before the switch
        t.program(s0 + 10, i)
        phrase(t, s0 + LEAD_BARS * BAR)
    end = len(PRESETS) * segment
    t.cc(end, 64, 0)
    t.meta(end + BAR, 0x06, b"End")
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
    with open(path, "wb") as f:
        f.write(header + t.encode())
    seconds = (end + BAR) / PPQ * 60.0 / BPM
    print(f"{path}: {len(PRESETS)} presets, {seconds / 60:.0f}:{seconds % 60:04.1f} min at {BPM} bpm")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ToneWerk-Demo.mid")
