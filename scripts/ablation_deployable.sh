#!/usr/bin/env bash
# Deploybare Ratio: nec_lite mit -DNEC_LITE_NO_MALLOC (Board-Pfad).
# Parallel Host-FE_I16 (Gateway) für dieselbe Corpus-Liste — klar etikettiert.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/mcu"
CC="${CC:-/usr/bin/gcc}"
mkdir -p "$OUT"
export NEC_DATA_DIR="${NEC_DATA_DIR:-$ROOT/data}"

CFLAGS=(
  -O2 -std=c11 -Wall -Wextra
  -DNEC_LITE_CORE_ONLY
  -I "$ROOT/include" -I "$ROOT/native" -I "$ROOT/lite"
)

echo "==> ablation deployable (NO_MALLOC)"
"$CC" "${CFLAGS[@]}" -DNEC_LITE_NO_MALLOC \
  "$ROOT/lite/ablation_deployable.c" \
  "$ROOT/lite/nec_lite.c" \
  "$ROOT/native/nec_frontend.c" \
  -lm -o "$OUT/ablation_nomalloc"
"$OUT/ablation_nomalloc" | tee "$OUT/ablation-nomalloc.txt"

echo "==> ablation host (choose-best, inkl. Kompakt/LZ falls greift)"
"$CC" "${CFLAGS[@]}" \
  "$ROOT/lite/ablation_deployable.c" \
  "$ROOT/lite/nec_lite.c" \
  "$ROOT/native/nec_frontend.c" \
  -lm -o "$OUT/ablation_host"
"$OUT/ablation_host" | tee "$OUT/ablation-host.txt"

python3 - <<PY
import json, pathlib, time
out = pathlib.Path(r"$OUT")
root = pathlib.Path(r"$ROOT")

def parse(path, label):
    rows = []
    nomalloc = None
    for line in path.read_text().splitlines():
        if line.startswith("# nomalloc="):
            nomalloc = int(line.split("=", 1)[1])
            continue
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 6:
            continue
        rows.append({
            "corpus": parts[0],
            "frontend": int(parts[1]),
            "orig": int(parts[2]),
            "coded": int(parts[3]),
            "ratio_pct": float(parts[4]),
            "mark": parts[5],
        })
    return {"label": label, "deployable": bool(nomalloc), "nomalloc": nomalloc, "rows": rows}

payload = {
    "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
    "note": (
        "deployable = Stream Bitpack/Zero/STORE under NEC_LITE_NO_MALLOC; "
        "host = FE_I16 choose-best (may use 0xCE/0xCF/0xCB or stream+RC)."
    ),
    "deployable": parse(out / "ablation-nomalloc.txt", "NO_MALLOC stream"),
    "host": parse(out / "ablation-host.txt", "host choose-best"),
}
(out / "ablation-deployable.json").write_text(json.dumps(payload, indent=2) + "\n")
print("wrote", out / "ablation-deployable.json")

# Kurz-Diff deploybar vs host
dep = {r["corpus"]: r for r in payload["deployable"]["rows"]}
hst = {r["corpus"]: r for r in payload["host"]["rows"]}
print(f"{'corpus':<32} {'deploy%':>8} {'host%':>8} {'Δpp':>7}")
for c in dep:
    d, h = dep[c]["ratio_pct"], hst.get(c, {}).get("ratio_pct")
    if h is None:
        print(f"{c:<32} {d:7.1f}% {'—':>8} {'—':>7}")
    else:
        print(f"{c:<32} {d:7.1f}% {h:7.1f}% {h-d:+6.1f}")
PY
