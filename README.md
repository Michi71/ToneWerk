# ToneWerk

![ToneWerk: preset bar, instrument and voicing, the MK8 effect chain and reverb, the keyboard of the instrument](docs/images/tonewerk.png)

Electric and acoustic keyboards as a plugin and standalone app, with the MK8 effect chain of
[pmpiano](https://github.com/Michi71/pmpiano): the Rhodes Mark I, Mark II, Mark V, Mark 7, Mark 8 and Sparkle Top
(physical models of pmtines), the Wurlitzer 200A and the Hohner Pianet T (physical models of pmreeds), and the Hohner
Clavinet D6, the Splendid and Salamander grands and an upright piano (hybrid voices of synthkeys: a sampled attack that turns
into computed modes). The list, the voicing controls and the licences are in [docs/INSTRUMENTS.md](docs/INSTRUMENTS.md). Built with [JUCE](https://juce.com) on the [Pamplejuce](https://github.com/sudara/pamplejuce)
template for **macOS, Linux and Windows** as **VST3, AU (macOS), CLAP and Standalone**.

## Repository layout

| Folder | Contents |
|---|---|
| `engine/` | the engines of pmtines, pmreeds and synthkeys (with the synthkeys banks) and the MK8 effect chain of pmpiano; versions and build in [engine/UPSTREAM.md](engine/UPSTREAM.md) |
| `source/` | the plugin: processor, loader, resampler, presets, user interface |
| `tests/` | Catch2 tests (`Tests`) |
| `JUCE/`, `cmake/`, `modules/clap-juce-extensions/` | submodules (JUCE `develop`, Pamplejuce CMake helpers, CLAP) |

## Building

```bash
git clone --recursive https://github.com/Michi71/ToneWerk.git
cd ToneWerk
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
ctest --test-dir Builds --output-on-failure
```

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler (Xcode/Clang, clang-cl or MSVC, GCC/Clang on Linux) and, on Linux, the
JUCE dependencies (`libasound2-dev libjack-jackd2-dev libx11-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev
libfreetype6-dev libfontconfig1-dev`). The results are in `Builds/ToneWerk_artefacts/Release/`; on the Mac the build copies
VST3, AU and CLAP to `~/Library/Audio/Plug-Ins/`.

CMake options:

- `TONEWERK_FLOAT` (off): the engines compute in single precision (`PMP_FLOAT`).
- `TONEWERK_LTO` (on): link-time optimisation of the plugin binaries. The CI turns it off except for releases.

## Instruments

| Engine | Instruments | Voice |
|---|---|---|
| pmtines | Rhodes Mark I, Mark II, Mark V, Mark 7, Mark 8, Sparkle Top | physical model: hammer, tine with tonebar, pickup; calibrated key by key |
| pmreeds | Wurlitzer 200A, Hohner Pianet T | physical model: hammer or pad, reed, electrostatic or magnetic pickup |
| synthkeys | Hohner Clavinet D6, Splendid Grand Piano, Salamander Grand Piano, Upright Piano KW | hybrid: a short PCM attack crossfades into 48 computed modes per key |

Nothing has to be installed besides the plugin: the physical models compute everything, and the synthkeys banks (38 MB) are
compiled into it. The engines render mono; the effect chain and the reverb make the stereo image.

## Usage

- **Instrument**: the presets of the three engines. Switching builds the new instrument in the background (the physical models
  design and voice their keys, ~0.1–0.5 s); the previous instrument fades out in 30 ms.
- **Volume** (−48 … +12 dB) after the effect chain, followed by a peak limiter (attack 0.1 ms, release 120 ms, no lookahead)
  and the soft clip as the last safeguard: full chords are turned down instead of distorted. Per-instrument trims level the
  instruments at C4 velocity 100 (`instruments()` in `source/Parameters.cpp`, measured with `./Builds/Tests "[.levels]"`).
- **Humanize** (0–100 %, default 50 %): every strike scatters a little, so no two notes are bit for bit the same. The velocity
  is multiplied by a random factor exp(σ g); 50 % is the physical presets' hammer-speed scatter (`human.speed` 0.05) carried
  over to the velocity (σ 0.038 of ln velocity), 100 % twice that. Level and timbre move: at 100 % the level of repeated
  strikes scatters by about 1 dB. Applies to every instrument; the physical models are built without their own speed scatter
  (they keep the scatter of the felt, `human.contact`). At 0 % the hybrid voices repeat a strike bit for bit (at the bank's
  rate).
- **Voicing**: the controls of the instrument's engine on top of its calibration. The physical models take the technician's
  controls (hardness, hammer mass, strike point, tine or reed gauge, pickup gap and offset, the Rhodes' tonebar, damper), the
  hybrid voices hardness, decay, unison and stretch on their poles ([docs/INSTRUMENTS.md](docs/INSTRUMENTS.md)). Every change
  rebuilds the instrument (after 250 ms without further changes); sounding notes stop. A harder or heavier hammer or a thinner
  tine or reed integrates the hammer contact with 16 sub-steps instead of 2: combined extremes ran away otherwise. The settings stay when the instrument
  changes, and extremes carry over: **Reset** sets every control back to the instrument as calibrated.
- **Effects**: the MK8 chain in the units of the Rhodes V8 Pro – preamp (drive, envelope, level), equalizer, Vari-Pan,
  compressor, chorus, phaser, delay. The **FX** switch bypasses the whole chain; like the reverb switch it cross-fades over
  20 ms, so switching while playing does not click.
- **Reverb** at the end, on its own switch (independent of **FX**, so the grands get a room without the MK8 preamp): mix, size,
  decay (RT60 0.3–10 s), damping (how much faster the highs die away), pre-delay (0–200 ms), width. The grand presets have it on;
  Steinway and Grandpa's with less, their recordings carry some room already.
- **Presets**: factory and user presets in one list; **Save** stores the current settings as
  `~/Documents/ToneWerk/Presets/<name>.twpreset` (XML), **Delete** moves a user preset to the trash, `<`/`>` step through the list.
  The host saves the state, including the preset name, with the project.
- **Keyboard**: the keys of the instrument (E1–E7 for the Rhodes and the clavinet, A1–C7 Wurlitzer, F1–E6 Pianet, A0–C8 the
  pianos), playable with the mouse; incoming MIDI notes light up.
- MIDI: note on/off with velocity, CC64 (half pedal; the Pianet has none), CC1 (pickup offset of the physical models), CC7
  (the hybrid voices' volume), CC120/123.

## How it works

- The physical models run at **44.1 kHz**, the rate their presets are calibrated at. A short Kaiser-windowed sinc resampler
  (`source/Resampler.*`, 48 taps) converts to the host rate; at 44.1 kHz it is bypassed. Its only delay is the lookahead of 24
  input samples (0.54 ms); passband flat to ±0.03 dB up to 18 kHz, 83–97 dB signal-to-noise (`tests/ResamplerTests.cpp`). The
  hybrid voices run at the host rate and resample their banks themselves. `./Builds/Tests "[.latency]"` measures the time from note-on to the attack for
  every instrument.
- The screenshot above is rendered by a hidden test: `TONEWERK_SNAPSHOT=$PWD/docs/images/tonewerk.png ./Builds/Tests "[.snapshot]"`.
- The three engines all call themselves `pmp::Engine`; each is built as its own library with the namespace renamed and reached
  through one interface (`engine/adapters/sound_engine.h`, [engine/UPSTREAM.md](engine/UPSTREAM.md)).
- A loader thread builds instruments (designs the keys, prepares the engine) and hands them over through an atomic pointer; the
  audio thread adopts them, fades the old one out and hands it back for deletion. Nothing is allocated or freed on the audio thread.
- The effect chain (`pmp::Mk8Effects`, from pmpiano) runs in the plugin at the host rate; the engines have none.
- The reverb (`source/Reverb.*`) is a feedback delay network: pre-delay, early reflections, four allpass diffusers per channel,
  eight delay lines mixed by a Hadamard matrix, each with a one-pole absorption filter set from the RT60 at low frequencies and
  at Nyquist. Slow modulation of the line lengths (read through a first-order allpass, which keeps the RT60 exact) prevents
  metallic ringing. Measured in `tests/ReverbTests.cpp`: RT60 within 8 % of the setting for every size, stable at the extremes,
  bit-transparent at mix 0; about 0.3 % of one core.
- The engines are built without fast math (their own targets); the template's default `-Ofast` is deliberately not used.

## CI

`.github/workflows/build.yml` builds and tests on macOS (universal), Linux and Windows, validates the VST3 with pluginval
(strictness 10) and uploads one zip per platform. The compiles come from sccache (GitHub's cache); link-time optimisation, whose
links cannot be cached and took most of a build, is on only for a `v*` tag, which creates a release with the three zips. The builds
are not signed: macOS reports them as unidentified (`xattr -dr com.apple.quarantine <plugin>`), Windows SmartScreen likewise.

## Licence

GPL-3.0-or-later like pmpiano, pmtines, pmreeds and synthkeys (`LICENSE`). JUCE is licensed under the AGPLv3 or the JUCE
licence; a distributed binary is therefore subject to the terms of the AGPLv3. The synthkeys banks are covered by the licences
of the free recordings they are built from (public domain, CC0, CC BY 3.0; [docs/INSTRUMENTS.md](docs/INSTRUMENTS.md)).
