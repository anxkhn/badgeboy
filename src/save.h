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

// Save states: a full snapshot of the emulator (the whole gb_s struct plus cart
// RAM) written to a numbered flash slot, independent of the cartridge battery
// save. The caller passes the gb_s pointer and size; this module stays decoupled
// from the emulator core. State slots are tagged with the firmware build and the
// ROM id, so a snapshot is only ever restored into a compatible binary and game.

// Number of available save-state slots.
#define STATE_SLOTS 3

// Snapshot the emulator to a slot. Returns false if the slot index is invalid
// or the data does not fit. The gb_s pointers are not used by this module; the
// caller re-links them after a load.
bool state_store(int slot, const void *gb, uint32_t gb_size,
                 const uint8_t *cart_ram, uint32_t ram_size, uint32_t rom_id);

// Restore a snapshot from a slot into gb and cart_ram. Returns false when the
// slot holds no compatible snapshot for this build, ROM, and sizes.
bool state_load(int slot, void *gb, uint32_t gb_size,
                uint8_t *cart_ram, uint32_t ram_size, uint32_t rom_id);

#endif // SAVE_H
