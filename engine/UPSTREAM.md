# Engine aus pmpiano

`engine/src/core` ist der Kern der pmpiano-Engine (Physical Modelling und Hybrid-Stimmen, GPL-3.0-or-later),
übernommen aus `Michi71/pmpiano` am 2026-10-25, Stand `0201eccd890a6c8d6fc25689f123810c774fe0ca`
(`src/core` ohne die C-API `capi*`). Die neun Banken in `banks/` stammen aus demselben Stand
(Herkunft und Lizenzen: [docs/BANKS.md](../docs/BANKS.md)).

Änderungen gegenüber pmpiano:

- `engine.h`/`engine.cpp`: `alignedAlloc`/`alignedFree` statt `posix_memalign`/`std::free` für die Bank-Abbilder
  (Windows kennt `posix_memalign` nicht).

Beim Nachziehen eines neueren pmpiano-Stands `src/core` hierher kopieren, die Änderungen oben erneut anwenden
(oder upstream übernehmen) und den Stand hier eintragen. Die Engine baut ohne Fast-Math (`engine/CMakeLists.txt`);
`key_voice.cpp` bricht mit `-ffast-math` ab.
