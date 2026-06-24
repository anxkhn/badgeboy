# Vendored: Peanut-GB

This directory contains a vendored copy of the Peanut-GB emulator core.

- Upstream: https://github.com/deltabeard/Peanut-GB
- Author: Mahyar Koshkouei
- License: MIT (see `LICENSE` in this directory)
- Branch: `cgb` (Game Boy Color support, contributed by froggestspirit and not
  yet merged to upstream `main` at the time of vendoring)

`peanut_gb.h` is a single-header emulator library for the Sharp LR35902 CPU,
PPU, timers, and memory bank controllers. BadgeBoy supplies the platform layer
(display, input, ROM access) and enables `PEANUT_FULL_GBC_SUPPORT`.

Only `peanut_gb.h` is vendored. To update, copy a newer `peanut_gb.h` from the
`cgb` branch and re-test against the compatibility notes in
`docs/COMPATIBILITY.md`.
