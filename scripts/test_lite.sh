#!/usr/bin/env bash
# Mojo-freier Lite-Pfad: static lib, Gateway, Tests, lz4/zstd-1 Bench.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"

CC="${CC:-/usr/bin/gcc}"
CFLAGS="-O2 -std=c11 -D_POSIX_C_SOURCE=200809L -DNEC_LITE_CORE_ONLY -Wall -Wextra -I $ROOT/include -I $ROOT/native"

echo "==> libnec_lite.a (no Mojo)"
$CC $CFLAGS -c "$ROOT/native/nec_frontend.c" -o "$ROOT/build/nec_frontend.o"
$CC $CFLAGS -c "$ROOT/lite/nec_lite.c" -o "$ROOT/build/nec_lite.o"
ar rcs "$ROOT/build/libnec_lite.a" "$ROOT/build/nec_lite.o" "$ROOT/build/nec_frontend.o"

echo "==> test_nec_lite"
$CC $CFLAGS "$ROOT/tests/test_nec_lite.c" "$ROOT/build/libnec_lite.a" -o "$ROOT/build/test_nec_lite"
"$ROOT/build/test_nec_lite"

echo "==> nec-gateway"
$CC $CFLAGS "$ROOT/lite/nec_gateway.c" "$ROOT/build/libnec_lite.a" -o "$ROOT/build/nec-gateway"
strip -o "$ROOT/build/nec-gateway.stripped" "$ROOT/build/nec-gateway"
echo -n "  nec-gateway stripped: "
wc -c < "$ROOT/build/nec-gateway.stripped" | tr -d ' '
echo " bytes"
echo -n "  ldd (must not contain KGEN/AsyncRT): "
if ldd "$ROOT/build/nec-gateway" | grep -E 'KGEN|AsyncRT|MSupport' >/dev/null; then
  echo "FAIL: Mojo runtime linked"
  ldd "$ROOT/build/nec-gateway"
  exit 1
fi
echo "clean"

echo "==> gateway CLI roundtrip"
python3 - << 'PY'
import struct, random, pathlib
root = pathlib.Path("/tmp")
# tiny i16 sequence
buf = bytearray()
x = 0
random.seed(1)
for _ in range(200):
    x += random.randint(-3, 3)
    buf += struct.pack("<h", x)
pathlib.Path("/tmp/nec_gw_i16.bin").write_bytes(buf)
PY
"$ROOT/build/nec-gateway" compress --frontend i16 --input /tmp/nec_gw_i16.bin --output /tmp/nec_gw_i16.nec
"$ROOT/build/nec-gateway" decompress --input /tmp/nec_gw_i16.nec --output /tmp/nec_gw_i16.out
cmp -s /tmp/nec_gw_i16.bin /tmp/nec_gw_i16.out
echo "  gateway i16 CLI OK"

echo "==> lite vs lz4 vs zstd-1"
$CC $CFLAGS -O2 "$ROOT/lite/nec_lite_bench.c" "$ROOT/build/libnec_lite.a" \
  /usr/lib64/liblz4.so.1 -lzstd -lm -o "$ROOT/build/nec_lite_bench"
"$ROOT/build/nec_lite_bench" | tee "$ROOT/build/lite-bench.txt"

echo "==> ratio vs heatshrink/lz4/miniz"
bash "$ROOT/scripts/codec_ratio.sh"

echo "==> object sizes"
size "$ROOT/build/nec_lite.o" "$ROOT/build/nec_frontend.o" "$ROOT/build/nec-gateway.stripped" || true
echo "lite ALL PASS"
