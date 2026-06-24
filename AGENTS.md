# AGENTS.md

Working context for contributors and coding agents. This file records the
decisions, hardware facts, and pitfalls discovered while bringing BadgeBoy up,
so that future work does not have to rediscover them.

## What this project is

Bare-metal RP2350 firmware that runs the Peanut-GB Game Boy / Game Boy Color
core on the GitHub Universe 2025 badge. The badge is a custom Pimoroni Tufty
2350. The firmware replaces the stock MonaOS MicroPython image.

## Hardware facts (verified)

The pin map was read from the badge's own MicroPython firmware with
`tools/dump_pinout.py` (calls `machine.Pin.board`). Do not substitute the stock
Tufty 2040/2350 pinout; this custom board differs.

- Chip: RP2350B (48-GPIO package). This matters: the display data bus uses
  GPIO 32-39, which do not exist on the 30-GPIO RP2350A.
- Module: the board is effectively a Pimoroni Pico Plus 2 W on a custom carrier.
  - Flash: 16 MB QSPI, execute-in-place. Confirmed by the board profile and the
    Pimoroni spec. Large enough for a ROM library plus save slots.
  - PSRAM: 8 MB, chip-select on GPIO 47. Available for framebuffers, shader
    buffers, save-state staging, and a second console core.
  - Wireless: CYW43439 (WiFi and Bluetooth radio) is populated. Proven by stock
    MonaOS using `network.WLAN(network.STA_IF)`. BLE is usable; Bluetooth Classic
    A2DP audio is not practical on this SDK stack.
- Display: ST7789, 320x240, driven over an 8-bit parallel 8080 bus, not SPI.
  - CS 27, DC/RS 28, WR 30, RD 31, data bus D0-D7 on GPIO 32-39, backlight 26 (PWM).
  - No reset line is exposed. The ST7789 software reset (SWRESET) is used.
- Power: `POWER_EN` is GPIO 41 and must be driven high to hold the rail.
- Buttons (active low, pull-up): A 7, B 8, C 9, UP 10, DOWN 6, HOME 22.
- IR: RX 21, TX 20. The IR receiver on GPIO 21 was the first proof that this
  board is not a stock Tufty (21 is a display data pin on stock hardware).
- Audio: none. No speaker, no codec, no DAC, no headphone jack. Stock MonaOS
  has no audio code path of any kind. Any sound output requires a hardware mod
  (an external I2S DAC on the Qwiic/STEMMA bus, or PWM plus an RC filter to a
  wired jack). Bluetooth earphone audio is not a practical option on this radio.

All of the pin assignments live in `src/config.h`.

## Non-obvious gotchas

1. PIO GPIO base. A PIO state machine sees only a 32-pin window. The data bus
   (32-39) and WR (30) are above GPIO 31, so the LCD PIO calls
   `pio_set_gpio_base(pio0, 16)` to move its window to GPIO 16-47. Without this
   the display gets no data. See `tufty_lcd.c`.
2. Board variant. The build targets `pimoroni_pico_plus2_rp2350` (an RP2350B
   board). Building for `pico2` (RP2350A) will not accept GPIO 32-39.
3. SDK platform name. Pico SDK 2.x uses `PICO_PLATFORM=rp2350-arm-s`. The bare
   value `rp2350` fails with "rp2350.cmake does not exist".
4. GBC support is gated. Peanut-GB hides the `cgb` struct behind
   `PEANUT_FULL_GBC_SUPPORT`, which defaults to 0. `main.c` defines it to 1
   before including the header. Game Boy Color titles will not compile without it.
5. Panel orientation. The panel is mounted rotated. MADCTL is set for a 180
   degree rotation (`MADCTL_ROW | MADCTL_SWAP_XY | MADCTL_SCAN`).
6. Framebuffer byte order. The ST7789 expects big-endian RGB565. `main.c` byte
   swaps each pixel when writing the framebuffer, so the blit path streams raw.
7. No audio hardware. `ENABLE_SOUND` is 0. Do not add audio without first adding
   a speaker; there is none on the badge.

## Source layout

```
src/
  main.c              emulator glue: ROM access, CGB palette to RGB565,
                      input mapping, frame loop
  config.h            verified pin map, display-mode selection, geometry
  tufty_lcd.c/.h      8080 parallel ST7789 driver (PIO + DMA), scaling blit
  st7789_parallel.pio PIO program for the 8080 write strobe
third_party/peanut-gb/
  peanut_gb.h         vendored emulator core (cgb branch), MIT
tools/
  rom2c.py            ROM to C header converter (called by CMake)
  dump_pinout.py      reads the real pin map from the badge over the REPL
docs/                 hardware, building, flashing, configuration, architecture,
                      compatibility
```

## Build and flash

```bash
./build.sh /abs/path/game.gbc [fullscreen|aspect|centered]
# -> build/badgeboy.uf2
```

CMake injects two things:
- `GBC_ROM`: absolute path to the ROM. `tools/rom2c.py` converts it to
  `build/generated/gb_rom.h` (never committed).
- `GBC_DISPLAY_MODE`: FULLSCREEN, ASPECT, or CENTERED, passed to the firmware as
  `-DDISPLAY_MODE=<0|1|2>`.

