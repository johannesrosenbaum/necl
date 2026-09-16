#!/usr/bin/env bash
# Host-Ratio: nec_lite vs heatshrink vs lz4 vs miniz vs DRH vs Sprintz-Delta.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build"
VENDOR="$ROOT/mcu/vendor"
CC="${CC:-/usr/bin/gcc}"
mkdir -p "$OUT" "$OUT/mcu"

CFLAGS=(
  -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra
  -Wno-implicit-fallthrough
  -DNEC_LITE_CORE_ONLY
  -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS
  -DMINIZ_NO_ARCHIVE_WRITING_APIS -DNDEBUG
  -I "$ROOT/include" -I "$ROOT/lite" -I "$ROOT/native" -I "$ROOT/mcu" -I "$VENDOR"
)

echo "==> codec_ratio (host)"
DATA_DIR="$ROOT/data"
if [[ ! -f "$DATA_DIR/nab_machine_temp.i16le" ]]; then
  bash "$ROOT/scripts/fetch_sensor_data.sh" || echo "WARN: Sensor-Fetch fehlgeschlagen" >&2
fi
python3 "$ROOT/scripts/ucr_to_i16le.py" || echo "WARN: UCR-Konvertierung fehlgeschlagen" >&2
"$CC" "${CFLAGS[@]}" \
  "$ROOT/lite/codec_ratio.c" \
  "$ROOT/lite/nec_lite.c" \
  "$ROOT/lite/drh.c" \
  "$ROOT/lite/sprintz_delta.c" \
  "$ROOT/native/nec_frontend.c" \
  "$ROOT/mcu/hs_stream.c" \
  "$VENDOR/heatshrink_encoder.c" \
  "$VENDOR/heatshrink_decoder.c" \
  "$VENDOR/lz4.c" \
  "$VENDOR/miniz.c" \
  "$VENDOR/miniz_tdef.c" \
  "$VENDOR/miniz_tinfl.c" \
  -lm -o "$OUT/codec_ratio"

export NEC_DATA_DIR="$DATA_DIR"
"$OUT/codec_ratio" | tee "$OUT/mcu/ratio.txt"
python3 - << PY
import json, pathlib
root = pathlib.Path(r"$OUT/mcu")
rows = []
for line in (root / "ratio.txt").read_text().splitlines()[1:]:
    parts = line.split()
    if len(parts) >= 5:
        rows.append({
            "corpus": parts[0], "codec": parts[1],
            "orig": int(parts[2]), "coded": int(parts[3]),
            "ratio": parts[4],
        })
payload = {"rows": rows}
size_path = root / "mcu-size.json"
if size_path.exists():
    size = json.loads(size_path.read_text())
    m0 = size.get("cortex-m0", {})
    totals = []
    for r in rows:
        if r["corpus"].endswith("16k") and r["codec"] in m0:
            f = m0[r["codec"]]["flash"]
            totals.append({**r, "flash_m0": f, "flash_plus_coded": f + r["coded"]})
    payload["flash_plus_payload_m0"] = totals
(root / "ratio.json").write_text(json.dumps(payload, indent=2) + "\n")
print("wrote", root / "ratio.json")
PY
echo "wrote $OUT/mcu/ratio.txt"
