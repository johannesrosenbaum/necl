# NECL ROI — Temperaturfühler (deploybar)

Generated: 2026-09-17 15:38:55

## Anwendungsfall

Gebäude-/Hallen-Temperaturfühler (int16 Stream, Batch-TX)

- Corpus-Proxy: `nab_ambient_temp.i16le`
- **Deploybare Ratio:** **59.9%** (ablation-deployable.json (NO_MALLOC))
- Claim: ROI auf deploybarer NO_MALLOC-Ratio — nicht Host-0xCE/0xCB

## Szenario

| Parameter | Wert |
|-----------|------|
| Geräte | 500 |
| Sample-Intervall | 300 s |
| Batch / TX | 12 Samples → TX alle 60 min |
| Sample | 2 B int16 + 12 B Header/TX |
| Horizont | 5 Jahr(e) |
| Mobilfunk | 0.50 €/MB |
| Batterie roh | 4.0 J, TX-Anteil Energie 60% |
| Service | 55 € / Tausch |

## Ergebnis (Flotte)

| | Roh | NECL deploybar | Delta |
|--|----:|---------------:|------:|
| MB gesamt | 752.39 | 551.05 | **−201.34** |
| Datenkosten € | — | — | **−101 €** |
| Batterie-Laufzeit | 4.0 J | **4.8 J** | — |
| Service-Kosten € | — | — | **−5519 €** |
| **Summe gespart** | | | **5620 €** |

Pro Gerät und Jahr: 315576 B → 231128 B TX
(−26.8% inkl. Header).

## Pitch

> 500 Fühler, Sample/300s, Batch 12 → TX alle 60 min, deploybar 59.9% Rest → ca. 5620 € gespart über 5 J (Daten + Batterie-Service, Modell).

## Grenzen

- Keine gemessenen Board-µJ (Modell: TX-Energieanteil).
- €/MB und Servicekosten kundenspezifisch — CLI-Flags überschreiben.
- Host-Kompakt (`0xCE`) / Match (`0xCB`) **nicht** eingerechnet.
