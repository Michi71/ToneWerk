New instruments: twelve electric and acoustic keyboards, physical models and hybrid voices, with the MK8 effect chain, MIDI
learn and a new icon.

![ToneWerk](https://github.com/Michi71/ToneWerk/raw/v0.2.0/docs/images/tonewerk.png)

## Instruments

| Instruments | Voice |
|---|---|
| Rhodes Mark I, Mark II, Mark V, Mark 7, Mark 8, Sparkle Top | physical model: hammer, tine with tonebar, pickup, calibrated key by key |
| Wurlitzer 200A, Hohner Pianet T | physical model: hammer or pad, reed, electrostatic or magnetic pickup |
| Hohner Clavinet D6, Splendid Grand Piano, Salamander Grand Piano, Upright Piano KW | hybrid: a short sampled attack crossfades into 48 computed modes per key |

Nothing has to be installed besides the plugin: the physical models compute everything, and the banks of the hybrid voices
are built in. **The separate bank download of 0.1.0 is no longer needed.**

## What is new

- **Instruments:** all of them are new (see above). The instruments of 0.1.0 are gone.
- **Voicing:** the physical models take the technician's controls (hardness, hammer mass, strike point, tine or reed gauge,
  pickup gap and offset, tonebar, damper); the hybrid voices take hardness, decay, unison and stretch. The panel shows the
  controls of the selected instrument, and the keyboard shows its keys.
- **Voices:** a polyphony limit for slower computers (All, 64 … 8); over it the quietest voice fades out without a click.
  On a current machine every key held with the pedal costs 4–9 % of one core, with 16 voices 2–4 %.
- **MIDI learn:** right-click any control for *MIDI Learn*, *Forget CC* and *Clear all*. The assignments are saved with the
  project and in user presets.
- **Program change:** a MIDI program change loads the factory preset of that number (standalone and AU; VST3 hosts may not
  pass program changes on).
- **Factory presets:** 14 presets for the new instruments.
- **Icon:** ToneWerk has its own icon.
- **Unchanged:** the MK8 effect chain, the reverb, Humanize, the volume limiter and the user presets.

## Upgrading from 0.1.0

Projects and user presets saved with 0.1.0 load, but **the instrument they name is now a different one** (the instrument is
saved by its position in the list, and the list is new). Choose the instrument again; the effects and the reverb keep their
settings.

## Downloads

| File | Contents |
|---|---|
| `ToneWerk-0.2.0-macOS.zip` | VST3, AU, CLAP, Standalone – universal (Apple Silicon and Intel), macOS 11+ |
| `ToneWerk-0.2.0-Windows.zip` | VST3, CLAP, Standalone (x64) |
| `ToneWerk-0.2.0-Linux.zip` | VST3, CLAP, Standalone (x64) |

Each zip contains the licence (`LICENSE.txt`) and the credits of the recordings the hybrid voices are built from
(`CREDITS.md`).

## Installation

Copy the plugin formats to the usual folders: macOS `~/Library/Audio/Plug-Ins/VST3`, `…/Components`, `…/CLAP`; Windows
`C:\Program Files\Common Files\VST3` and `…\CLAP`; Linux `~/.vst3`, `~/.clap`. The standalone app runs from anywhere.

## Known limitations

- The builds are **not signed**. macOS: `xattr -dr com.apple.quarantine <plugin or app>`; Windows SmartScreen asks for
  confirmation.
- Changing a voicing control rebuilds the instrument; sounding notes stop.

## Licence

GPL-3.0-or-later; JUCE under the AGPLv3. The banks of the hybrid voices are covered by the licences of their recordings
(public domain, CC0, CC BY 3.0; `CREDITS.md`).
