#!/usr/bin/env bash
# Cortex-M Cross-Compile: nec_lite vs lz4 vs miniz vs heatshrink, gleiche Flags.
# Erzeugt ELF + .map und schreibt build/mcu-size.json
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCU="$ROOT/mcu"
OUT="$ROOT/build/mcu"
VENDOR="$MCU/vendor"
TC_ROOT="$ROOT/build/toolchains"
mkdir -p "$OUT" "$VENDOR" "$TC_ROOT"

XPACK_VER="14.2.1-1.1"
XPACK_DIR="$TC_ROOT/xpack-arm-none-eabi-gcc-${XPACK_VER}"
XPACK_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${XPACK_VER}/xpack-arm-none-eabi-gcc-${XPACK_VER}-linux-x64.tar.gz"

resolve_cc() {
  if [[ -n "${CC:-}" && -x "${CC}" ]]; then
    return 0
  fi
  if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    CC="$(command -v arm-none-eabi-gcc)"
    return 0
  fi
  if [[ -x "$XPACK_DIR/bin/arm-none-eabi-gcc" ]]; then
    CC="$XPACK_DIR/bin/arm-none-eabi-gcc"
    return 0
  fi
  echo "==> fetch xpack arm-none-eabi-gcc ${XPACK_VER} (user-local, kein sudo)"
  curl -fsSL "$XPACK_URL" -o "$TC_ROOT/xpack.tar.gz"
  tar -C "$TC_ROOT" -xzf "$TC_ROOT/xpack.tar.gz"
  CC="$XPACK_DIR/bin/arm-none-eabi-gcc"
  if [[ ! -x "$CC" ]]; then
    echo "Toolchain fehlt nach Extract: $CC" >&2
    ls -la "$TC_ROOT" >&2
    exit 1
  fi
}

resolve_cc
export PATH="$(dirname "$CC"):$PATH"
echo "CC=$CC"

CPU="${MCU_CPU:-cortex-m3}"

CFLAGS=(
  -mcpu="$CPU"
  -mthumb
  -Os
  -ffunction-sections
  -fdata-sections
  -fno-builtin
  -ffreestanding
  -fno-exceptions
  -Wall
  -I "$ROOT/include"
  -I "$ROOT/native"
  -I "$VENDOR"
  -DNEC_LITE_CORE_ONLY
  -DNEC_LITE_NO_MALLOC
)

fetch() {
  local url="$1" dest="$2"
  if [[ -f "$dest" ]]; then
    return 0
  fi
  echo "fetch $(basename "$dest")"
  curl -fsSL "$url" -o "$dest"
}

fetch "https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.c" "$VENDOR/lz4.c"
fetch "https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.h" "$VENDOR/lz4.h"
# miniz amalgamation (MIT)
fetch "https://raw.githubusercontent.com/richgel999/miniz/3.0.2/miniz.h" "$VENDOR/miniz.h"
fetch "https://raw.githubusercontent.com/richgel999/miniz/3.0.2/miniz.c" "$VENDOR/miniz.c"
HS=https://raw.githubusercontent.com/atomicobject/heatshrink/v0.4.1
fetch "$HS/heatshrink_common.h" "$VENDOR/heatshrink_common.h"
fetch "$HS/heatshrink_encoder.h" "$VENDOR/heatshrink_encoder.h"
fetch "$HS/heatshrink_encoder.c" "$VENDOR/heatshrink_encoder.c"
fetch "$HS/heatshrink_decoder.h" "$VENDOR/heatshrink_decoder.h"
fetch "$HS/heatshrink_decoder.c" "$VENDOR/heatshrink_decoder.c"

compile() {
  local src="$1" obj="$2"
  shift 2
  "$CC" "${CFLAGS[@]}" "$@" -c "$src" -o "$obj"
}

link_elf() {
  local name="$1"
  shift
  "$CC" -mcpu="$CPU" -mthumb -nostdlib \
    -Wl,--gc-sections "-Wl,-Map=$OUT/${name}.map" \
    -T "$MCU/link.ld" \
    -o "$OUT/${name}.elf" \
    "$@" -lgcc
}

echo "==> objects ($CPU -Os -ffreestanding)"
compile "$MCU/startup.c" "$OUT/startup.o"
compile "$MCU/runtime.c" "$OUT/runtime.o"
compile "$ROOT/native/nec_frontend.c" "$OUT/nec_frontend.o"
compile "$ROOT/lite/nec_lite.c" "$OUT/nec_lite.o"
compile "$MCU/main_nec.c" "$OUT/main_nec.o"
compile "$VENDOR/lz4.c" "$OUT/lz4.o" -DLZ4_FREESTANDING=1 \
  -DLZ4_memcpy=memcpy -DLZ4_memset=memset -DLZ4_memmove=memmove \
  -include "$MCU/memdecls.h"
compile "$MCU/main_lz4.c" "$OUT/main_lz4.o"
MINIZ_FLAGS=(
  -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS
  -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_MALLOC -DNDEBUG
)
compile "$VENDOR/miniz.c" "$OUT/miniz.o" "${MINIZ_FLAGS[@]}"
compile "$VENDOR/miniz_tdef.c" "$OUT/miniz_tdef.o" "${MINIZ_FLAGS[@]}"
compile "$VENDOR/miniz_tinfl.c" "$OUT/miniz_tinfl.o" "${MINIZ_FLAGS[@]}"
compile "$MCU/main_miniz.c" "$OUT/main_miniz.o" "${MINIZ_FLAGS[@]}"
compile "$VENDOR/heatshrink_encoder.c" "$OUT/heatshrink_encoder.o"
compile "$VENDOR/heatshrink_decoder.c" "$OUT/heatshrink_decoder.o"
compile "$MCU/hs_stream.c" "$OUT/hs_stream.o"
compile "$MCU/main_heatshrink.c" "$OUT/main_heatshrink.o"

