#!/usr/bin/env bash
# ============================================================================
#  build.sh  -  configure and build BadgeBoy.
#
#  Usage:
#    ./build.sh /abs/path/to/game.gbc [display_mode]
#
#    display_mode (optional): fullscreen (default) | aspect | centered
#
#  Examples:
#    ./build.sh ~/roms/game.gbc
#    ./build.sh ~/roms/game.gbc aspect
#
#  Output: build/badgeboy.uf2
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SDK_DIR="${PICO_SDK_PATH:-$HOME/pico-sdk}"

ROM="${1:-${GBC_ROM:-}}"
MODE_RAW="${2:-fullscreen}"
MODE="$(printf '%s' "$MODE_RAW" | tr '[:lower:]' '[:upper:]')"

if [[ -z "$ROM" ]]; then
  echo "usage: ./build.sh /abs/path/to/game.gbc [fullscreen|aspect|centered]" >&2
  exit 1
fi
if [[ ! -f "$ROM" ]]; then
  echo "error: ROM not found: $ROM" >&2
  exit 1
fi
ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"   # absolutise

echo "==> 1/4  ARM toolchain"
if ! command -v arm-none-eabi-gcc >/dev/null; then
  echo "    arm-none-eabi-gcc not found. Install it first:" >&2
  echo "      sudo apt install -y gcc-arm-none-eabi cmake ninja-build build-essential python3" >&2
  exit 1
fi

echo "==> 2/4  Pico SDK ($SDK_DIR)"
if [[ ! -d "$SDK_DIR" ]]; then
  git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
  git -C "$SDK_DIR" submodule update --init --depth 1
fi
export PICO_SDK_PATH="$SDK_DIR"

echo "==> 3/4  Configure (ROM=$ROM, display=$MODE)"
cmake -S "$HERE" -B "$HERE/build" -G Ninja \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_BOARD=pimoroni_pico_plus2_rp2350 \
  -DGBC_ROM="$ROM" \
  -DGBC_DISPLAY_MODE="$MODE"

echo "==> 4/4  Build"
cmake --build "$HERE/build" -j

echo
echo "DONE -> $HERE/build/badgeboy.uf2"
echo "Flash: hold BOOTSEL, tap RESET, then copy badgeboy.uf2 onto the RP2350 drive."
echo "Note: this replaces MonaOS. See docs/FLASHING.md to restore it."
