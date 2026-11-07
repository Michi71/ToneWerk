# ToneWerk – working rules

JUCE plugin (VST3, AU, CLAP, Standalone; macOS, Linux, Windows) with the instruments of three sound engines (Rhodes, reed pianos,
hybrid voices) and an MK8 effect chain, based on the Pamplejuce template. Layout, usage and
design are in the [README](README.md), the instruments in [docs/INSTRUMENTS.md](docs/INSTRUMENTS.md), the engine versions in
[engine/UPSTREAM.md](engine/UPSTREAM.md).

## Language

Michael writes German, and the answers to him are German. Everything in the repository is English: code, comments, commit
messages, README, `docs/`, PR texts and the user interface.

## Workflow

- Work on a branch; open and merge PRs only when asked. Do not rewrite pushed history.
- Build and test before every commit:
  ```sh
  cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build Builds && ctest --test-dir Builds --output-on-failure
  ```
  On the Mac also `auval -v aumu TnWk Mi71`. A picture of the editor: `TONEWERK_SNAPSHOT=/path/ui.png ./Builds/Tests "[.snapshot]"`.
- CI costs minutes (private repository, macOS counts tenfold).

## Decisions that stand

- **Engines:** `engine/{tines,reeds,hybrid,fx}/src/core` are copies of separate, private code bases (Michael keeps them
  private; never name or link them in this repository, in commits or PR texts). Do not change the copies here: make changes in
  their own repositories and copy them in, recording the version in `engine/UPSTREAM.md`. All three engines are `pmp::Engine`; each library renames the namespace
  (`-Dpmp=pmp_tines`, ...) and the plugin reaches them only through `engine/adapters/sound_engine.h`. The hybrid banks are
  kept as images (`engine/hybrid/banks`) and turned into sources at build time (the generated text exceeds GitHub's file
  limit). No fast math (`key_voice.cpp` refuses it); `SharedCode` therefore does not use the template's `-Ofast`.
- **Real time:** nothing is allocated, freed or locked on the audio thread. The loader thread (`PluginProcessor::run`) builds
  instruments; the hand-over goes through `pending_`/`retired_`.
- **Rates:** the physical models run at 44.1 kHz (their calibration rate) and the resampler in `InstrumentVoice` converts to the
  host rate; the hybrid voices run at the host rate (their engine resamples its banks). The effect chain runs in the plugin at the
  host rate; the engines render mono.
- **Instruments:** `instruments()` in `source/Parameters.cpp`; the index is saved in projects, so new ones go at the end.
- **Parameter IDs** (`source/Parameters.h`) are part of saved projects and presets: never rename them; new parameters go at the
  end of `allParameterIds()`. Display names may change.
- **Licence:** GPL-3.0-or-later with an SPDX header in every source file. `engine/hybrid/banks` is not covered by it
  (public domain, CC0, CC BY 3.0: docs/INSTRUMENTS.md).
