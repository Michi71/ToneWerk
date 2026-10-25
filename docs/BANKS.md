# Die Hybrid-Banken

Die neun Banken in `banks/` sind aus pmpiano übernommen (Stand `0201ecc`, 2026-10-25; dort `docs/SAMPLELIB_IMPORT.md` und
`docs/SAMPLELIB_BANKS.md`). Jede Bank enthält je Taste einen kurzen PCM-Anschlag aus den Aufnahmen und die gefitteten Moden für
Sustain, Dämpfer und Ausklang; den Klang danach rechnet die Engine. Neben jeder Bank liegt ihr Manifest (`*.bank.json`), bei
einigen der Pegel-Fit (`*_hybrid_level.json`).

| Slot | Datei | Instrument (Preset) | Rate | Quelle |
|---|---|---|---|---|
| 0 | `piano_hybrid.bank` | Grand Piano (`grand`) | 48 kHz | Salamander Grand Piano (SGP) |
| 1 | `mark1_hybrid.bank` | Rhodes Mark I (`mark1`) | 44,1 kHz | Matt's Fender Rhodes (Pianobook) |
| 2 | `mark2_hybrid.bank` | Rhodes Mark II (`mark2`), Suitcase (`suitcase`) | 44,1 kHz | mRhodes (Pianobook, Mark II 73 von 1982) |
| 3 | `wurli_hybrid.bank` | Wurlitzer 200A (`wurli200a`) | 44,1 kHz | WörliTzer 1.2 (Pianobook) |
| 4 | `clav_hybrid.bank` | Clavinet D6 (`clav6`) | 44,1 kHz | Clavinet_Lit.sf2 „Realistic Clavinet“ (Lithalean/Mike77154) |
| 5 | `pianet_hybrid.bank` | Pianet T (`pianet`) | 44,1 kHz | Anne-Marie the Hohner T (Pianobook) |
| 6 | `steinway_hybrid.bank` | Steinway Grand (`steinway`) | 44,1 kHz | Steinway Grand (Pianobook) |
| 7 | `grandpas_hybrid.bank` | Grandpa's Grand (`grandpas`) | 44,1 kHz | Grandpa's Piano (Pianobook, Steinway L 1933) |
| 8 | `cp80_hybrid.bank` | Yamaha CP80 (`cp80`) | 44,1 kHz | GregSullivan.E-Pianos (sfzinstruments) |

Der Rhodes MK8 hat keine Bank (keine Lizenz für die Plugin-Renderings) und spielt das physikalische Modell.

## Lizenzen

Die Banken fallen nicht unter die GPL des Codes. Es gelten die Lizenzen der Bibliotheken, aus denen sie gebaut sind; vor einer
Weitergabe der Banken (Release, Bundle mit `TONEWERK_BUNDLE_BANKS`) sind diese je Bibliothek zu prüfen.

## Neue Banken

Banken entstehen in pmpiano (`tools/analysis/rhodes_hybrid_*.py`, `piano_hybrid_export.py`). Eine neue Bank ersetzt die Datei in
`banks/` (Git LFS); das Preset der Engine bestimmt mit `hybrid.bank` den Slot. Eine Bank spielt bei ihrer eigenen Rate, das
Plugin setzt auf die Host-Rate um.
