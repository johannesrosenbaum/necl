#!/usr/bin/env python3
"""
ROI-Rechnung: batteriebetriebener Temperaturfühler (NECL-Nische).

Basis: deploybare NO_MALLOC-Ratio (Default: nab_ambient ~59.9%),
nicht Host-Kompakt/LZ.

Beispiel-Annahmen sind konservativ und als Verkaufs-Skizze gedacht —
keine Board-µJ-Messung.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "mcu"
ABLATION = OUT / "ablation-deployable.json"

# Default: Ambient-Temperatur = Kern-Vertical
DEFAULT_CORPUS = "nab_ambient_temp.i16le"
DEFAULT_RATIO = 59.9  # falls noch keine Ablation gelaufen


def load_deployable_ratio(corpus: str) -> tuple[float, str]:
    if ABLATION.exists():
        data = json.loads(ABLATION.read_text())
        for r in data.get("deployable", {}).get("rows", []):
            if r["corpus"] == corpus:
                return float(r["ratio_pct"]), "ablation-deployable.json (NO_MALLOC)"
    return DEFAULT_RATIO, f"fallback default {DEFAULT_RATIO}% (run ablation_deployable.sh)"


def main() -> None:
    ap = argparse.ArgumentParser(description="NECL Sensor-ROI (deploybare Ratio)")
    ap.add_argument("--corpus", default=DEFAULT_CORPUS)
    ap.add_argument("--ratio-pct", type=float, default=None, help="Override deploybare Ratio %%")
    ap.add_argument("--interval-s", type=float, default=300.0, help="Sample-Intervall Sekunden")
    ap.add_argument(
        "--batch",
        type=int,
        default=12,
        help="Samples pro Funk-Paket (Puffer; Header sonst dominiert)",
    )
    ap.add_argument("--sample-bytes", type=int, default=2, help="Rohbytes pro Sample (int16=2)")
    ap.add_argument("--header-bytes", type=int, default=12, help="Funk-/App-Header pro TX")
    ap.add_argument("--devices", type=int, default=500)
    ap.add_argument("--years", type=float, default=5.0)
    ap.add_argument("--eur-per-mb", type=float, default=0.50, help="€/MB Mobilfunk-Payload")
    ap.add_argument("--tx-energy-share", type=float, default=0.60)
    ap.add_argument("--battery-years-raw", type=float, default=4.0, help="Laufzeit ohne Codec")
    ap.add_argument("--service-eur", type=float, default=55.0, help="€ pro Batterie-Service")
    args = ap.parse_args()

    if args.ratio_pct is not None:
        ratio = args.ratio_pct
        ratio_src = "cli --ratio-pct"
    else:
        ratio, ratio_src = load_deployable_ratio(args.corpus)

    frac = ratio / 100.0
    batch = max(1, args.batch)
    samples_per_year = (365.25 * 24.0 * 3600.0) / args.interval_s
    tx_per_year = samples_per_year / batch
    body_raw = batch * args.sample_bytes
    body_nec = body_raw * frac
    raw_tx = tx_per_year * (args.header_bytes + body_raw)
    nec_tx = tx_per_year * (args.header_bytes + body_nec)
    bytes_saved = raw_tx - nec_tx

    mb_raw = raw_tx / (1024 * 1024)
    mb_nec = nec_tx / (1024 * 1024)
    mb_saved = bytes_saved / (1024 * 1024)

    eur_raw = mb_raw * args.eur_per_mb * args.devices * args.years
    eur_nec = mb_nec * args.eur_per_mb * args.devices * args.years
    eur_data = eur_raw - eur_nec

    pkt_raw = args.header_bytes + body_raw
    pkt_nec = args.header_bytes + body_nec
    e_ratio = (1.0 - args.tx_energy_share) + args.tx_energy_share * (pkt_nec / pkt_raw)
    batt_years_nec = args.battery_years_raw / e_ratio if e_ratio > 0 else args.battery_years_raw
    services_raw = args.years / args.battery_years_raw
    services_nec = args.years / batt_years_nec
    eur_batt = (services_raw - services_nec) * args.service_eur * args.devices

    payload = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "vertical": "Gebäude-/Hallen-Temperaturfühler (int16 Stream, Batch-TX)",
        "claim": "ROI auf deploybarer NO_MALLOC-Ratio — nicht Host-0xCE/0xCB",
        "ratio_pct": ratio,
        "ratio_source": ratio_src,
        "corpus": args.corpus,
        "assumptions": {
            "interval_s": args.interval_s,
            "batch_samples": batch,
            "tx_interval_s": args.interval_s * batch,
            "sample_bytes": args.sample_bytes,
            "header_bytes": args.header_bytes,
            "devices": args.devices,
            "years": args.years,
            "eur_per_mb": args.eur_per_mb,
            "tx_energy_share": args.tx_energy_share,
            "battery_years_raw": args.battery_years_raw,
            "service_eur": args.service_eur,
        },
        "per_device_year": {
            "samples": round(samples_per_year, 1),
            "tx_count": round(tx_per_year, 1),
            "tx_bytes_raw": round(raw_tx, 1),
            "tx_bytes_nec": round(nec_tx, 1),
            "tx_bytes_saved_frac": round(bytes_saved / raw_tx, 4) if raw_tx else 0,
        },
        "fleet": {
            "mb_raw": round(mb_raw * args.devices * args.years, 3),
            "mb_nec": round(mb_nec * args.devices * args.years, 3),
            "mb_saved": round(mb_saved * args.devices * args.years, 3),
            "eur_data_saved": round(eur_data, 2),
            "battery_years_raw": args.battery_years_raw,
            "battery_years_nec": round(batt_years_nec, 2),
            "eur_service_saved": round(eur_batt, 2),
            "eur_total_saved": round(eur_data + eur_batt, 2),
        },
        "pitch": (
            f"{args.devices} Fühler, Sample/{args.interval_s:.0f}s, "
            f"Batch {batch} → TX alle {args.interval_s * batch / 60:.0f} min, "
            f"deploybar {ratio:.1f}% Rest → ca. {eur_data + eur_batt:.0f} € "
            f"gespart über {args.years:.0f} J (Daten + Batterie-Service, Modell)."
        ),
    }

    OUT.mkdir(parents=True, exist_ok=True)
    json_path = OUT / "roi-sensor.json"
    md_path = OUT / "roi-sensor.md"
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    f = payload["fleet"]
    p = payload["per_device_year"]
    md = f"""# NECL ROI — Temperaturfühler (deploybar)

