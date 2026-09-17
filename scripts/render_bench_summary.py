#!/usr/bin/env python3
"""Rendert bench/published/SUMMARY.md + README.md aus JSON-Snapshots."""
from __future__ import annotations

import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUB = ROOT / "bench" / "published"

FOCUS = [
    "ticks",
    "timeseries_i16",
    "nab_machine_temp.i16le",
    "nab_ambient_temp.i16le",
    "nab_ec2_cpu.i16le",
    "intel_lab_temp.i16le",
    "joint_7axis_1khz_10s.i16le",
]


def pct_row(r: dict) -> float:
    return float(r["ratio_pct"])


def main() -> None:
    abl = json.loads((PUB / "ablation-deployable.json").read_text())
    dep = {r["corpus"]: r for r in abl["deployable"]["rows"]}
    hst = {r["corpus"]: r for r in abl["host"]["rows"]}

    size = {}
    if (PUB / "mcu-size.json").exists():
        size = json.loads((PUB / "mcu-size.json").read_text())

    timing = {}
    if (PUB / "host-timing.json").exists():
        timing = json.loads((PUB / "host-timing.json").read_text())

    ratio = {}
    if (PUB / "ratio.json").exists():
        rows = json.loads((PUB / "ratio.json").read_text())["rows"]
        from collections import defaultdict

        by = defaultdict(dict)
        for r in rows:
            by[r["corpus"]][r["codec"]] = r
        ratio = by

    gen = time.strftime("%Y-%m-%d %H:%M:%S %z")
    lines = [
        "# NECL Bench Summary (publishable)",
        "",
        f"Generated: `{gen}`",
        "",
        "## Rules (read first)",
        "",
        "| Label | Meaning | Use in claims? |",
        "|-------|---------|----------------|",
        "| **Deployable** | `-DNEC_LITE_NO_MALLOC` — Stream Bitpack/Zero/STORE | **Yes — board / SDK** |",
        "| **Host** | Full choose-best (`0xCE`/`0xCF`/`0xCB`, stream+RC) | Gateway only — label clearly |",
        "",
        "Ratio = coded/orig × 100 (lower is better). All rows roundtrip-verified.",
        "",
        "## Deployable vs Host",
        "",
        "| Corpus | Deployable | Host | Δpp | Notes |",
        "|--------|----------:|-----:|----:|-------|",
    ]

    notes = {
        "ticks": "TICK8: deployable expands (no RC on MCU)",
        "timeseries_i16": "smooth AR(1) int16",
        "nab_machine_temp.i16le": "vertical: machine temp",
        "nab_ambient_temp.i16le": "vertical: ambient temp / ROI",
        "nab_ec2_cpu.i16le": "plateaus — LZ peer territory",
        "intel_lab_temp.i16le": "long series; host uses stream+RC",
        "joint_7axis_1khz_10s.i16le": "synthetic 7-axis @ 1 kHz, planar layout (robotics proxy)",
    }

    for c in FOCUS:
        if c not in dep:
            continue
        d = pct_row(dep[c])
        h = pct_row(hst[c]) if c in hst else None
        if h is None:
            lines.append(f"| `{c}` | **{d:.1f}%** | — | — | {notes.get(c, '')} |")
        else:
            lines.append(
                f"| `{c}` | **{d:.1f}%** | {h:.1f}% | {h - d:+.1f} | {notes.get(c, '')} |"
            )

    lines += [
        "",
        "## Peers on focus corpora (Host harness `codec_ratio`)",
        "",
        "_Peer table uses the **host** `nec_lite` binary (may include compact/LZ). "
        "Compare deployable column above for board claims._",
        "",
        "| Corpus | NECL host | heatshrink | Sprintz | lz4 | miniz |",
        "|--------|----------:|-----------:|--------:|----:|------:|",
    ]

    def rp(corpus: str, codec: str) -> str:
        if corpus not in ratio or codec not in ratio[corpus]:
            return "—"
        return ratio[corpus][codec]["ratio"]

    for c in FOCUS:
        if c not in ratio or "nec_lite" not in ratio[c]:
            continue
        lines.append(
            f"| `{c}` | {rp(c, 'nec_lite')} | {rp(c, 'heatshrink')} | "
            f"{rp(c, 'sprintz_d')} | {rp(c, 'lz4')} | {rp(c, 'miniz')} |"
        )

    lines += ["", "## Flash (freestanding `.text`)", ""]
    if size:
        lines.append("| Target | nec_lite | heatshrink | lz4 | miniz |")
        lines.append("|--------|--------:|-----------:|----:|------:|")
        for cpu in ("cortex-m3", "cortex-m0"):
            b = size.get(cpu, {})
            if not b:
                continue

            def fl(name: str) -> str:
                e = b.get(name, {})
                v = e.get("flash", e.get("text"))
                return str(v) if v is not None else "—"

            lines.append(
                f"| {cpu} | {fl('nec_lite')} | {fl('heatshrink')} | "
                f"{fl('lz4')} | {fl('miniz')} |"
            )
        lines.append("")
        lines.append("Library heap: **0**. `scratch_bound(n)`: **0**.")
        lines.append("")
    else:
        lines.append("_No `mcu-size.json` in this snapshot._")
        lines.append("")

    lines += ["## Host timing proxy (`timeseries_16k`)", ""]
    if timing.get("rows"):
        lines.append("| Codec | Enc MB/s | Dec MB/s |")
        lines.append("|-------|---------:|---------:|")
        for row in timing["rows"]:
            lines.append(
                f"| `{row['codec']}` | {row['enc_mb_s']:.1f} | {row['dec_mb_s']:.1f} |"
            )
        lines.append("")
        lines.append("_Host x86_64 proxy — **not** MCU cycles / µJ._")
        lines.append("")
    else:
        lines.append("_No host-timing.json._")
        lines.append("")

    # Joint bus sketch
    if "joint_7axis_1khz_10s.i16le" in dep:
        d = pct_row(dep["joint_7axis_1khz_10s.i16le"])
        raw_bps = 7 * 2 * 1000  # 14 kB/s
        nec_bps = raw_bps * (d / 100.0)
        lines += [
            "## Robotics proxy — payload model (not bus %)",
            "",
            f"Corpus `joint_7axis_1khz_10s.i16le`: deployable **{d:.1f}%**.",
            "",
            f"- Raw payload @ 1 kHz × 7 × int16: **{raw_bps} B/s**",
            f"- NECL deployable body (same rate): **~{nec_bps:.0f} B/s**",
            f"- Body savings: **~{100 - d:.0f}%** of sample bytes",
            "",
            "CAN/EtherCAT frame headers, PDO slots and worst-case STORE still apply. "
            "Do **not** quote this as “bus load −X%” without a schedule model.",
            "",
        ]

    lines += [
        "## Reproduce",
        "",
        "```bash",
        "bash scripts/publish_bench.sh",
        "# → bench/published/",
        "```",
        "",
        "Methodology: [`docs/BENCH.md`](../../docs/BENCH.md).",
        "",
    ]

    PUB.mkdir(parents=True, exist_ok=True)
    (PUB / "SUMMARY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    readme = f"""# NECL published benches

Snapshot generated `{gen}`.

| File | Content |
|------|---------|
| [SUMMARY.md](SUMMARY.md) | Human-readable scorecard |
| `ablation-deployable.json` | Deployable (`NO_MALLOC`) vs host |
| `ratio.json` | Full peer suite (host `nec_lite`) |
| `mcu-size.json` | Cortex-M flash |
| `host-timing.json` | Host MB/s proxy |
| `platz1-report.md` | Full Platz-1 report |
| `roi-sensor.md` | Example ROI (ambient vertical) |

**Claim rule:** Board/SDK numbers = **Deployable** column only.

Regenerate: `bash scripts/publish_bench.sh`
"""
    (PUB / "README.md").write_text(readme, encoding="utf-8")
    print("wrote", PUB / "SUMMARY.md")
    print("wrote", PUB / "README.md")


if __name__ == "__main__":
    main()
