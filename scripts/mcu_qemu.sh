#!/usr/bin/env bash
# Bare-Metal auf QEMU Cortex-M3 (lm3s6965evb), Semihosting. Kein sudo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/mcu"
TC_ROOT="$ROOT/build/toolchains"
XPACK_GCC="$TC_ROOT/xpack-arm-none-eabi-gcc-14.2.1-1.1"
QVER="9.2.4-1"
QDIR="$TC_ROOT/xpack-qemu-arm-${QVER}"
QURL="https://github.com/xpack-dev-tools/qemu-arm-xpack/releases/download/v${QVER}/xpack-qemu-arm-${QVER}-linux-x64.tar.gz"

CC="${CC:-}"
if [[ -z "$CC" || ! -x "$CC" ]]; then
  if [[ -x "$XPACK_GCC/bin/arm-none-eabi-gcc" ]]; then
    CC="$XPACK_GCC/bin/arm-none-eabi-gcc"
  elif command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    CC="$(command -v arm-none-eabi-gcc)"
  else
    echo "arm-none-eabi-gcc fehlt — bash $ROOT/scripts/mcu_build.sh zuerst" >&2
    exit 1
  fi
fi
export PATH="$(dirname "$CC"):$PATH"

QEMU="${QEMU:-}"
if [[ -z "$QEMU" || ! -x "$QEMU" ]]; then
  if command -v qemu-system-arm >/dev/null 2>&1; then
    QEMU="$(command -v qemu-system-arm)"
  elif [[ -x "$QDIR/bin/qemu-system-arm" ]]; then
    QEMU="$QDIR/bin/qemu-system-arm"
  elif [[ -x "$QDIR/bin/qemu-system-gnuarmeclipse" ]]; then
    QEMU="$QDIR/bin/qemu-system-gnuarmeclipse"
  else
    echo "==> fetch xpack qemu-arm ${QVER}"
    mkdir -p "$TC_ROOT"
    curl -fL --retry 3 -o "$TC_ROOT/xpack-qemu-arm.tar.gz" "$QURL"
    tar -C "$TC_ROOT" -xzf "$TC_ROOT/xpack-qemu-arm.tar.gz"
    if [[ -x "$QDIR/bin/qemu-system-arm" ]]; then
      QEMU="$QDIR/bin/qemu-system-arm"
    else
      QEMU="$QDIR/bin/qemu-system-gnuarmeclipse"
    fi
  fi
fi
echo "QEMU=$QEMU"
echo "CC=$CC"

CFLAGS=(
  -mcpu=cortex-m3 -mthumb -Os
  -ffunction-sections -fdata-sections
  -fno-builtin -ffreestanding -fno-exceptions -Wall
  -I "$ROOT/include" -I "$ROOT/native" -DNEC_LITE_CORE_ONLY -DNEC_LITE_NO_MALLOC -DNEC_SEMIHOST
)
mkdir -p "$OUT"
"$CC" "${CFLAGS[@]}" -c "$ROOT/mcu/startup.c" -o "$OUT/startup_qemu.o"
"$CC" "${CFLAGS[@]}" -c "$ROOT/mcu/runtime.c" -o "$OUT/runtime_qemu.o"
"$CC" "${CFLAGS[@]}" -c "$ROOT/native/nec_frontend.c" -o "$OUT/nec_frontend_qemu.o"
"$CC" "${CFLAGS[@]}" -c "$ROOT/lite/nec_lite.c" -o "$OUT/nec_lite_qemu.o"
"$CC" "${CFLAGS[@]}" -c "$ROOT/mcu/main_nec.c" -o "$OUT/main_nec_qemu.o"
"$CC" -mcpu=cortex-m3 -mthumb -nostdlib -Wl,--gc-sections \
  "-Wl,-Map=$OUT/nec_lite_qemu.map" -T "$ROOT/mcu/link_qemu.ld" \
  -o "$OUT/nec_lite_qemu.elf" \
  "$OUT/startup_qemu.o" "$OUT/runtime_qemu.o" "$OUT/nec_frontend_qemu.o" \
  "$OUT/nec_lite_qemu.o" "$OUT/main_nec_qemu.o" -lgcc

echo "==> qemu $OUT/nec_lite_qemu.elf"
set +e
timeout --signal=KILL 12s "$QEMU" -M lm3s6965evb -nographic -semihosting-config enable=on,target=native \
  -kernel "$OUT/nec_lite_qemu.elf" -d guest_errors 2>&1 | tee "$OUT/qemu-run.txt"
rc=${PIPESTATUS[0]}
set -e
echo "qemu exit=$rc"
if grep -q '^ok=1' "$OUT/qemu-run.txt" 2>/dev/null; then
  echo "QEMU bench OK"
  exit 0
fi
exit "$rc"