Generated: {payload['generated']}

## Anwendungsfall

{payload['vertical']}

- Corpus-Proxy: `{payload['corpus']}`
- **Deploybare Ratio:** **{ratio:.1f}%** ({ratio_src})
- Claim: {payload['claim']}

## Szenario

| Parameter | Wert |
|-----------|------|
| Geräte | {args.devices} |
| Sample-Intervall | {args.interval_s:.0f} s |
| Batch / TX | {batch} Samples → TX alle {args.interval_s * batch / 60:.0f} min |
| Sample | {args.sample_bytes} B int16 + {args.header_bytes} B Header/TX |
| Horizont | {args.years:.0f} Jahr(e) |
| Mobilfunk | {args.eur_per_mb:.2f} €/MB |
| Batterie roh | {args.battery_years_raw:.1f} J, TX-Anteil Energie {args.tx_energy_share:.0%} |
| Service | {args.service_eur:.0f} € / Tausch |

## Ergebnis (Flotte)

| | Roh | NECL deploybar | Delta |
|--|----:|---------------:|------:|
| MB gesamt | {f['mb_raw']:.2f} | {f['mb_nec']:.2f} | **−{f['mb_saved']:.2f}** |
| Datenkosten € | — | — | **−{f['eur_data_saved']:.0f} €** |
| Batterie-Laufzeit | {f['battery_years_raw']:.1f} J | **{f['battery_years_nec']:.1f} J** | — |
| Service-Kosten € | — | — | **−{f['eur_service_saved']:.0f} €** |
| **Summe gespart** | | | **{f['eur_total_saved']:.0f} €** |

Pro Gerät und Jahr: {p['tx_bytes_raw']:.0f} B → {p['tx_bytes_nec']:.0f} B TX
(−{p['tx_bytes_saved_frac']*100:.1f}% inkl. Header).

## Pitch

> {payload['pitch']}

## Grenzen

- Keine gemessenen Board-µJ (Modell: TX-Energieanteil).
- €/MB und Servicekosten kundenspezifisch — CLI-Flags überschreiben.
- Host-Kompakt (`0xCE`) / Match (`0xCB`) **nicht** eingerechnet.
"""
    md_path.write_text(md, encoding="utf-8")
    print(md)
    print(f"wrote {md_path}")
    print(f"wrote {json_path}")


if __name__ == "__main__":
    main()
