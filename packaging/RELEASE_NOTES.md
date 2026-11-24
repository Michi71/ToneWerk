The MK8 effect chain follows the Rhodes V8 Pro more closely: the Envelope now moves the Mid band, the Mid band has the
plugin's shape, and drive, compressor and Envelope respond to every instrument at the same point of its dynamics.

![ToneWerk](https://github.com/Michi71/ToneWerk/raw/v0.4.0/docs/images/tonewerk.png)

▶ [Watch the video](https://youtu.be/U0xUk3iMI5s): the first fourteen factory presets one after the other, three minutes (recorded with 0.2.1).

## What is new

- **Envelope moves the Mid band.** As on the Rhodes V8 Pro, a level follower moves the centre of the Mid band up in
  octaves with the strength of your playing; gain and width of the band stay. At Mid 0 dB the Envelope does nothing:
  for a touch wah turn **Mid** up and **Mid Freq** down. The separate touch wah of earlier versions is gone.
- **The Mid band as on the plugin:** measured again on the Rhodes V8 Pro, it lifts or cuts 1.29 times the setting
  (+15 dB peaks at +19.4 dB) and is narrower (Q 2.85).
- **Drive and compressor engage later** (6 dB): their level reference had not followed a level change of the engine.
- **Every instrument is matched to the chain.** Drive, compressor and Envelope were fitted to one Rhodes; each instrument
  now tells the chain how far it plays from that level (the Mark 8 by 6.9 dB, the grand pianos by 3.7 to 8.2 dB). The
  Mark 8 sweeps the Mid band as far as the plugin's Rhodes.
- **No level burst on preset changes:** loading a preset that switches the effects off while notes sound no longer
  plays the old effects about 10 dB too loud for the length of the fade.

## What changes for existing projects

Projects and user presets of 0.3.x and 0.2.x load; no control, instrument or preset moves, and no parameter changes its
range. They sound different where they use the effect chain:

- Settings with **Envelope** above 0 and Mid at 0 dB no longer get a wah; with Mid gain the band now sweeps as on the
  plugin.
- Settings with **Mid** gain get more of it at the centre (+15 dB is now +19.4 dB) and less on the skirts.
- **Drive** and **compressor** engage 6 dB later, less the instrument's own amount: on the Mark 8 and the Salamander
  Grand about 1 to 2 dB earlier than before, on the other instruments 2 to 5 dB later (less compressed, less
  distorted).

## Downloads

| File | Contents |
|---|---|
| `ToneWerk-0.4.0-macOS.zip` | VST3, AU, CLAP, Standalone – universal (Apple Silicon and Intel), macOS 11+ |
| `ToneWerk-0.4.0-Windows.zip` | VST3, CLAP, Standalone (x64) |
| `ToneWerk-0.4.0-Linux.zip` | VST3, CLAP, Standalone (x64) |

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
