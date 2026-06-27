#!/usr/bin/env bash
#
# Compile the app into a loadable relocatable Xtensa object (data/app.elf) that
# the esp32-elf host loads at runtime from LittleFS. Run this, then:
#
#     pio run -e esp32-elf -t uploadfs        # flash the filesystem image
#     pio run -e esp32-elf -t upload -t monitor
#
# The flag combination matters and matches what the loader was verified against:
#   -c                       produce a relocatable object (ET_REL), not an exe
#   -mlongcalls              calls go via l32r literals (no out-of-range PC calls)
#   -mtext-section-literals  keep literal pools inside .text -> one contiguous
#                            block, so SLOT0_OP relocs stay intra-section no-ops
#   -fno-common              give .bss real symbols (loader rejects SHN_COMMON)
#   -fno-exceptions/-rtti/-stack-protector  drop runtime deps the app doesn't need
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
XG="$(ls "$HOME"/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++ 2>/dev/null | head -1 || true)"

if [ -z "$XG" ]; then
    echo "xtensa-esp32-elf-g++ not found. Build any esp32 env once so PlatformIO"
    echo "installs the toolchain:  pio run -e esp32-serial"
    exit 1
fi

mkdir -p "$ROOT/data"
OUT="$ROOT/data/app.elf"

# Note: NO -ffunction-sections here on purpose -- keeping all code in a single
# .text means there are never cross-section PC-relative calls for the loader to
# patch; code->rodata/bss refs are absolute (R_XTENSA_32), which it handles.
"$XG" -c -Os \
    -mlongcalls -mtext-section-literals \
    -fno-rtti -fno-exceptions -fno-stack-protector -fno-common \
    -I"$ROOT/include" \
    "$ROOT/src/app.cpp" -o "$OUT"

echo "built $OUT"
"$(dirname "$XG")/xtensa-esp32-elf-size" "$OUT" 2>/dev/null || true
echo
echo "next:  pio run -e esp32-elf -t uploadfs && pio run -e esp32-elf -t upload -t monitor"