echo "==> link"
link_elf nec_lite "$OUT/startup.o" "$OUT/runtime.o" "$OUT/nec_frontend.o" "$OUT/nec_lite.o" "$OUT/main_nec.o"
link_elf lz4 "$OUT/startup.o" "$OUT/runtime.o" "$OUT/lz4.o" "$OUT/main_lz4.o"
link_elf miniz "$OUT/startup.o" "$OUT/runtime.o" "$OUT/miniz.o" "$OUT/miniz_tdef.o" "$OUT/miniz_tinfl.o" "$OUT/main_miniz.o"
link_elf heatshrink "$OUT/startup.o" "$OUT/runtime.o" "$OUT/heatshrink_encoder.o" "$OUT/heatshrink_decoder.o" "$OUT/hs_stream.o" "$OUT/main_heatshrink.o"

SIZE_BIN="$(dirname "$CC")/arm-none-eabi-size"
NM_BIN="$(dirname "$CC")/arm-none-eabi-nm"
[[ -x "$SIZE_BIN" ]] || SIZE_BIN=size
[[ -x "$NM_BIN" ]] || NM_BIN=nm
echo "==> size"
"$SIZE_BIN" -A "$OUT"/nec_lite.elf "$OUT"/lz4.elf "$OUT"/miniz.elf "$OUT"/heatshrink.elf | tee "$OUT/size.txt"

python3 - << PY
import json, re, subprocess, pathlib
out = pathlib.Path("$OUT")
cpu = "$CPU"
key = "cortex-m0" if cpu == "cortex-m0" else "cortex-m3"
size_bin = "$SIZE_BIN"
nm_bin = "$NM_BIN"
HARNESS = {"src", "dst", "scratch", "back", "g_sink", "coded", "plain", "g_cy_enc", "g_cy_dec", "g_coded", "g_ok"}
LIBSTATE = {"enc", "dec"}

def parse_size_a(elf):
    text = subprocess.check_output([size_bin, "-A", str(elf)], text=True)
    sec = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and re.match(r"^\.\w+", parts[0]):
            try:
                sec[parts[0]] = int(parts[1])
            except ValueError:
                pass
    t = sec.get(".text", 0)
    d = sec.get(".data", 0)
    b = sec.get(".bss", 0)
    return {"text": t, "data": d, "bss": b, "flash": t, "ram": b + d}

def parse_nm(elf):
    raw = subprocess.check_output([nm_bin, "-S", "-B", str(elf)], text=True, errors="replace")
    harness = {}
    lib = {}
    other_bss = 0
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        # addr size type name
        typ = parts[2]
        if typ not in ("B", "b", "C"):
            continue
        try:
            sz = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[3]
        if name in HARNESS:
            harness[name] = sz
        elif name in LIBSTATE:
            lib[name] = sz
        else:
            other_bss += sz
    return {
        "harness_buffers": harness,
        "library_state": lib,
        "library_state_bytes": sum(lib.values()),
        "other_bss": other_bss,
    }

names = ("nec_lite", "lz4", "miniz", "heatshrink")
images = {}
for name in names:
    elf = out / f"{name}.elf"
    rec = parse_size_a(elf)
    rec.update(parse_nm(elf))
    rec["elf_bytes"] = elf.stat().st_size
    images[name] = rec

path = out / "mcu-size.json"
report = {}
if path.exists():
    try:
        report = json.loads(path.read_text())
    except json.JSONDecodeError:
        report = {}
report["note"] = (
    "arm-none-eabi-gcc 14.2.1 -Os -mthumb -ffreestanding -ffunction-sections --gc-sections. "
    "nec_lite stream harness n=2048 + enc/dec BSS; peers still 128B block. size -A .text is Flash (no libc)."
)
report["toolchain"] = "xpack-arm-none-eabi-gcc-14.2.1-1.1"
report["harness_payload_bytes"] = 2048
report["heatshrink"] = {
    "version": "0.4.1",
    "window_bits": 8,
    "lookahead_bits": 4,
    "dynamic_alloc": 0,
    "use_index": 1,
}
report["ram_model"] = {
    "nec_lite": "stream: caller-owned nec_lite_enc_t+dec_t (~1.4KiB each); scratch_bound=0; FLAG_STREAM records; n does not scale library RAM",
    "lz4": "block: src=n + dst=LZ4_compressBound(n) + back=n; library BSS=0",
    "miniz": "block: src=n + dst + back; library may keep probes in BSS/stack",
    "heatshrink": "stream: constant encoder+decoder window (2^W); IO in arbitrary chunks",
}
report[key] = images
path.write_text(json.dumps(report, indent=2) + "\n")
print("wrote", path, "key", key)
for k, v in images.items():
    print(
        f"  {k:10s}  flash={v['flash']:6d}  bss={v['bss']:6d}  "
        f"lib_state={v['library_state_bytes']:5d}  harness={sum(v['harness_buffers'].values()):5d}"
    )
PY
