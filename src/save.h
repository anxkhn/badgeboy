// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Persistent cartridge save RAM, stored in a reserved region at the top of the
// badge's flash. A save is tagged with a per-ROM id so it is never loaded for
// the wrong game.

// Load a previously stored save into cart_ram. Returns true only when a valid
// save for this exact ROM and size is present. size must be gb_get_save_size().
bool save_load(uint8_t *cart_ram, uint32_t size, uint32_t rom_id);

// Write cart_ram to flash, tagged with rom_id. Disables interrupts briefly and
// blocks for the duration of the flash erase and program. No-op when size is 0.
void save_store(const uint8_t *cart_ram, uint32_t size, uint32_t rom_id);

// CRC32 of a buffer, used to skip redundant writes when the save has not
// changed since it was last persisted.
uint32_t save_crc32(const uint8_t *data, uint32_t len);

#endif // SAVE_H
