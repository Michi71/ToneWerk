# The engine from pmpiano

`engine/src/core` is the core of the pmpiano engine (physical modelling and hybrid voices, GPL-3.0-or-later), taken from
`Michi71/pmpiano` on 2026-10-25 at `0201eccd890a6c8d6fc25689f123810c774fe0ca` (`src/core` without the C API `capi*`). The nine
banks in `banks/` come from the same version (origin and licences: [docs/BANKS.md](../docs/BANKS.md)).

Changes against that version:

- `engine.h`/`engine.cpp`: `alignedAlloc`/`alignedFree` instead of `posix_memalign`/`std::free` for the bank images (Windows
  has no `posix_memalign`). Upstream since pmpiano `70e49e9` (PR #109): a newer pmpiano `src/core` carries it, so the copy
  here then matches pmpiano exactly.

The build settings for the engine (`engine/CMakeLists.txt`: no fast math, `SYSTEM` headers, `-Wno-psabi`,
`_CRT_SECURE_NO_WARNINGS` on Windows) belong to ToneWerk and leave the sources untouched.

To pick up a newer pmpiano version, copy its `src/core` here, re-apply the changes above (unless they are upstream by then) and
record the version here. The engine is built without fast math (`engine/CMakeLists.txt`); `key_voice.cpp` refuses
`-ffast-math`.
