# Roadmap

Planned work, ordered roughly from easiest to hardest. Effort is a rough estimate
for one developer familiar with the codebase. Items in a tier tend to share
prerequisites, so building them together is efficient.

Status keys: `[ ]` planned, `[~]` in progress, `[x]` done and verified on hardware.

Verified hardware that shapes this list: RP2350B, 16 MB flash, 8 MB PSRAM
(CS GPIO 47), a CYW43439 WiFi and Bluetooth radio, and no audio hardware. See
[docs/HARDWARE.md](docs/HARDWARE.md).

## Tier 1: software only, no new storage or UI

- [~] **Fast-forward.** A latched speed control, not a hold. HOME acts as a
  function modifier: HOME+A cycles normal, 2x, and maximum speed, and the choice
  persists after release. Maximum drops the frame-time throttle and enables
  Peanut-GB frame skip, so it runs as fast as the hardware allows rather than a
  fixed multiple. Timing-loop change in `main.c`.
- [~] **GBC color correction.** Map raw CGB colors to the washed, hardware
  accurate LCD palette. Uses the Gambatte integer matrix in RGB555 space, which
  is shifts and adds only, no division, no clamping, with outputs already in the
  0 to 31 range:
  `R = (13r + 2g + b) >> 4`, `G = (3g + b) >> 2`, `B = (2g + 14b) >> 4`.
  Applied during the RGB565 conversion in `draw_line`. Toggle at runtime with
  HOME+B so the effect can be compared on hardware in one flash.
- [~] **DMG palette selection.** A set of four-shade palettes (authentic green,
  pocket grey, and others) for original Game Boy titles, selected at build time.
  Later this becomes a runtime choice once the in-game menu exists, and can be
  auto-selected per game with `gb_colour_hash`.

## Tier 2: requires a flash storage layer (shared prerequisite)

The single biggest unlock. A small flash region below the firmware, written with
the Pico SDK flash API (`flash_range_program` and `flash_range_erase`, run from
RAM with interrupts disabled). Used by everything below. Build it once and test
it carefully, since a bad write is how you brick the badge.

- [ ] **Persistent cartridge saves.** Write cart SRAM to flash on HOME and on a
  dirty-RAM timer; load it at boot. `gb_get_save_size` gives the exact size to
  persist. Fixes the "saves are lost on power off" limitation, and is the natural
  first user of the flash layer.
- [ ] **Save-state slots.** Serialize the full `struct gb_s` plus cart RAM to
  numbered flash slots. The save and restore mechanism is independent of the UI
  and can land first with a fixed slot, then gain slot selection from the menu.

## Tier 3: requires an on-screen UI overlay

- [ ] **In-game menu.** A pause overlay drawn over a frozen frame: Resume, Save
  State, Load State, Fast-forward, Reset, Palette. This is the UI host for the
  save-state slots and palette features. Needs a minimal bitmap font and input
  focus handling; no new hardware.
- [ ] **Display shaders.** Per-pixel effects in the scaling blit: scanlines and a
  dot-matrix or LCD grid overlay, alongside the GBC correction from Tier 1. There
  is no GPU, so these are CPU-side passes; PSRAM holds the effect buffers. Costs
  throughput, most visible in fullscreen, so they are toggleable from the menu.

## Tier 4: requires storage plus a launcher and asset management

- [ ] **Game menu and ROM browser.** Store several ROMs in the 16 MB flash (or
  stream from PSRAM) and pick one from a list at boot. Needs a flash layout for
  ROM blobs, `gb_get_rom_name` for titles, and a browser screen on the Tier 3 UI.
  A later WiFi upload path (the CYW43439 radio is present) could add games without
  re-flashing.
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
