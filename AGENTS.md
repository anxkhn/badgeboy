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
8. Three-region flash layout. Flash is split into firmware (`0x000000`), ROM pack
   (`0x400000`, `ROMPACK_OFFSET`), and per-game saves (`0xC80000`,
   `GAMESAVE_BASE`), all in `src/flash_layout.h`. The firmware and the ROM pack
   are separate flash images: a pack `.uf2` uses UF2 family id `0xe48bff57` (the
   RP2 absolute family) so BOOTSEL writes it at the absolute pack address without
   touching the firmware. Reflashing one leaves the other and the saves intact.
9. Per-game saves. Each game gets its own 448 KiB save area (one 64 KiB battery
   block plus three 128 KiB state slots), keyed by launcher index via
   `save_set_game(index)` (`src/save.c`). The built-in ROM is index 0; pack games
   follow, capped at 8 (`GAMESAVE_MAX`). A save is tagged with an FNV-1a `rom_id`
   so it never loads for the wrong game.
10. Backlight and brightness. The backlight is a single 16-bit hardware PWM on
    GPIO 26, not zoned. `lcd_set_backlight` applies a gamma 2.8 curve that crushes
    low inputs below the LED turn-on threshold, so the Brightness menu uses
    `lcd_set_backlight_pct`, a linear duty, with a 20 percent floor. There is no
    software framebuffer dimming.
11. Use clean, canonical ROM dumps. A bad or hacked dump (for example a Pokemon
    Red flagged `[S][BF]`) can break the game's own save validation even when
    BadgeBoy persists SRAM correctly. The clean canonical dump saves and
    continues fine.
12. Diagnostics behind a flag. On-screen save diagnostics are gated by the
    `BADGEBOY_DEBUG` CMake option (default OFF; `build.sh` forwards the env var).
    The save store/load logic is host-verifiable: a RAM-mocked test round-trips a
    store then load byte-perfect and rejects a wrong size or `rom_id`, so suspect
    the ROM dump or auto-save timing before the storage logic.

## Source layout

```
src/
  main.c              emulator glue: ROM access, CGB palette to RGB565,
                      input mapping, launcher, frame loop (run_game)
  config.h            verified pin map, display-mode selection, geometry
  flash_layout.h      three-region flash map (firmware, ROM pack, per-game saves)
  rompack.c/.h        ROM pack parsing, read in place from XIP flash
  save.c/.h           per-game battery and save-state flash storage layer
  tufty_lcd.c/.h      8080 parallel ST7789 driver (PIO + DMA), scaling blit
  st7789_parallel.pio PIO program for the 8080 write strobe
third_party/peanut-gb/
  peanut_gb.h         vendored emulator core (cgb branch), MIT
tools/
  rom2c.py            ROM to C header converter (called by CMake)
  pack_roms.py        builds a flashable ROM pack .uf2 from several ROMs
  dump_pinout.py      reads the real pin map from the badge over the REPL
docs/                 hardware, building, flashing, configuration, architecture,
                      saves, compatibility
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

Planned work, tiered by effort, lives in [ROADMAP.md](ROADMAP.md). Update the
status markers there as items land and are verified on hardware.

## Verifying a change on hardware

There is no automated test for display output. After a change:
1. Build for a known-good ROM.
2. Flash and confirm the image is upright, correctly scaled, and responsive.
3. If the screen is blank, garbled, or rotated, the likely culprits are the PIO
   clock divider, the MADCTL value, the GPIO base, or the backlight and power
   pins. The bring-up table in `docs/HARDWARE.md` lists symptoms and fixes.
