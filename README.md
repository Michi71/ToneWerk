# ToneWerk

Die Hybrid-Instrumente von [pmpiano](https://github.com/Michi71/pmpiano) als Plugin und Standalone-Programm: Rhodes Mark I/II,
Suitcase, MK8 (physikalisches Modell), Wurlitzer 200A, Clavinet D6, Pianet T, die Flügel (SGP, Steinway, Grandpa's) und die
Yamaha CP80, dazu die MK8-Effektkette. Gebaut mit [JUCE](https://juce.com) auf dem Template
[Pamplejuce](https://github.com/sudara/pamplejuce) für **macOS, Linux und Windows** als **VST3, AU (macOS), CLAP und Standalone**.

## Aufbau des Repositorys

| Ordner | Inhalt |
|---|---|
| `engine/` | der Engine-Kern aus pmpiano (Physical Modelling und Hybrid-Stimmen), Stand und Änderungen in [engine/UPSTREAM.md](engine/UPSTREAM.md) |
| `banks/` | die neun Hybrid-Banken (**Git LFS**, ~160 MB), Herkunft und Lizenzen in [docs/BANKS.md](docs/BANKS.md) |
| `source/` | das Plugin: Processor, Lader, Resampler, Presets, Oberfläche |
| `tests/` | Catch2-Tests (`Tests`) |
| `JUCE/`, `cmake/`, `modules/clap-juce-extensions/` | Submodule (JUCE `develop`, Pamplejuce-CMake, CLAP) |

## Bauen

```bash
git clone --recursive https://github.com/Michi71/ToneWerk.git
cd ToneWerk
git lfs pull
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
ctest --test-dir Builds --output-on-failure
```

Voraussetzungen: CMake ≥ 3.25, Ninja, ein C++20-Compiler (Xcode/Clang, clang-cl oder MSVC, GCC/Clang unter Linux) und unter
Linux die JUCE-Abhängigkeiten (`libasound2-dev libjack-jackd2-dev libx11-dev libxinerama-dev libxext-dev libxrandr-dev
libxcursor-dev libfreetype6-dev libfontconfig1-dev`). Die Ergebnisse liegen in `Builds/ToneWerk_artefacts/Release/`; auf dem Mac
kopiert der Build VST3, AU und CLAP nach `~/Library/Audio/Plug-Ins/`.

CMake-Optionen:

- `TONEWERK_DEV_BANKS` (an): der Build findet die Banken in seinem `banks/`-Ordner. Für eine Weitergabe aus.
- `TONEWERK_BUNDLE_BANKS` (aus): kopiert `banks/*.bank` in die Plugin-Bundles (`Contents/Resources/banks`) und neben das
  Standalone-Programm.
- `TONEWERK_FLOAT` (aus): die Engine rechnet in einfacher Genauigkeit (`PMP_FLOAT`).

## Banken

Ein Hybrid-Instrument braucht seine Bank. Gesucht wird in dieser Reihenfolge:

1. der im Plugin gewählte Ordner (Knopf **Banken… → Ordner wählen…**, gilt für alle Instanzen),
2. `$TONEWERK_BANKS`,
3. `banks/` im Bundle (`Contents/Resources/banks`) bzw. neben dem Programm,
4. der Benutzerordner: `~/Library/Application Support/ToneWerk/banks` (macOS), `%APPDATA%\ToneWerk\banks` (Windows),
   `~/.config/ToneWerk/banks` (Linux),
5. der Systemordner: `/Library/Application Support/ToneWerk/banks`, `C:\ProgramData\ToneWerk\banks`, `/usr/local/share/ToneWerk/banks`,
6. im Entwicklungs-Build `banks/` des Quellbaums.

Fehlt eine Bank, spielen Mark I, Mark II, Suitcase, Wurlitzer und Clavinet ihr physikalisches Modell; Flügel, Pianet und CP80
haben keines und bleiben stumm. Die Statuszeile unter der Instrumentenwahl sagt, was spielt.

## Bedienung

- **Instrument**: die Presets der Engine. Ein Wechsel lädt die Bank im Hintergrund; das alte Instrument blendet in 30 ms aus.
- **Lautstärke** (−48 … +12 dB) hinter der Effektkette, danach der Soft-Clip der Engine.
- **Voicing** auf den Polen und Layern der Bank (pmpiano README „Voicing“): Hammerhärte, Abklingen, Chorbreite, Streckung. Jede
  Änderung baut das Instrument neu (nach 250 ms Ruhe), klingende Noten brechen dabei ab.
- **Effekte**: die MK8-Kette in den Einheiten des Rhodes V8 Pro – Vorstufe (Drive, Hüllkurve, Pegel), Equalizer, Vari-Pan,
  Kompressor, Chorus, Phaser, Delay. Der Schalter **FX** nimmt die ganze Kette heraus.
- **Presets**: Werkspresets und eigene Presets in einer Liste; **Speichern** legt die aktuellen Einstellungen als
  `~/Documents/ToneWerk/Presets/<Name>.twpreset` ab (XML), **Löschen** legt ein eigenes Preset in den Papierkorb, `<`/`>` blättern.
  Der Host speichert den Zustand samt Presetnamen im Projekt.
- **Klaviatur**: die 73 Tasten E1–E7 der Engine, spielbar mit der Maus; eingehende MIDI-Noten leuchten mit.
- MIDI: Note On/Off mit Velocity, CC64 (Halbpedal), CC1 (Pickup-Voicing des Modells), CC120/123.

## Technik

- Die Engine läuft bei der **Rate ihrer Bank** (44,1 kHz, der SGP-Flügel 48 kHz; das Modell ohne Bank 48 kHz). Ein Windowed-Sinc-
  Interpolator je Kanal setzt auf die Host-Rate um; bei gleicher Rate entfällt er.
- Ein Ladethread baut Instrumente (Bank lesen, Engine vorbereiten) und übergibt sie über einen atomaren Zeiger; der Audiothread
  übernimmt sie, blendet das alte aus und gibt es zum Löschen zurück. Auf dem Audiothread wird nichts angelegt oder freigegeben.
- Die Effektkette (`pmp::Mk8Effects`) läuft im Plugin bei der Host-Rate, die Engine selbst mit `fx.on=0` und ohne Soft-Clip.
- Die Engine baut ohne Fast-Math (eigenes Ziel `pmpcore`); das Pamplejuce-Standard-`-Ofast` ist bewusst nicht übernommen.

## CI

`.github/workflows/build.yml` baut und testet auf macOS (Universal), Linux und Windows, prüft das VST3 mit pluginval
(Strictness 10) und lädt je System ein Zip hoch. Die Banken checkt die CI **nicht** aus (LFS-Kontingent eines privaten
Repositorys); die Tests laufen dort mit den physikalischen Modellen. Ein Tag `v*` erzeugt ein Release mit den drei Zips und
`ToneWerk-Banks.zip`. Die Builds sind nicht signiert: macOS meldet sie als unbekannt (`xattr -dr com.apple.quarantine <Plugin>`),
Windows SmartScreen ebenso.

## Lizenz

GPL-3.0-or-later wie pmpiano (`LICENSE`). JUCE steht unter AGPLv3 oder der JUCE-Lizenz; ein weitergegebenes Binary unterliegt
damit den Bedingungen der AGPLv3. Für die Banken gelten die Lizenzen der freien Bibliotheken, aus denen sie gebaut sind
([docs/BANKS.md](docs/BANKS.md)).
