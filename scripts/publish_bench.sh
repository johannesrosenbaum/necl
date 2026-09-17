#!/usr/bin/env bash
# Erzeugt GitHub-fähige Bench-Artefakte unter bench/published/
# (deploybar vs Host klar getrennt; kein stilles Vermischen).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/mcu"
PUB="$ROOT/bench/published"
CC="${CC:-/usr/bin/gcc}"
mkdir -p "$PUB" "$OUT" "$ROOT/data"

echo "==> joint telemetry corpus"
python3 "$ROOT/scripts/gen_joint_telemetry.py"

echo "==> deployable ablation"
bash "$ROOT/scripts/ablation_deployable.sh"

echo "==> host ratio suite (peers)"
bash "$ROOT/scripts/codec_ratio.sh"

echo "==> ROI (deployable ambient)"
python3 "$ROOT/scripts/roi_sensor.py"

echo "==> MCU flash (falls Toolchain da)"
if MCU_CPU=cortex-m3 bash "$ROOT/scripts/mcu_build.sh"; then
  MCU_CPU=cortex-m0 bash "$ROOT/scripts/mcu_build.sh" || true
else
  echo "WARN: mcu_build fehlgeschlagen — Flash-Zahlen ggf. stale" >&2
fi

python3 "$ROOT/scripts/platz1_report.py" >/dev/null

# Snapshot fürs Repo
cp -f "$OUT/ablation-deployable.json" "$PUB/"
cp -f "$OUT/ratio.json" "$PUB/"
cp -f "$OUT/platz1-report.md" "$PUB/"
cp -f "$OUT/platz1-report.json" "$PUB/"
cp -f "$OUT/roi-sensor.md" "$PUB/" 2>/dev/null || true
cp -f "$OUT/roi-sensor.json" "$PUB/" 2>/dev/null || true
[[ -f "$OUT/mcu-size.json" ]] && cp -f "$OUT/mcu-size.json" "$PUB/"
[[ -f "$OUT/host-timing.json" ]] && cp -f "$OUT/host-timing.json" "$PUB/"

python3 "$ROOT/scripts/render_bench_summary.py"

echo "OK: $PUB/README.md"
echo "OK: $PUB/SUMMARY.md"
