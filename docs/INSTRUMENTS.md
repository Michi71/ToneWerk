# The instruments

ToneWerk plays the presets of three sound engines (versions and build: [engine/UPSTREAM.md](../engine/UPSTREAM.md)). The index in the
instrument choice is saved in projects and presets; new instruments go at the end of `instruments()` in
`source/Parameters.cpp`.

| # | Instrument | Engine preset | Engine | Voice | Keys | Rate |
|---|---|---|---|---|---|---|
| 0 | Rhodes Mark I | `mark1` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 1 | Rhodes Mark II | `mark2` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 2 | Rhodes Mark V | `mark5` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 3 | Rhodes Mark 7 | `mark7` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 4 | Rhodes Mark 8 | `mark8` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 5 | Rhodes Sparkle Top | `sparkletop` | Rhodes | physical model | E1–E7 | 44.1 kHz |
| 6 | Wurlitzer 200A | `wurli` | reed pianos | physical model | A1–C7 | 44.1 kHz |
| 7 | Hohner Pianet T | `pianet` | reed pianos | physical model | F1–E6 | 44.1 kHz |
| 8 | Hohner Clavinet D6 | `clavinet` | hybrid | hybrid | E1–E7 | host rate (bank 44.1 kHz) |
| 9 | Splendid Grand Piano | `splendid_hybrid` | hybrid | hybrid | A0–C8 | host rate (bank 22.05 kHz) |
| 10 | Salamander Grand Piano | `salamander` | hybrid | hybrid | A0–C8 | host rate (bank 44.1 kHz) |
| 11 | Upright Piano KW | `upright` | hybrid | hybrid | A0–C8 | host rate (bank 22.05 kHz) |

- **Physical models** (the Rhodes and reed engines): every key is a physical system of hammer (or the Pianet's pad), tine or reed and
  pickup; no samples. The presets are calibrated key by key against recordings at 44.1 kHz. The Pianet has no sustain pedal (the
  pad is the damper); CC1 moves the pickups live (the "bark").
- **Hybrid voices** (the hybrid engine): a short PCM attack from the bank crossfades into 48 computed modes per key; decay, damper,
  half pedal and the sympathetic strings (the grands and the upright) are computed. The banks are compiled into the plugin; no
  bank files are needed at run time.

## Voicing

The controls act on top of the calibrated preset; all at 0 is the preset as calibrated. The panel shows the controls of the
instrument's engine.

| Control | Parameter | Rhodes | Wurlitzer, Pianet | Hybrid voices |
|---|---|---|---|---|
| Hardness | `voicing_hardness` | hammer tip, +100 % = half the contact time | felt, or the pad's tear time | timbre of 32 velocity steps harder per +100 % |
| Mass | `voicing_hammer_mass` | hammer weight | hammer weight, or the pad's adhesion | – |
| Strike Point | `voicing_strike_point` | contact point on the tine | contact point on the reed | – |
| Tine/Reed Gauge | `voicing_gauge` | tine diameter (re-tuned) | reed thickness (re-tuned) | – |
| Pickup Gap | `voicing_pickup_gap` | field width, wider = softer | slot or field width | – |
| Pickup Offset | `voicing_pickup_offset` | offset x₀/w added to the preset | the same | – |
| Tonebar | `voicing_tonebar_detune` | tonebar tuning [cent] | – | – |
| Damper | `voicing_damper_release` | decay time with the damper on | the same | – |
| Decay | `voicing_decay` | – | – | T60 of every mode × 2^(decay/100) |
| Unison | `voicing_unison` | – | – | spread of the modes of one partial |
| Stretch | `voicing_stretch` | – | – | stretch about C4 [cent/octave] |

## Licences

The code of the engines is GPL-3.0-or-later; `engine/reeds/src/core/pickup_tables.h` holds the tables of
[openwurli](https://github.com/hal0zer0/openwurli) (GPL-3.0-or-later). The physical models carry no recordings; they were
calibrated against recordings that stay outside the repositories.

The banks of the hybrid voices (`engine/hybrid/banks`) are built from free recordings and are covered by their licences, not by the GPL:

| Bank | Recording | Licence |
|---|---|---|
| `splendid.bank` | Splendid Grand Piano (AKAI, SFZ mapping by kinwie, [sfzinstruments](https://sfzinstruments.github.io/pianos/splendid_grand_piano)) | public domain |
| `clav.bank` | "Lith's Realistic Clavinet" by Lithalean/Mike77154 (musical-artifacts) | CC BY 3.0 |
| `upright.bank` | "Upright Piano KW" by Gonzalo/Roberto for [FreePats](http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html#UprightKW) | CC0 |
| `salamander.bank` | Salamander Grand Piano V3 by Alexander Holm ([FreePats](https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html)) | CC BY 3.0 |

A distributed binary contains these banks; the CC BY ones require the attribution above to go with it.
