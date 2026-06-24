# Changelog

All notable changes to this project are documented here. The format is based on
Keep a Changelog, and the project follows semantic versioning.

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

[0.1.0]: https://github.com/anxkhn/badgeboy/releases/tag/v0.1.0
