#!/usr/bin/env bash
#
# Compile a Flipper app (compat/apps/*.c) into a loadable Xtensa object at
# data/app.elf, for the esp32-flipper / cardputer-flipper host firmware.
#
#   loader/build_flipper_app.sh            # default: classic ESP32 (LX6)
#   loader/build_flipper_app.sh s3         # ESP32-S3 (LX7), e.g. Cardputer
#   loader/build_flipper_app.sh esp32 compat/apps/hello_world.c
#
# Same flag rationale as build_esp32_app.sh; see ../INFO.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-esp32}"
APP="${2:-$ROOT/compat/apps/hello_world.c}"

case "$TARGET" in
    esp32) PKG=toolchain-xtensa-esp32;   PREFIX=xtensa-esp32-elf ;;
    s3)    PKG=toolchain-xtensa-esp32s3; PREFIX=xtensa-esp32s3-elf ;;
    *) echo "usage: $0 [esp32|s3] [app.c]"; exit 1 ;;
esac

XCC="$(ls "$HOME/.platformio/packages/$PKG/bin/$PREFIX-gcc" 2>/dev/null | head -1 || true)"
if [ -z "$XCC" ]; then
    echo "$PREFIX-gcc not found. Build the matching env once so PlatformIO installs it:"
    [ "$TARGET" = s3 ] && echo "  pio run -e cardputer-flipper" || echo "  pio run -e esp32-flipper"
    exit 1
fi

mkdir -p "$ROOT/data"
OUT="$ROOT/data/app.elf"

"$XCC" -c -Os \
    -mlongcalls -mtext-section-literals \
    -fno-common \
    -I"$ROOT/compat/include" -I"$ROOT/include" \
    "$APP" -o "$OUT"

echo "built $OUT  ($TARGET)  from $(basename "$APP")"
"$(dirname "$XCC")/$PREFIX-size" "$OUT" 2>/dev/null || true
echo
echo "next:"
[ "$TARGET" = s3 ] \
    && echo "  pio run -e cardputer-flipper -t upload -t uploadfs -t monitor" \
    || echo "  pio run -e esp32-flipper -t upload -t uploadfs -t monitor"
