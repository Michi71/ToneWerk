Playing and fitting: voicing changes no longer cut notes off, the plugin adapts to your keyboard and your band's pitch, and there are fifteen more presets.

![ToneWerk](https://github.com/Michi71/ToneWerk/raw/v0.3.0/docs/images/tonewerk.png)

▶ [Watch the video](https://youtu.be/U0xUk3iMI5s): the first fourteen factory presets one after the other, three minutes (recorded with 0.2.1).

## What is new

- **Voicing without a cut:** turning a voicing control (or changing **Voices**) no longer stops what sounds. The notes ring
  out with the voicing they were struck with, pedal and key releases included, while every new strike plays the new one.
- **Velocity:** a new control next to Humanize bends the velocity curve (−100 … +100 %), to fit the plugin to your keyboard:
  positive values make a light touch play louder, negative ones softer.
- **Transpose and Tune**, in the header: the keys shifted by up to an octave, and the pitch of the whole instrument by up to
  ±100 cents (A = 432 Hz is −31.8 cents).
- **Level meter:** the output peak of each channel under the instrument's name, with the limiter's threshold marked;
  **LIM** lights while the limiter holds a peak back.
- **Fifteen more factory presets** that use the voicing and the new controls: Mark I Bark, Mark I Ballad, Mark II Suitcase
  Tremolo, Mark V Glass, Sparkle Top Dream, Wurlitzer Overdrive, Wurlitzer Tremolo, Pianet Echo, Clavinet Octave Bass,
  Clavinet Phaser, Splendid Grand Intimate, Salamander Concert Hall, Salamander A=432, Honky-Tonk Upright, Felt Upright.
  They follow the old ones in the list, so the MIDI programs 0–13 are what they were.
- **Tooltips** say what each control does, the voicing controls in the words of the loaded instrument.
- **Effects on a mono output:** the FX switch did nothing on a mono track. The chain now runs there too (the Vari-Pan
  becomes a tremolo).
- **Latency reported to the host:** about 0.58 ms, the same for every instrument, so recorded notes line up.

## What changes for existing projects

Projects and user presets of 0.2.x load unchanged; the new controls start at their neutral values.

- **Five factory presets are quieter:** Mark I Vari-Pan, Mark II Phaser, Mark 7 Chorus, Mark 8 and Wurlitzer Chorus are
  2–3.5 dB down, because a chord at a moderate velocity already had the limiter working. A project keeps the level it was
  saved with; only loading the preset anew gives the lower one.
- **Timing:** the hybrid voices (Clavinet, the pianos), and the Rhodes and reed pianos at 44.1 kHz, answer about 0.58 ms
  later than before, so that every instrument has the latency the host is told. In a project the host compensates for it.

## Downloads

| File | Contents |
|---|---|
| `ToneWerk-0.3.0-macOS.zip` | VST3, AU, CLAP, Standalone – universal (Apple Silicon and Intel), macOS 11+ |
| `ToneWerk-0.3.0-Windows.zip` | VST3, CLAP, Standalone (x64) |
| `ToneWerk-0.3.0-Linux.zip` | VST3, CLAP, Standalone (x64) |

Each zip contains the licence (`LICENSE.txt`) and the credits of the recordings the hybrid voices are built from
(`CREDITS.md`).

## Installation

Copy the plugin formats to the usual folders: macOS `~/Library/Audio/Plug-Ins/VST3`, `…/Components`, `…/CLAP`; Windows
`C:\Program Files\Common Files\VST3` and `…\CLAP`; Linux `~/.vst3`, `~/.clap`. The standalone app runs from anywhere.

## Known limitations

- The builds are **not signed**. macOS: `xattr -dr com.apple.quarantine <plugin or app>`; Windows SmartScreen asks for
  confirmation.
- While notes ring out after a voicing change, two instruments compute. A further change in that time fades the ringing
  notes out, and switching the instrument fades as before.
- A change of **Tune** rebuilds the instrument like a voicing control, and the decay times change with the pitch (by 6 %
  at most).

## Licence

GPL-3.0-or-later; JUCE under the AGPLv3. The banks of the hybrid voices are covered by the licences of their recordings
(public domain, CC0, CC BY 3.0; `CREDITS.md`).
