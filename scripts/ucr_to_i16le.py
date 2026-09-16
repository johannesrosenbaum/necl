#!/usr/bin/env python3
"""UCR TRAIN+TEST → int16 LE, min/max linear auf [-32767, 32767]. Erstes Feld = Klasse, skip."""
from __future__ import annotations

import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
UCR = DATA / "ucr"
SERIES = ("ECG200", "Coffee", "ItalyPowerDemand", "MoteStrain", "GunPoint")


def load_split(path: pathlib.Path) -> list[float]:
    vals: list[float] = []
    if not path.is_file():
        return vals
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        for tok in parts[1:]:
            vals.append(float(tok))
    return vals


def main() -> int:
    DATA.mkdir(parents=True, exist_ok=True)
    if not UCR.is_dir():
        print("ucr_to_i16le: no", UCR, file=sys.stderr)
        return 1
    n_ok = 0
    for name in SERIES:
        vals: list[float] = []
        for split in ("TRAIN", "TEST"):
            vals.extend(load_split(UCR / name / f"{name}_{split}.txt"))
        if not vals:
            print(f"  skip {name}: empty")
            continue
        mn = min(vals)
        mx = max(vals)
        span = mx - mn if mx != mn else 1.0
        out = []
        for v in vals:
            q = int(round((v - mn) / span * 65534.0 - 32767.0))
            if q > 32767:
                q = 32767
            if q < -32767:
                q = -32767
            out.append(q)
        dest = DATA / f"ucr_{name}.i16le"
        dest.write_bytes(struct.pack("<" + "h" * len(out), *out))
        print(f"  {dest.name}: n={len(out)} min={mn:.4g} max={mx:.4g}")
        n_ok += 1
    print(f"ucr_to_i16le: {n_ok}/{len(SERIES)}")
    return 0 if n_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
