# The hybrid banks

The nine banks in `banks/` come from pmpiano (version `0201ecc`, 2026-10-25; see `docs/SAMPLELIB_IMPORT.md` and
`docs/SAMPLELIB_BANKS.md` there). For every key a bank holds a short PCM attack from the recordings and the fitted modes for
sustain, damper and release; the engine computes the sound after the attack. Each bank comes with its manifest (`*.bank.json`),
some with their level fit (`*_hybrid_level.json`).

| Slot | File | Instrument (preset) | Rate | Source |
|---|---|---|---|---|
| 0 | `piano_hybrid.bank` | Grand Piano (`grand`) | 48 kHz | Salamander Grand Piano (SGP) |
| 1 | `mark1_hybrid.bank` | Rhodes Mark I (`mark1`) | 44.1 kHz | Matt's Fender Rhodes (Pianobook) |
| 2 | `mark2_hybrid.bank` | Rhodes Mark II (`mark2`), Suitcase (`suitcase`) | 44.1 kHz | mRhodes (Pianobook, Mark II 73 from 1982) |
| 3 | `wurli_hybrid.bank` | Wurlitzer 200A (`wurli200a`) | 44.1 kHz | WörliTzer 1.2 (Pianobook) |
| 4 | `clav_hybrid.bank` | Clavinet D6 (`clav6`) | 44.1 kHz | Clavinet_Lit.sf2 "Realistic Clavinet" (Lithalean/Mike77154) |
| 5 | `pianet_hybrid.bank` | Pianet T (`pianet`) | 44.1 kHz | Anne-Marie the Hohner T (Pianobook) |
| 6 | `steinway_hybrid.bank` | Steinway Grand (`steinway`) | 44.1 kHz | Steinway Grand (Pianobook) |
| 7 | `grandpas_hybrid.bank` | Grandpa's Grand (`grandpas`) | 44.1 kHz | Grandpa's Piano (Pianobook, Steinway L 1933) |
| 8 | `cp80_hybrid.bank` | Yamaha CP80 (`cp80`) | 44.1 kHz | GregSullivan.E-Pianos (sfzinstruments) |

The Rhodes MK8 has no bank (no licence for the plugin renderings) and plays the physical model.

## Licences

The banks are not covered by the GPL of the code. The licences of the libraries they are built from apply; check them library
by library before distributing the banks (a release, or a bundle built with `TONEWERK_BUNDLE_BANKS`).

## New banks

Banks are made in pmpiano (`tools/analysis/rhodes_hybrid_*.py`, `piano_hybrid_export.py`). A new bank replaces its file in
`banks/` (Git LFS); the engine preset picks the slot with `hybrid.bank`. A bank plays at its own rate; the plugin converts to
the host rate.
