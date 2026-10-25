# ToneWerk – working rules

JUCE plugin (VST3, AU, CLAP, Standalone; macOS, Linux, Windows) with the hybrid instruments of the pmpiano engine, based on the
Pamplejuce template. Layout, usage and design are in the [README](README.md), the banks in [docs/BANKS.md](docs/BANKS.md), the
engine version in [engine/UPSTREAM.md](engine/UPSTREAM.md).

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
- CI costs minutes (private repository, macOS counts tenfold) and does not fetch the banks (LFS quota).

## Decisions that stand

- **Engine:** `engine/src/core` is a copy of pmpiano `src/core`; keep changes there small and record them in
  `engine/UPSTREAM.md`, preferably make them upstream in pmpiano and pull them in. No fast math (`key_voice.cpp` refuses it);
  `SharedCode` therefore does not use the template's `-Ofast`.
- **Real time:** nothing is allocated, freed or locked on the audio thread. The loader thread (`PluginProcessor::run`) builds
  instruments; the hand-over goes through `pending_`/`retired_`.
- **Rates:** the engine runs at the rate of its bank, the resampler in `InstrumentVoice` converts to the host rate; the effect
  chain runs in the plugin at the host rate (`fx.on=0` in the engine).
- **Parameter IDs** (`source/Parameters.h`) are part of saved projects and presets: never rename them; new parameters go at the
  end of `allParameterIds()`. Display names may change.
- **Licence:** GPL-3.0-or-later with an SPDX header in every source file. `banks/` is not covered by it.
