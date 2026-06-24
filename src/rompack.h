// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ROMPACK_H
#define ROMPACK_H

#include <stdint.h>
#include <stdbool.h>

// A game the launcher can run, sourced either from the firmware's built-in ROM
// or from the separately flashed ROM pack. The rom pointer is directly readable
// (flash is memory mapped), so the emulator reads it in place with no copy.
struct game {
    const char *title;  // null terminated, may point into flash
    const uint8_t *rom; // XIP pointer to the ROM image
    uint32_t size;
    bool cgb;
};

// Number of games present in the ROM pack at ROMPACK_OFFSET, or 0 when the pack
// is absent or invalid. Reads straight from memory-mapped flash.
int rompack_count(void);

// Fill out with game i from the pack (0 <= i < rompack_count()). Returns false
// for an out-of-range index.
bool rompack_get(int i, struct game *out);

#endif // ROMPACK_H
