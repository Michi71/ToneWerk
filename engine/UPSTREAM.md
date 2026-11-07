# The sound engines

ToneWerk plays the instruments of three engine repositories and keeps the MK8 effect chain of pmpiano. All four are copies;
ToneWerk changes none of their sources and only builds them (`engine/CMakeLists.txt`).

| Folder | Source | Version | Instruments |
|---|---|---|---|
| `tines/src/core` | `Michi71/pmtines` `src/core` (without the C API `capi*`) | `ee19372e4648c77d43a811e39c83a4bf09e6a4e1` (2026-11-04) | Rhodes Mark I, Mark II, Mark V, Mark 7, Mark 8, Sparkle Top: physical models |
| `reeds/src/core` | `Michi71/pmreeds` `src/core` (without `capi*`) | `f9a8ca6eb07e66c69626afc7547a6667e9bf694f` (2026-11-04) | Wurlitzer 200A, Hohner Pianet T: physical models |
| `synthkeys/src/core` | `synthkeys` `src/core` (without `capi*` and the generated `hybrid_*_bank.cpp`) | working tree of 2026-11-07, no commit yet | Hohner Clavinet D6, Splendid Grand, Salamander Grand, Upright Piano KW: hybrid voices |
| `synthkeys/banks` | `synthkeys` `out/bank/{splendid,clav,upright,salamander}.bank` | the images embedded in that working tree (CRC-32 in the `hybrid_*_bank.h` headers: b00748e0, 2ef67e42, 350743e5, 0ec9521e) | the banks of the hybrid voices |
| `fx/src/core` | `Michi71/pmpiano` `src/core`: `mk8_fx.*`, `config.h`, `dsp.h` | `0201eccd890a6c8d6fc25689f123810c774fe0ca` (2026-10-25) | the MK8 preamp and effect chain, the soft clip |

## How they are built

- **One namespace each.** All three engines call themselves `pmp::Engine` with their own `pmp::Params`, `pmp::KeyVoice`, ...
  Each is a static library of its own with the namespace renamed by the preprocessor (`-Dpmp=pmp_tines`, `pmp_reeds`,
  `pmp_synthkeys`), so the copies stay untouched and their symbols cannot meet. The effect chain keeps `pmp`.
- **Adapters.** `adapters/*_engine.cpp` are ToneWerk's: they are compiled into the engine libraries (they see the renamed
  namespace) and offer the engines through `adapters/sound_engine.h`, which names none of them. The plugin includes only that
  header (and `core/mk8_fx.h`, `core/dsp.h` of the effect chain).
- **The synthkeys banks.** synthkeys compiles its banks into the engine as generated sources (`hybrid_*_bank.cpp`, made by
  its `tools/samples/embed_bank.py`). As text they are 30–110 MB each, over GitHub's file limit. ToneWerk keeps the bank images
  (`synthkeys/banks`, 38 MB) and generates the same arrays at build time with `tools/embed_bank.cpp`; the headers
  `hybrid_*_bank.h` are synthkeys' own.
- **Rates.** pmtines and pmreeds run at 44.1 kHz, the rate their presets are calibrated and auto-voiced at; the plugin's
  resampler converts to the host rate. synthkeys runs at the host rate and resamples its banks itself.
- **No fast math**, `SYSTEM` headers, `-Wno-psabi`, `/bigobj` and `_CRT_SECURE_NO_WARNINGS` on Windows; `TONEWERK_FLOAT`
  builds every engine with `PMP_FLOAT`.

## Updating

Copy the engine's `src/core` (without `capi*`; for synthkeys without the generated `hybrid_*_bank.cpp`) over its folder here,
for synthkeys also the bank images from `out/bank/` (check them against the CRC-32 in the headers), and record the version in
the table. A new preset of an engine becomes an instrument by a line in `instruments()` (`source/Parameters.cpp`), at the end
of the list (the index is saved in projects). New source files go into `engine/CMakeLists.txt`.

Licences: GPL-3.0-or-later, every source with its SPDX header. `reeds/src/core/pickup_tables.h` holds the tables of
[openwurli](https://github.com/hal0zer0/openwurli) (GPL-3.0-or-later). The banks are covered by the licences of their
recordings ([docs/INSTRUMENTS.md](../docs/INSTRUMENTS.md)).
