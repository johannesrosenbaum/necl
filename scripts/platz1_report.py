#!/usr/bin/env python3
"""Platz-1-Report: Ratio-Suite + MCU-Flash → Markdown + JSON."""
from __future__ import annotations

import json
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "mcu"
RATIO = OUT / "ratio.json"
SIZE = OUT / "mcu-size.json"
TIMING = OUT / "host-timing.json"
QEMU = OUT / "qemu-bench.json"
ABLATION = OUT / "ablation-deployable.json"
ROI = OUT / "roi-sensor.json"

# Offizielle Suite (siehe docs/PLATZ1.md)
SUITE = [
    "ticks",
    "ticks_16k",
    "timeseries_i16",
    "nab_machine_temp.i16le",
    "nab_ambient_temp.i16le",
    "nab_ec2_cpu.i16le",
    "melbourne_daily_min_temp.i16le",
    "intel_lab_temp.i16le",
    "ucr_Coffee.i16le",
    "ucr_GunPoint.i16le",
    "ucr_MoteStrain.i16le",
    "ucr_ECG200.i16le",
    "harness128",
    "random_16k",
]

PEERS = ["nec_lite", "sprintz_d", "heatshrink", "lz4", "miniz", "drh"]


def pct(s: str) -> float:
    return float(s.strip().rstrip("%"))


