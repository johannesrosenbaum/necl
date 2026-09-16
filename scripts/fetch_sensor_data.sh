#!/usr/bin/env bash
# Echte Sensor-Corpora → nec-core/data/*.i16le (int16 LE, Temperatur/CPU skaliert).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/data"
mkdir -p "$DATA"
export NEC_DATA="$DATA"

fetch() {
  local url="$1" dest="$2"
  if [[ -f "$dest" ]]; then
    return 0
  fi
  echo "fetch $(basename "$dest")"
  curl -fL --retry 3 --max-time 60 -o "$dest" "$url"
}

NAB=https://raw.githubusercontent.com/numenta/NAB/master/data
fetch "$NAB/realKnownCause/machine_temperature_system_failure.csv" "$DATA/nab_machine_temp.csv"
fetch "$NAB/realKnownCause/ambient_temperature_system_failure.csv" "$DATA/nab_ambient_temp.csv"
fetch "$NAB/realAWSCloudwatch/ec2_cpu_utilization_24ae8d.csv" "$DATA/nab_ec2_cpu.csv"
fetch "https://raw.githubusercontent.com/jbrownlee/Datasets/master/daily-min-temperatures.csv" "$DATA/melbourne_daily_min_temp.csv"

INTEL_GZ="$DATA/intel_lab_data.txt.gz"
if [[ ! -f "$DATA/intel_lab_temp.i16le" ]]; then
  echo "fetch intel berkeley lab (data.txt.gz, ~12 MiB)"
  curl -fL --retry 3 -o "$INTEL_GZ" "http://db.csail.mit.edu/labdata/data.txt.gz" || \
    echo "WARN: Intel-Lab nicht geladen (Netz), NAB reicht als echte Sensorreihe" >&2
fi

python3 - << 'PY'
import csv, gzip, os, struct, pathlib
root = pathlib.Path(os.environ.get("NEC_DATA", "."))
data = root

def csv_value_col(path, scale, out_name, skip_header=True, col=-1):
    vals = []
    with open(path, newline="") as f:
        r = csv.reader(f)
        if skip_header:
            next(r, None)
        for row in r:
            if not row:
                continue
            try:
                x = float(row[col])
            except (ValueError, IndexError):
                continue
            v = int(round(x * scale))
            if v > 32767:
                v = 32767
            if v < -32767:
                v = -32767
            vals.append(v)
    out = data / out_name
    with open(out, "wb") as f:
        f.write(struct.pack("<" + "h" * len(vals), *vals))
    print(f"  {out.name}: {len(vals)} samples ({len(vals)*2} bytes) scale={scale}")

csv_value_col(data / "nab_machine_temp.csv", 100.0, "nab_machine_temp.i16le")
csv_value_col(data / "nab_ambient_temp.csv", 100.0, "nab_ambient_temp.i16le")
csv_value_col(data / "nab_ec2_cpu.csv", 100.0, "nab_ec2_cpu.i16le")
# Melbourne: Date,Temp  — already °C
csv_value_col(data / "melbourne_daily_min_temp.csv", 100.0, "melbourne_daily_min_temp.i16le")

gz = data / "intel_lab_data.txt.gz"
out = data / "intel_lab_temp.i16le"
if gz.exists() and gz.stat().st_size > 1000 and not out.exists():
    n = 0
    cap = 400000  # ~800 KiB i16, genug für Ratio, nicht das volle 2.3M
    with gzip.open(gz, "rt", errors="replace") as f, open(out, "wb") as w:
        for line in f:
            parts = line.split()
            if len(parts) < 5:
                continue
            try:
                temp = float(parts[4])
            except ValueError:
                continue
            v = int(round(temp * 100.0))
            if v > 32767:
                v = 32767
            if v < -32767:
                v = -32767
            w.write(struct.pack("<h", v))
            n += 1
            if n >= cap:
                break
    print(f"  {out.name}: {n} samples ({n*2} bytes) from Intel Berkeley (temp*100, cap={cap})")
elif out.exists():
    print(f"  {out.name}: keep existing {out.stat().st_size} bytes")
else:
    print("  intel_lab_temp.i16le: skipped")
PY