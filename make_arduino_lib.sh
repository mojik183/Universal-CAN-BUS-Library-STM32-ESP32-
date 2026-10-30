#!/bin/sh
# @author: mojik183
# @github: github.com/mojik183
# @ai assistant: claude.ai
# @date: 2026-10-29
# @version: 0.1.0
# make_arduino_lib.sh - build an Arduino library folder (UniversalCAN) from this source tree.
#
#   sh make_arduino_lib.sh            creates ./UniversalCAN
#   sh make_arduino_lib.sh /path/out  creates /path/out/UniversalCAN
#
# Copy the created folder into your Arduino "libraries" folder (File > Preferences shows the sketchbook location)
# and restart the Arduino IDE. The default can_cfg.h of the package is set for an ESP32 with the TWAI backend;
# edit libraries/UniversalCAN/src/can_cfg.h to change it.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-.}/UniversalCAN
VERSION=$(sed -n 's/^#define CAN_LIB_VERSION_MAJOR  *\([0-9]*\).*/\1/p' "$HERE/can_cfg.h").$(sed -n 's/^#define CAN_LIB_VERSION_MINOR  *\([0-9]*\).*/\1/p' "$HERE/can_cfg.h").$(sed -n 's/^#define CAN_LIB_VERSION_PATCH  *\([0-9]*\).*/\1/p' "$HERE/can_cfg.h")

rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/examples/CanLoopback"
cp "$HERE"/can_*.c "$HERE"/can_*.h "$OUT/src/"
rm -f "$OUT/src/can_platform_stm32.c" "$OUT/src/can_stm32_bxcan.c" "$OUT/src/can_stm32_fdcan.c" "$OUT/src/can_port_virtual.c"
cp "$HERE/arduino_example/CanLoopback.ino" "$OUT/examples/CanLoopback/"
cp "$HERE/README.md" "$OUT/README.md"

# ESP32 defaults for the package: TWAI on, other backends off (switch MCP on in can_cfg.h if you use it).
sed -i 's|^\(#define CAN_CFG_PLATFORM  *\).*|\1CAN_PLATFORM_ESP32|;
        s|^\(#define CAN_CFG_ENABLE_BACKEND_STM32_BXCAN  *\)[0-9]|\10|;
        s|^\(#define CAN_CFG_ENABLE_BACKEND_MCP25XX  *\)[0-9]|\10|;
        s|^\(#define CAN_CFG_ENABLE_BACKEND_ESP32_TWAI  *\)[0-9]|\11|' "$OUT/src/can_cfg.h"

cat > "$OUT/library.properties" <<PROP
name=UniversalCAN
version=$VERSION
author=mojik183
maintainer=mojik183
sentence=Universal CAN / CAN FD library: ESP32 TWAI, STM32, MCP2515, MCP2517FD, MCP2518FD.
paragraph=One umbrella header, all settings in can_cfg.h, watch-window debugging, optional FreeRTOS. Arduino support is for ESP32 (Arduino-ESP32 core) and is not tested on real boards.
category=Communication
url=https://github.com/mojik183
architectures=esp32
includes=can_lib.h
PROP
echo "Created $OUT (version $VERSION)"
