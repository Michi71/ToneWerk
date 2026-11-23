Sparkle Top: the notes around C4 no longer sound slightly distorted.

![ToneWerk](https://github.com/Michi71/ToneWerk/raw/v0.3.2/docs/images/tonewerk.png)

▶ [Watch the video](https://youtu.be/U0xUk3iMI5s): the first fourteen factory presets one after the other, three minutes (recorded with 0.2.1).

## What is new

This release fixes one instrument and has no new controls.

- **Sparkle Top:** C4 and C♯4 carried an off-pitch tone beside the note, between a major third and a fourth above it,
  that rang for about a second and sounded like slight distortion, most of all when struck hard. It comes from the
  tonebar, which rang far longer in the model than on the recordings. It now dies away as fast as there (within about a
  tenth of a second), from C4 up. D4 to B♭5 had the same ring for a shorter time and lose it too.

## What changes for existing projects

Projects and user presets of 0.3.x and 0.2.x load unchanged. No control and no preset value changes; the Sparkle Top
sounds different in the attack of C4 to B♭5, cleaner and with less of a ring after the strike. All other instruments
and the other keys of the Sparkle Top are unchanged.

## Downloads

| File | Contents |
|---|---|
| `ToneWerk-0.3.2-macOS.zip` | VST3, AU, CLAP, Standalone – universal (Apple Silicon and Intel), macOS 11+ |
| `ToneWerk-0.3.2-Windows.zip` | VST3, CLAP, Standalone (x64) |
| `ToneWerk-0.3.2-Linux.zip` | VST3, CLAP, Standalone (x64) |

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
