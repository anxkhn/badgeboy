# Changelog

All notable changes to this project are documented here. The format is based on
Keep a Changelog, and the project follows semantic versioning.

## [0.6.0] - 2026-06-24

Adds display shaders and corrects the automatic cartridge save.

### Added

- Display shaders, selectable from the in-game menu: scanlines, a dot-matrix LCD
  grid, a combined retro LCD, and a vignette. Each is a CPU-side pass folded into
  the scaling blit and tracks the source pixel grid as it scales.

### Changed

- The default monochrome palette is now high-contrast mono rather than green.

### Fixed

- The automatic cartridge save committed to flash every few seconds on games that
  write save RAM continuously (such as an in-game clock), which was noisy and wore
  the flash. Commits are now rate limited to at most once per 30 seconds, and the
  noisy "Game saved" toast is replaced by a small, brief corner dot.

## [0.5.0] - 2026-06-24

Adds an in-game mod menu and a round of UI and tooling work.

### Added

- In-game mod menu (HOME+C): a modal menu rendered at the panel's native 320x240,
  in a Material-style dark theme with the Tamzen 7x14 font. Collects speed, color
  correction, DMG palette, save-state slot selection, save and load state, and
  reset, with right-aligned values and a rounded selection highlight.
- Save-state slot selection (multiple slots) through the menu.
- Subtle text toasts for the quick save-state and battery-save actions.
- `lcd_blit_full` for native-resolution drawing, plus a clang-format config and
  `tools/format.sh`.

### Changed

- The DMG palette is now a runtime setting and is hidden on Game Boy Color games.
- HOME is a function modifier: HOME+A speed, HOME+B color correction, HOME+UP and
  HOME+DOWN save and load state, HOME+C opens the menu. The battery save remains
  automatic.
- Removed the on-screen dot indicators in favour of the menu and text toasts.
- Documentation: GitHub alert blocks and mermaid diagrams; Conventional Commits
  and the formatter step documented in CONTRIBUTING.

## [0.4.0] - 2026-06-24

Adds save-state snapshots, verified on hardware including across a power cycle.

### Added

- Save-state snapshots: a full capture of the machine (the whole `gb_s` plus cart
  RAM) to a flash slot, taken with HOME+UP and restored with HOME+DOWN,
  independent of the cartridge battery save.
- Snapshots are tagged with the firmware build and ROM id, so one only restores
  into a compatible binary and game; the emulator callbacks and priv pointer are
  re-linked after a restore. On-screen markers show snapshot (magenta) and
  restore result (green found, red not found).

### Changed

- HOME function controls reworked: the cartridge battery save is now fully
  automatic (no button), and HOME+UP / HOME+DOWN drive save states.
- Documented the two save kinds clearly in the README and `docs/CONFIGURATION.md`.

## [0.3.0] - 2026-06-24

Adds persistent cartridge saves, verified on hardware across a full power cycle.

### Added

- Battery-backed cartridge saves. Cart RAM is written to a reserved 64 KiB region
  at the top of flash and reloaded on boot, so progress survives power off.
- Automatic saving a moment after in-game writes settle, plus an explicit
  HOME+UP save, with an on-screen marker during the flash write.
- Per-ROM save tagging (FNV-1a hash of the ROM header) so a save never loads for
  the wrong game, and a CRC32 check that skips redundant writes to limit flash
  wear.
- New `src/save.c` flash storage layer using `flash_range_erase` and
  `flash_range_program` with interrupts disabled, safe for the single-core
  firmware.

## [0.2.0] - 2026-06-24

Adds the first round of playability features, all verified on hardware.

### Added

- Game Boy Color color correction using the Gambatte integer matrix, so colors
  match the real CGB LCD instead of the oversaturated raw values. On by default
  (`-DGBC_COLOR_CORRECTION`) and toggleable live with HOME+B.
- Latched fast-forward. HOME acts as a function modifier; HOME+A cycles normal,
  2x, and maximum speed. Fast-forward runs several emulated frames per displayed
  frame and skips the blocking blit on intermediate frames, so it speeds up
  despite the blit being the per-frame bottleneck.
- On-screen status indicator: one to three colored bars for the speed level and
  a marker for the color correction state.
- Four selectable palettes for monochrome (DMG) games, chosen at build time with
  `-DDMG_PALETTE=0..3`.

### Changed

- HOME is now a function modifier rather than a reserved button.
- Hardened `CMakeLists.txt` so a plain `cmake` invocation selects the correct
  RP2350 CPU flags (set `PICO_PLATFORM` and `PICO_BOARD` before `project()`).
- Documented the verified module memory and radio: 16 MB flash, 8 MB PSRAM
  (CS GPIO 47), and a CYW43439 WiFi and Bluetooth radio. See `docs/HARDWARE.md`.
- Moved the roadmap into `ROADMAP.md`, tiered by effort with status markers.

## [0.1.0] - 2026-06-24

First working release. A Game Boy Color title boots, renders full screen, and is
playable on the badge.

### Added

- Bare-metal RP2350 firmware running the Peanut-GB core (cgb branch) with Game
  Boy and Game Boy Color support.
- 8-bit parallel (8080) ST7789 display driver using PIO and DMA, with the init
  sequence modelled on Pimoroni's ST7789 driver for the 320x240 panel.
- Verified pin map for the custom badge, read from the device firmware and
  documented in `docs/HARDWARE.md`.
- Three display scaling modes: full screen, aspect correct, and native centered,
  selectable at build time.
- Build-time ROM embedding from any user-supplied `.gb` or `.gbc` file, via
  `tools/rom2c.py`.
- Five-button to eight-input control mapping using C as a shift modifier.
- Build system (`CMakeLists.txt`, `build.sh`) and tooling (`tools/rom2c.py`,
  `tools/dump_pinout.py`).
- Documentation set: hardware, building, flashing, configuration, architecture,
  compatibility, plus `AGENTS.md` and `CONTRIBUTING.md`.

### Known limitations

- No audio (the badge has no speaker).
- Cartridge saves are volatile and lost on power off.
- One embedded ROM per build; no on-device ROM browser yet.

[0.6.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.6.0
[0.5.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.5.0
[0.4.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.4.0
[0.3.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.3.0
[0.2.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.2.0
[0.1.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.1.0
