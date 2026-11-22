Even keyboards: single keys that struck harder, rang shorter or sounded thinner than the keys beside them are evened out, in every instrument family.

![ToneWerk](https://github.com/Michi71/ToneWerk/raw/v0.3.1/docs/images/tonewerk.png)

▶ [Watch the video](https://youtu.be/U0xUk3iMI5s): the first fourteen factory presets one after the other, three minutes (recorded with 0.2.1).

## What is new

This release has no new controls. Every instrument was checked key by key against its neighbours and against its
recordings, and the keys that stood out were fixed.

- **Splendid Grand:** many keys from G4 up sounded thin and short, G4, G5, D6 and G6 most of all. Their recordings have the
  two channels in opposite phase, and the mono mix the voice is built from cancelled the body of the tone and left the
  strike. The voice is rebuilt from a mix that keeps the body. E6 and C♯7 to E7 no longer drop after the strike, and B♭5,
  A6 and D♯7 ring closer to their neighbours.
- **Upright:** D3, D♯3 and E3 struck about 3 dB harder than the keys around them. The Upright plays one recording over
  three keys, and the groups rang for different lengths; the largest steps (around A5 and C6, around F♯7, and the soft C5)
  are halved or gone.
- **Wurlitzer:** D♯5, F♯5 and G♯5 struck harder than their neighbours, and F♯5 could reach full scale. Soft notes in
  the bass now set in as fast as on the recordings (after about 10 ms instead of 20 ms at C2).
- **Mark I and Sparkle Top:** the hard attack of G4 to A4 (Mark I) and of E3 (Sparkle Top) is evened out.
- **Mark II:** the treble from D6 was too soft at the strike and now has the short knock of the action its recordings
  show. Single keys no longer run away from their neighbours above velocity 100, and E1 to G♯1 are less harsh when
  struck hard.
- **Mark V:** the hard attack of B♭4 to G5 and of E3 is evened out; C6 no longer runs away at high velocity.
- **Mark 7:** the attack now hardens with the velocity over the whole keyboard, as on the recordings. Before, it did so
  only on five keys, which stood 3–5 dB above the rest when struck hard.
- **Mark 8:** the hard attack of E6 to G♯6 and of four more keys is evened out.

## What changes for existing projects

Projects and user presets of 0.3.0 and 0.2.x load unchanged. No control and no preset value changes; the instruments
themselves sound different on the keys named above.

- **Splendid Grand:** every key is rebuilt, and the voice is about 1 dB louder.
- **Mark 7:** hard playing is louder; the highest single-note peak at velocity 120 rises by 2 dB.
- **Mark II:** E1 to G♯1 are up to 3 dB quieter at the highest velocities.

## Downloads

| File | Contents |
|---|---|
| `ToneWerk-0.3.1-macOS.zip` | VST3, AU, CLAP, Standalone – universal (Apple Silicon and Intel), macOS 11+ |
| `ToneWerk-0.3.1-Windows.zip` | VST3, CLAP, Standalone (x64) |
| `ToneWerk-0.3.1-Linux.zip` | VST3, CLAP, Standalone (x64) |

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