Flashing: hold BOOTSEL, tap RESET, copy the `.uf2` to the `RP2350` drive.
USB ids: `2e8a:000f` is BOOTSEL, `2e8a:0009` is the running firmware,
`2e8a:0005` is MonaOS. See `docs/FLASHING.md` for restoring MonaOS.

## Conventions

- Plain C11, Pico SDK only. No RTOS, no dynamic allocation in the hot path.
- Keep verified hardware values in `config.h`, not scattered through the code.
- Do not commit ROMs, generated ROM headers, or built images. `.gitignore`
  covers them. They contain copyrighted data.
- Document new hardware findings here and in `docs/HARDWARE.md`.
- The project is licensed GPL-3.0-or-later. The vendored Peanut-GB core is MIT.
  New source files should carry the short SPDX header used by existing files.

## Roadmap

Ordered roughly from easiest to hardest. Effort is a rough estimate for one
developer familiar with the codebase. Items in a tier share prerequisites, so
doing them together is efficient.

Verified hardware that shapes this list: 16 MB flash, 8 MB PSRAM, CYW43439
wireless, and no audio hardware.

### Tier 1: software only, no new storage or UI

1. Fast-forward (easy). Toggle that drops the frame-time throttle and adds
   frame-skip. Bind to a held button (for example C+HOME cycles 1x, 2x, 4x).
   2x to 4x is comfortable. True 8x is best-effort: it is bound by display DMA
   throughput and the core, so expect "as fast as it runs" rather than a fixed
   multiple. Pure timing-loop change in `main.c`.
2. GBC color correction (easy). A precomputed lookup table that maps raw CGB
   colors to the washed, hardware-accurate palette. Applied during the RGB565
   conversion. Table can live in PSRAM. No display cost beyond one lookup.
3. Per-game DMG palette selection (easy). Pick the green or grey or custom
   palette for original Game Boy titles. Static tables, selected via config or
   the in-game menu once it exists.

### Tier 2: requires a flash storage layer (shared prerequisite)

The single biggest unlock. A small flash region below the firmware, written with
the SDK flash API, used by everything below. Build it once.

4. Persistent cartridge saves (moderate). Write cart SRAM to flash on HOME and
   on a dirty-RAM timer; load it at boot. Fixes the "saves are lost on power off"
   limitation. This is the natural first user of the flash layer.
5. Save-state slots (moderate). Serialize the full emulator state to numbered
   flash slots. Needs the menu in Tier 3 for slot selection, but the save and
   restore mechanism itself is independent and can land first with a fixed slot.

### Tier 3: requires an on-screen UI overlay

6. In-game menu (moderate). A pause overlay drawn over a frozen frame: Resume,
   Save State, Load State, Fast-forward, Reset, Palette. This is the UI host for
   the save-state slots and palette features. Needs a minimal font and input
   focus handling; no new hardware.
7. Display shaders (moderate). Per-pixel effects in the scaling blit: scanlines
   and a dot-matrix or LCD grid overlay, plus the GBC correction from Tier 1.
   There is no GPU, so these are CPU-side passes; PSRAM holds the effect buffers.
   Costs throughput, most visible in fullscreen, so make it toggleable from the
   menu.

### Tier 4: requires storage plus a launcher and asset management

8. Game menu and ROM browser (hard). Store several ROMs in the 16 MB flash (or
   stream from PSRAM) and pick one from a list at boot. Needs a flash layout for
   ROM blobs and a browser screen built on the Tier 3 UI. A later WiFi-based
   upload path (we have the CYW43439 radio) could replace re-flashing to add
   games.
9. NES support (hard). Add a second console core (for example InfoNES or a
   compact 6502 plus PPU core) behind a console abstraction so the launcher can
   boot either a GB or an NES title. PSRAM and CPU headroom make this viable; the
   real work is PPU timing, mapper coverage, and a clean core interface. Depends
   on the launcher (item 8) to choose a core.

### Tier 5: hard or hardware dependent

10. Dual-boot MonaOS and BadgeBoy (hard, risky). A small chain-loader in flash
    that offers MonaOS or BadgeBoy at power-on (for example HOME held at boot).
    Requires a deliberate flash partition layout and careful handling so a bad
    write cannot brick the badge. We have both images, so it is possible, but it
    is bootloader work, not a flag.
11. Audio out (hard, needs hardware). There is no audio hardware on the badge, so
    Peanut-GB sound stays off until one of these exists: an external I2S DAC on
    the Qwiic/STEMMA bus, or PWM plus an RC filter to a wired jack. Bluetooth
    earphone (A2DP) audio over the CYW43439 is not practical on this stack and is
    not planned.

## Verifying a change on hardware

There is no automated test for display output. After a change:
1. Build for a known-good ROM.
2. Flash and confirm the image is upright, correctly scaled, and responsive.
3. If the screen is blank, garbled, or rotated, the likely culprits are the PIO
   clock divider, the MADCTL value, the GPIO base, or the backlight and power
   pins. The bring-up table in `docs/HARDWARE.md` lists symptoms and fixes.
