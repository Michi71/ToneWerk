# The engine from pmpiano

`engine/src/core` is the core of the pmpiano engine (physical modelling and hybrid voices, GPL-3.0-or-later), taken from
`Michi71/pmpiano` on 2026-10-25 at `0201eccd890a6c8d6fc25689f123810c774fe0ca` (`src/core` without the C API `capi*`). The nine
banks in `banks/` come from the same version (origin and licences: [docs/BANKS.md](../docs/BANKS.md)).

Changes against pmpiano:

- `engine.h`/`engine.cpp`: `alignedAlloc`/`alignedFree` instead of `posix_memalign`/`std::free` for the bank images (Windows
  has no `posix_memalign`). Upstream on the pmpiano branch `engine-aligned-alloc` (not merged yet).

To pick up a newer pmpiano version, copy its `src/core` here, re-apply the changes above (unless they are upstream by then) and
record the version here. The engine is built without fast math (`engine/CMakeLists.txt`); `key_voice.cpp` refuses
`-ffast-math`.