def main() -> None:
    if not RATIO.exists():
        raise SystemExit(f"missing {RATIO} — run bash scripts/codec_ratio.sh")

    rows = json.loads(RATIO.read_text())["rows"]
    by: dict[str, dict[str, dict]] = defaultdict(dict)
    for r in rows:
        by[r["corpus"]][r["codec"]] = r

    lines = [
        "# NECL Platz-1-Report",
        "",
        f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "## Ratio (Suite)",
        "",
        "| Corpus | NECL | Sprintz | heatshrink | lz4 | miniz | Best |",
        "|--------|-----:|--------:|-----------:|----:|------:|------|",
    ]

    nec_wins_hs = 0
    nec_wins_sp = 0
    sp_n = 0
    hs_n = 0
    table_json = []

    for c in SUITE:
        if c not in by or "nec_lite" not in by[c]:
            continue
        vals = {p: pct(by[c][p]["ratio"]) for p in PEERS if p in by[c]}
        nec = vals["nec_lite"]
        sp = vals.get("sprintz_d")
        hs = vals.get("heatshrink")
        best = min(vals, key=vals.get)
        if hs is not None:
            hs_n += 1
            if nec < hs:
                nec_wins_hs += 1
        if sp is not None:
            sp_n += 1
            # Remis zählt als Platz-1-OK (±0,05 pp / Bitpack-Parität)
            if nec <= sp + 0.05:
                nec_wins_sp += 1
        def fmt(p: str) -> str:
            if p not in vals:
                return "—"
            v = vals[p]
            mark = " **" if p == best else ""
            return f"{mark}{v:.1f}%**" if p == best else f"{v:.1f}%"

        lines.append(
            f"| `{c}` | {fmt('nec_lite')} | {fmt('sprintz_d')} | "
            f"{fmt('heatshrink')} | {fmt('lz4')} | {fmt('miniz')} | `{best}` |"
        )
        table_json.append(
            {
                "corpus": c,
                "ratios": vals,
                "best": best,
                "nec_beats_heatshrink": hs is not None and nec < hs,
                "nec_beats_sprintz": sp is not None and nec <= sp + 0.05,
            }
        )

    lines += [
        "",
        "## Scorecard (Host-Suite `codec_ratio`)",
        "",
        f"- vs **heatshrink** (Markt-Peer): **{nec_wins_hs}/{hs_n}** besser",
        f"- vs **Sprintz-Delta** (Algo-Peer, Remis ±0,05 pp): **{nec_wins_sp}/{sp_n}** ≥",
        "",
        "_Achtung: `nec_lite` in `codec_ratio` ist der **Host**-Build (Kompakt/LZ möglich)._",
        "",
    ]

    ablation_payload = {}
    lines += ["## Deploybar vs Host (Ablation)", ""]
    if ABLATION.exists():
        ablation_payload = json.loads(ABLATION.read_text())
        dep = {r["corpus"]: r for r in ablation_payload.get("deployable", {}).get("rows", [])}
        hst = {r["corpus"]: r for r in ablation_payload.get("host", {}).get("rows", [])}
        lines += [
            "| Corpus | Deploybar (`NO_MALLOC`) | Host choose-best | Δpp |",
            "|--------|------------------------:|-----------------:|----:|",
        ]
        for c in (
            "ticks",
            "timeseries_i16",
            "nab_machine_temp.i16le",
            "nab_ambient_temp.i16le",
            "nab_ec2_cpu.i16le",
            "intel_lab_temp.i16le",
            "joint_7axis_1khz_10s.i16le",
        ):
            if c not in dep:
                continue
            d = dep[c]["ratio_pct"]
            if c in hst:
                h = hst[c]["ratio_pct"]
                lines.append(f"| `{c}` | **{d:.1f}%** | {h:.1f}% | {h - d:+.1f} |")
            else:
                lines.append(f"| `{c}` | **{d:.1f}%** | — | — |")
        lines += [
            "",
            f"_{ablation_payload.get('note', '')}_",
            "",
        ]
        if "ticks" in dep and dep["ticks"]["ratio_pct"] > 95.0:
            lines += [
                "> **Hinweis:** `ticks` expandiert unter `NO_MALLOC`, weil Bitpack nur für "
                "int16 greift und Nibble-RC auf dem MCU abgeschaltet ist — Host ~20 %. "
                "Deploybares Tick-Produkt braucht eigenen Pfad (nicht im Ambient-ROI).",
                "",
            ]
    else:
        lines.append("_Keine `ablation-deployable.json` — `bash scripts/ablation_deployable.sh`._")
        lines.append("")

    roi_payload = {}
    if ROI.exists():
        roi_payload = json.loads(ROI.read_text())
        lines += [
            "## ROI-Skizze (Vertical Temperaturfühler)",
            "",
            f"- {roi_payload.get('pitch', '')}",
            f"- Details: `build/mcu/roi-sensor.md` (Ratio {roi_payload.get('ratio_pct')}%, "
            f"Quelle {roi_payload.get('ratio_source')})",
            "",
        ]

    lines += [
        "## Flash / RAM (Cortex-M)",
        "",
    ]

    size_payload = {}
    if SIZE.exists():
        size_payload = json.loads(SIZE.read_text())
        for cpu in ("cortex-m3", "cortex-m0"):
            block = size_payload.get(cpu)
            if not block:
                continue
            lines.append(f"### {cpu}")
            lines.append("")
            lines.append("| Codec | Flash (.text) | Library-State |")
            lines.append("|-------|--------------:|--------------:|")
            for name in ("nec_lite", "heatshrink", "lz4", "miniz"):
                if name not in block:
                    continue
                e = block[name]
                flash = e.get("flash", e.get("text", "?"))
                lib = e.get("library_state_bytes", 0)
                lines.append(f"| {name} | {flash} | {lib} |")
            lines.append("")
    else:
        lines.append("_Keine `mcu-size.json` — `bash scripts/mcu_build.sh`._")
        lines.append("")

    timing_payload = {}
    lines += [
        "## Host-Timing-Proxy",
        "",
        "_Median Enc/Dec auf `timeseries_16k` (Host-CPU, kein MCU-Zyklus)._",
        "",
    ]
    if TIMING.exists():
        timing_payload = json.loads(TIMING.read_text())
        lines.append("| Codec | Enc MB/s | Dec MB/s | coded |")
        lines.append("|-------|---------:|---------:|------:|")
        for row in timing_payload.get("rows", []):
            lines.append(
                f"| `{row['codec']}` | {row['enc_mb_s']:.1f} | "
                f"{row['dec_mb_s']:.1f} | {row['coded']} |"
            )
        lines.append("")
        tsz = timing_payload.get("tsz", "n/a")
        lines.append(f"- **tsz:** `{tsz}`")
        lines.append("")
    else:
        lines.append("_Keine `host-timing.json` — `bash scripts/codec_ratio.sh`._")
        lines.append("")

    qemu_payload = {}
    lines += ["## QEMU MCU-Bench (Cortex-M3)", ""]
    if QEMU.exists():
        qemu_payload = json.loads(QEMU.read_text())
        lines.append(
            f"- ok={qemu_payload.get('ok')} n={qemu_payload.get('n')} "
            f"coded={qemu_payload.get('coded')} "
            f"cy_enc={qemu_payload.get('cy_enc')} cy_dec={qemu_payload.get('cy_dec')}"
        )
        lines.append(f"- _{qemu_payload.get('note', '')}_")
        lines.append("")
    else:
        lines.append("_Keine `qemu-bench.json` — `bash scripts/mcu_qemu.sh` / `platz1_bench.sh`._")
        lines.append("")

    lines += [
        "## Peers außerhalb der C-Harness",
        "",
        "- **tsz** (Rust, MIT/Apache): Cortex-M / BLE-Paket-Bitpack — separat evaluieren,",
        "  nicht als C-Vendor in `codec_ratio` verdrahtet (braucht `cargo`).",
        "",
        "Siehe [docs/PLATZ1.md](../docs/PLATZ1.md).",
        "",
    ]

    OUT.mkdir(parents=True, exist_ok=True)
    md_path = OUT / "platz1-report.md"
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (OUT / "platz1-report.json").write_text(
        json.dumps(
            {
                "suite": table_json,
                "scorecard": {
                    "vs_heatshrink": f"{nec_wins_hs}/{hs_n}",
                    "vs_sprintz": f"{nec_wins_sp}/{sp_n}",
                },
                "mcu_size": size_payload,
                "host_timing": timing_payload,
                "qemu_bench": qemu_payload,
                "ablation": ablation_payload,
                "roi_sensor": roi_payload,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(md_path.read_text())
    print(f"wrote {md_path}")
    print(f"wrote {OUT / 'platz1-report.json'}")


if __name__ == "__main__":
    main()
