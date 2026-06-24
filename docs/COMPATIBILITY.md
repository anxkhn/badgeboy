# Compatibility

This document explains the emulator core choice, what is supported, and the
known limits.

## Why Peanut-GB

The badge is a microcontroller, not a Linux board, so the emulator had to be a
small, fast, portable C core with no operating system dependencies. Peanut-GB
fits exactly: it is a single-header Game Boy core in C99, designed to be fast
enough for microcontrollers, and it is already used on RP2040 and RP2350 class
chips. Heavier multi-system emulators were ruled out because they assume far more
CPU, memory, or an OS.

The standard Peanut-GB `main` branch supports the original Game Boy (DMG) only.
Game Boy Color requires the `cgb` branch, which adds the color palettes and the
double-speed and DMA behaviour. BadgeBoy vendors that branch and enables it with
`PEANUT_FULL_GBC_SUPPORT`. Pokemon Yellow, for example, is a GBC enhanced title
and needs this.

## Supported systems

| System                     | Status          |
|----------------------------|-----------------|
| Game Boy (DMG)             | supported       |
| Game Boy Color (CGB)       | supported       |
| Game Boy Advance (GBA)     | not supported, out of scope |

GBA is not feasible on an RP2350 without external memory and far more compute,
so it is explicitly out of scope.

## Memory bank controllers

Peanut-GB implements the common controllers used by the large majority of GB and
GBC carts: no MBC, MBC1, MBC2, MBC3 (including the real-time clock variants), and
MBC5. ROM and RAM sizes that fit the badge flash and SRAM will load. Exotic or
rare mappers may not be supported by the core.

## Known limitations in this release (0.1.0)

- No audio. The badge has no speaker, so sound is compiled out. This also avoids
  the hardest timing-sensitive part of GB emulation.
- Saves are volatile. Cartridge RAM lives in SRAM and is lost on power off.
  Battery-backed games will save in-game, but the data does not persist across a
  reboot yet. Persisting cart RAM to flash is the top roadmap item.
- One ROM per build. The ROM is embedded in flash at build time. There is no
  on-device ROM browser yet.
- Five buttons for eight inputs. The badge has UP, DOWN, A, B, and C on the
  front. C is a shift modifier, so Left, Right, Start, and Select are reached by
  holding C. Games that need fast diagonal or simultaneous Left/Right input will
  feel awkward.
- Full-screen scaling is nearest neighbour and slightly stretches horizontally,
  because the panel area is wider than the 10:9 Game Boy ratio. Use the `ASPECT`
  display mode for correct proportions.
- Speed. Most titles run at or near full speed. Very demanding games may dip;
  frame skip and overclocking are available as future tuning options.

## Tested

| Game                     | Mode | Result |
|--------------------------|------|--------|
| Pokemon Yellow (GBC)     | GBC enhanced, MBC5+RAM+BATTERY | boots, full screen, playable |

Contributions to this table are welcome. Include the cartridge type printed by
`tools/rom2c.py`, the display mode, and what you observed.
