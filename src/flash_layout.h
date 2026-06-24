// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

// Map of the badge's 16 MB on-board flash. Offsets are from the start of flash
// (0x10000000, XIP_BASE). Three independent regions, each flashed on its own:
//
//   0x000000  Firmware      the .uf2 built by build.sh (well under 4 MB)
//   0x400000  ROM pack      games, flashed separately by tools/pack_roms.py
//   0xC80000  Game saves    per-game battery and save-state areas
//
// The firmware and the ROM pack are separate flash images: reflashing your game
// library writes only the pack and leaves the firmware in place, and the reverse.

#define ROMPACK_OFFSET (4u * 1024u * 1024u) // 0x400000
#define GAMESAVE_BASE (0xC80000u)           // 0xC80000
#define ROMPACK_MAX_SIZE (GAMESAVE_BASE - ROMPACK_OFFSET)

// Per-game save area. Each game gets one battery-save block followed by the
// save-state slots, so games never overwrite one another's saves. The number of
// distinguishable games is bounded by the space between the pack and the top.
#define GAMESAVE_BATTERY_SIZE (64u * 1024u)
#define GAMESAVE_STATE_SLOTS 3
#define GAMESAVE_STATE_SIZE (128u * 1024u)
#define GAMESAVE_SLOT_SIZE                                                             \
    (GAMESAVE_BATTERY_SIZE + GAMESAVE_STATE_SLOTS * GAMESAVE_STATE_SIZE) // 448 KB
#define GAMESAVE_MAX 8 // 8 * 448 KB = 3.5 MB, fills exactly to the top

// ROM pack on-flash format, little endian, placed at ROMPACK_OFFSET.
#define ROMPACK_MAGIC 0x4B504242u // "BBPK"
#define ROMPACK_VERSION 1u
#define ROMPACK_TITLE_LEN 32
#define ROMPACK_FLAG_CGB 0x01

struct rompack_header {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
};

struct rompack_entry {
    char title[ROMPACK_TITLE_LEN]; // display name, null terminated
    uint32_t offset;               // bytes from pack start to the ROM data
    uint32_t size;                 // ROM size in bytes
    uint8_t flags;                 // ROMPACK_FLAG_*
    uint8_t pad[23];
};

#endif // FLASH_LAYOUT_H
