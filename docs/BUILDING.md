# Building

## Prerequisites

- An ARM bare-metal toolchain (`arm-none-eabi-gcc`) and standard build tools.
- CMake 3.13 or newer, Ninja, Python 3.
- Git, to fetch the Pico SDK.

On Debian or Ubuntu:

```bash
sudo apt update
sudo apt install -y gcc-arm-none-eabi cmake ninja-build build-essential python3
```

## One-line build

```bash
./build.sh /absolute/path/to/game.gbc [fullscreen|aspect|centered]
```

`build.sh` will:

1. Verify the toolchain is present.
2. Clone the Pico SDK into `~/pico-sdk` on first run (set `PICO_SDK_PATH` to use
   an existing checkout).
3. Configure with your ROM and display mode.
4. Build `build/badgeboy.uf2`.

## Manual build

```bash
export PICO_SDK_PATH=~/pico-sdk    # an existing SDK checkout

cmake -S . -B build -G Ninja \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_BOARD=pimoroni_pico_plus2_rp2350 \
  -DGBC_ROM=/absolute/path/to/game.gbc \
  -DGBC_DISPLAY_MODE=FULLSCREEN

cmake --build build -j
```

## Build options

| Option              | Values                              | Default     |
|---------------------|-------------------------------------|-------------|
| `GBC_ROM`           | absolute path to a `.gb` or `.gbc`  | required    |
| `GBC_DISPLAY_MODE`  | `FULLSCREEN`, `ASPECT`, `CENTERED`  | `FULLSCREEN`|
| `PICO_PLATFORM`     | `rp2350-arm-s`                      | set by build |
| `PICO_BOARD`        | `pimoroni_pico_plus2_rp2350`        | set by build |

The ROM is converted to `build/generated/gb_rom.h` by `tools/rom2c.py` and
embedded in flash. That header and the built image contain copyrighted data and
are excluded from version control.

## Notes on the chip and SDK

- The board must be an RP2350B variant. The display data bus is on GPIO 32-39,
  which the RP2350A does not have. `pimoroni_pico_plus2_rp2350` is an RP2350B
  board; all pins are set explicitly in `src/config.h`, so the board file's own
  defaults do not matter.
- Pico SDK 2.x names the platform `rp2350-arm-s`. The plain value `rp2350` fails
  during configure with a missing `rp2350.cmake`.

## Rebuilding after changes

```bash
cmake --build build -j
```

To change the ROM or display mode, re-run `build.sh` or re-configure with new
`-D` values. Changing `GBC_ROM` triggers regeneration of the embedded header.
