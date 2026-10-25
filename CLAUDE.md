# ToneWerk – Arbeitsregeln

JUCE-Plugin (VST3, AU, CLAP, Standalone; macOS, Linux, Windows) mit den Hybrid-Instrumenten der pmpiano-Engine. Grundlage ist
das Template Pamplejuce. Aufbau, Bedienung und Technik stehen im [README](README.md), die Banken in [docs/BANKS.md](docs/BANKS.md),
der Engine-Stand in [engine/UPSTREAM.md](engine/UPSTREAM.md).

## Sprache

Michael schreibt Deutsch, die Antworten an ihn sind Deutsch. Code, Kommentare und Commit-Nachrichten sind Englisch; README,
`docs/`, PR-Texte und die Oberfläche sind Deutsch.

## Arbeitsweise

- Arbeiten auf einem Branch; PR anlegen und mergen nur auf Zuruf. Gepushte History nicht umschreiben.
- Vor jedem Commit bauen und testen:
  ```sh
  cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build Builds && ctest --test-dir Builds --output-on-failure
  ```
  Auf dem Mac zusätzlich `auval -v aumu TnWk Mi71`. Ein Bild der Oberfläche: `TONEWERK_SNAPSHOT=/pfad/ui.png ./Builds/Tests "[.snapshot]"`.
- Die CI kostet Minuten (privates Repository, macOS zählt zehnfach) und holt die Banken nicht (LFS-Kontingent).

## Entscheidungen, die stehen

- **Engine:** `engine/src/core` ist eine Kopie von pmpiano `src/core`; Änderungen dort klein halten und in `engine/UPSTREAM.md`
  eintragen, besser upstream in pmpiano machen und nachziehen. Ohne Fast-Math (`key_voice.cpp` bricht damit ab); `SharedCode`
  baut deshalb ohne das `-Ofast` des Templates.
- **Echtzeit:** auf dem Audiothread nichts anlegen, freigeben oder sperren. Instrumente baut der Ladethread
  (`PluginProcessor::run`), Übergabe über `pending_`/`retired_`.
- **Raten:** die Engine läuft bei der Rate ihrer Bank, der Resampler in `InstrumentVoice` setzt auf die Host-Rate um; die
  Effektkette läuft bei der Host-Rate im Plugin (`fx.on=0` in der Engine).
- **Parameter-IDs** (`source/Parameters.h`) sind Teil der gespeicherten Projekte und Presets: nicht umbenennen; neue Parameter
  ans Ende von `allParameterIds()`.
- **Lizenz:** GPL-3.0-or-later mit SPDX-Kopf in jeder Quelldatei. `banks/` fällt nicht darunter.
