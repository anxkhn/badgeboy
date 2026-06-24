# Roadmap

Planned work, ordered roughly from easiest to hardest. Effort is a rough estimate
for one developer familiar with the codebase. Items in a tier tend to share
prerequisites, so building them together is efficient.

Status keys: `[ ]` planned, `[~]` in progress, `[x]` done and verified on hardware.

Verified hardware that shapes this list: RP2350B, 16 MB flash, 8 MB PSRAM
(CS GPIO 47), a CYW43439 WiFi and Bluetooth radio, and no audio hardware. See
[docs/HARDWARE.md](docs/HARDWARE.md).

## Tier 1: software only, no new storage or UI

Done in v0.2.0.

- [x] **Fast-forward.** A latched speed control, not a hold. HOME acts as a
  function modifier: HOME+A cycles normal, 2x, and maximum speed, and the choice
  persists after release. It runs several emulated frames per displayed frame and
  skips rendering and the blocking blit on the intermediate frames, so it speeds
  up even though the display blit is the per-frame bottleneck. An on-screen
  indicator shows the current speed (one to three colored bars). Verified on
  hardware.
- [x] **GBC color correction.** Map raw CGB colors to the washed, hardware
  accurate LCD palette. Uses the Gambatte integer matrix in RGB555 space, which
  is shifts and adds only, no division, no clamping, with outputs already in the
  0 to 31 range:
  `R = (13r + 2g + b) >> 4`, `G = (3g + b) >> 2`, `B = (2g + 14b) >> 4`.
  Applied during the RGB565 conversion in `draw_line`. Toggled at runtime with
  HOME+B, with an on-screen marker for the state. Verified on hardware.
- [x] **DMG palette selection.** Four four-shade palettes (authentic green,
  pocket grey, high-contrast mono, dusk amber) for original Game Boy titles,
  selected at build time with `DMG_PALETTE`. Later this becomes a runtime choice
  once the in-game menu exists, and can be auto-selected per game with
  `gb_colour_hash`.

## Tier 2: requires a flash storage layer (shared prerequisite)

Done in v0.3.0 (battery saves) and v0.4.0 (save states).

The single biggest unlock. A small flash region below the firmware, written with
the Pico SDK flash API (`flash_range_program` and `flash_range_erase`, run from
RAM with interrupts disabled). Used by everything below. Build it once and test
it carefully, since a bad write is how you brick the badge.

- [x] **Persistent cartridge saves.** Write cart SRAM to flash on HOME+UP and on
  a dirty-RAM timer; load it at boot. `gb_get_save_size` gives the exact size to
  persist, and a CRC check skips redundant writes to limit flash wear. Saves are
  tagged with a per-ROM id so one game never loads another game's save. The save
  area is the top 64 KiB of flash, written with `flash_range_erase` and
  `flash_range_program` with interrupts disabled (safe because the firmware is
  single core). Implemented in `src/save.c`. Verified on hardware across a full
  power cycle.
- [x] **Save-state slots.** A full snapshot of the machine (the whole `gb_s`
  struct plus cart RAM) written to a numbered flash slot, independent of the
  battery save. Implemented in `src/save.c` as `state_store` and `state_load`,
  tagged with the firmware build and ROM id so a snapshot only restores into a
  compatible binary and game; the callbacks and priv pointer are re-linked after
  a load. One slot is wired (HOME+UP snapshot, HOME+DOWN restore); slot selection
  arrives with the in-game menu. Verified on hardware, including across a power
  cycle.

## Tier 3: requires an on-screen UI overlay

- [x] **In-game menu.** A modal mod menu opened with HOME+C, rendered at the
  panel's native 320x240 (not upscaled from the Game Boy framebuffer) in a
  Material-style dark theme with the Tamzen 7x14 font. Collects speed, color
  correction, DMG palette, save-state slot selection, save and load state, and
  reset, with right-aligned values and a rounded selection highlight. This is the
  UI host that unlocks save-state slot selection. Verified on hardware.
- [x] **Display shaders.** Per-pixel effects applied during the scaling blit:
  scanlines, a dot-matrix LCD grid, a combined retro LCD, and a vignette, each
  tracking the source pixel grid as it scales. There is no GPU, so these are
  CPU-side passes folded into the existing nearest-neighbour upscale. They are
  selected from the in-game menu and are most visible in fullscreen. Verified on
  hardware.

## Tier 4: requires storage plus a launcher and asset management

- [x] **Game menu and ROM browser.** Done in v0.7.0. The game library lives in a
  separately flashed ROM pack at a fixed flash offset (`0x400000`), built on the
  host by `tools/pack_roms.py` and read in place from XIP flash by
  `src/rompack.c`. The firmware's built-in ROM is game index 0; pack games are
  appended, capped at 8 so each game keeps its own save area. On boot the
  launcher lists each game with a CGB or DMG tag; switching is a soft reset via
  `run_game()`, no reboot. Each game has its own battery and save-state area,
  keyed by launcher index (`src/save.c`, `src/flash_layout.h`). Verified on
  hardware. A later WiFi upload path (the CYW43439 radio is present) could add
  games without re-flashing.
- [ ] **NES support.** Add a second console core (for example InfoNES, or a
  compact 6502 plus PPU core) behind a console abstraction so the launcher can
  boot a GB or an NES title. PSRAM and CPU headroom make this viable; the real
  work is PPU timing, mapper coverage, and a clean core interface. Depends on the
  launcher to choose a core.

## Tier 5: hard or hardware dependent

- [ ] **Dual-boot MonaOS and BadgeBoy.** A small chain-loader in flash that
  offers MonaOS or BadgeBoy at power-on (for example HOME held at boot). Requires
  a deliberate flash partition layout and careful handling so a bad write cannot
  brick the badge. Both images exist, so it is possible, but it is bootloader
  work, not a flag.
- [ ] **Audio out.** There is no audio hardware on the badge, so Peanut-GB sound
  stays off until one of these exists: an external I2S DAC on the Qwiic/STEMMA
  bus, or PWM plus an RC filter to a wired jack. Bluetooth earphone (A2DP) audio
  over the CYW43439 is not practical on this stack and is not planned.
