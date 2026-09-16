#!/usr/bin/env bash
# ELF nach STM32-ähnlich flashen, falls ein Probe hängt. Kein sudo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-$ROOT/build/mcu/nec_lite.elf}"
BIN="${ELF%.elf}.bin"

if [[ ! -f "$ELF" ]]; then
  echo "kein ELF: $ELF  — zuerst: bash $ROOT/scripts/mcu_build.sh" >&2
  exit 1
fi

OBJCOPY="$(dirname "$(command -v arm-none-eabi-gcc 2>/dev/null || true)")/arm-none-eabi-objcopy"
if [[ ! -x "$OBJCOPY" ]]; then
  OBJCOPY="$ROOT/build/toolchains/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin/arm-none-eabi-objcopy"
fi
if [[ -x "$OBJCOPY" ]]; then
  "$OBJCOPY" -O binary "$ELF" "$BIN"
  echo "bin $BIN"
fi

echo "==> USB / Probe"
if command -v lsusb >/dev/null 2>&1; then
  lsusb || true
fi
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "kein /dev/ttyACM|USB"

flash_ok=0
if command -v st-flash >/dev/null 2>&1 && [[ -f "$BIN" ]]; then
  echo "==> st-flash write $BIN 0x8000000"
  if st-flash write "$BIN" 0x8000000; then
    flash_ok=1
  fi
fi
if [[ "$flash_ok" -eq 0 ]] && command -v probe-rs >/dev/null 2>&1; then
  echo "==> probe-rs download $ELF"
  if probe-rs download "$ELF" --chip STM32F103C8; then
    flash_ok=1
  fi
fi
if [[ "$flash_ok" -eq 0 ]] && command -v openocd >/dev/null 2>&1; then
  echo "==> openocd stm32f1x"
  if openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
      -c "program $ELF verify reset exit"; then
    flash_ok=1
  fi
fi

if [[ "$flash_ok" -eq 0 ]]; then
  echo "kein ST-Link/J-Link/CMSIS-DAP gefunden — nicht geflasht."
  echo "ELF liegt bereit: $ELF"
  exit 2
fi
echo "flash OK"
