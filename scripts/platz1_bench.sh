#!/usr/bin/env bash
# Platz-1-Benches: Host-Ratio+Timing, MCU-Flash, optional QEMU-Zyklen.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/mcu"
mkdir -p "$OUT"

echo "==> 1/4 codec_ratio + host timing"
bash "$ROOT/scripts/codec_ratio.sh"

echo "==> 2/4 deployable ablation (NO_MALLOC) + ROI"
bash "$ROOT/scripts/ablation_deployable.sh"
python3 "$ROOT/scripts/roi_sensor.py"

echo "==> 3/4 MCU flash (M3 + M0)"
MCU_CPU=cortex-m3 bash "$ROOT/scripts/mcu_build.sh"
MCU_CPU=cortex-m0 bash "$ROOT/scripts/mcu_build.sh"

echo "==> 4/4 QEMU Cortex-M3 semihost (falls verfügbar)"
if bash "$ROOT/scripts/mcu_qemu.sh"; then
  python3 - << PY
import json, re, pathlib
out = pathlib.Path("$OUT")
text = (out / "qemu-run.txt").read_text(errors="replace")
rec = {"ok": None, "n": None, "coded": None, "cy_enc": None, "cy_dec": None}
for k in rec:
    m = re.search(rf"^{k}=(\d+)", text, re.M)
    if m:
        rec[k] = int(m.group(1))
rec["source"] = "qemu-lm3s6965evb-semihost"
rec["note"] = "DWT cy_* auf M3; QEMU timing ist Proxy, kein µJ"
(out / "qemu-bench.json").write_text(json.dumps(rec, indent=2) + "\n")
print("wrote", out / "qemu-bench.json", rec)
PY
else
  echo "WARN: QEMU-Bench fehlgeschlagen — Host-Timing bleibt Proxy" >&2
fi

python3 "$ROOT/scripts/platz1_report.py"
echo "OK: $OUT/platz1-report.md"
echo "OK: $OUT/roi-sensor.md"
