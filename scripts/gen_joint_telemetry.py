#!/usr/bin/env python3
"""Synthetische 7-Achsen-Gelenktelemetrie @ 1 kHz (Robotik-Proxy).

Layout: interlaced int16 LE, 7 channels × N samples.
Einheiten: 0.01° (Winkel). Trajektorien = glatte Sinusoiden + kleines Rauschen
→ Delta/Bitpack-freundlich (nicht Torque-Rauschen).

Kein echter Roboter-Log — Methodik in docs/BENCH.md.
"""
from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hz", type=int, default=1000)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--axes", type=int, default=7)
    ap.add_argument(
        "-o",
        type=Path,
        default=ROOT / "data" / "joint_7axis_1khz_10s.i16le",
    )
    args = ap.parse_args()

    n = int(args.hz * args.seconds)
    axes = args.axes
    # Phasen / Amplituden (0.01°)
    amp = [4500, 3200, 2800, 2100, 1500, 900, 600][:axes]
    freq = [0.35, 0.55, 0.40, 0.70, 0.90, 1.10, 1.40][:axes]
    phase = [0.0, 0.7, 1.2, 2.1, 0.3, 1.8, 2.6][:axes]

    out = bytearray()
    # Planar: alle Samples Achse 0, dann Achse 1, … — Delta-freundlich.
    # (Interleaved wäre bus-üblich, aber schlecht für 1D-Delta; Profile-Thema.)
    for a in range(axes):
        for i in range(n):
            t = i / float(args.hz)
            noise = ((i * 17 + a * 31) % 5) - 2
            v = int(round(amp[a] * math.sin(2 * math.pi * freq[a] * t + phase[a]))) + noise
            if v > 32767:
                v = 32767
            if v < -32768:
                v = -32768
            out += struct.pack("<h", v)

    args.o.parent.mkdir(parents=True, exist_ok=True)
    args.o.write_bytes(out)
    meta = args.o.with_suffix(".json")
    meta.write_text(
        __import__("json").dumps(
            {
                "layout": "planar_per_axis",
                "axes": axes,
                "hz": args.hz,
                "seconds": args.seconds,
                "samples_per_axis": n,
                "dtype": "int16_le",
                "unit": "0.01_deg",
                "note": "Synthetic robotics proxy — not a real robot capture.",
            },
            indent=2,
        )
        + "\n"
    )
    print(
        f"wrote {args.o} bytes={len(out)} samples/axis={n} axes={axes} "
        f"rate={args.hz}Hz duration={args.seconds}s layout=planar"
    )
    print(f"wrote {meta}")


if __name__ == "__main__":
    main()
