// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "save.h"
#include "flash_layout.h"

// Flash is split into independent regions (see flash_layout.h): the firmware at
// the bottom, the ROM pack above it, and a per-game save area near the top. Each
// game has its own battery-save block followed by its save-state slots, so games
// never overwrite one another's saves. All erase and program functions run from
// RAM with interrupts disabled; this is safe because BadgeBoy is single core and
// never starts core1.
//
//   [ firmware ][ ROM pack ][ game 0 save ][ game 1 save ] ... (top of 16 MB)
//   where each game save is [ battery ][ state slot 0 ][ state slot 1 ] ...
//
#define SAVE_MAGIC 0x56534242u // "BBSV"
#define SAVE_VERSION 1u
#define STATE_MAGIC 0x54534242u // "BBST"

// The active game's index, set by save_set_game. Selects which per-game area in
// flash the battery and state functions read and write.
static int g_game_slot = 0;

void save_set_game(int slot) {
    if (slot < 0 || slot >= GAMESAVE_MAX)
        slot = 0;
    g_game_slot = slot;
}

// Flash offset of the active game's save area and its sub-regions.
static uint32_t game_base_of(int slot) {
    return GAMESAVE_BASE + (uint32_t)slot * GAMESAVE_SLOT_SIZE;
}
static uint32_t game_base(void) {
    return game_base_of(g_game_slot);
}
static uint32_t battery_offset(void) {
    return game_base();
}
static uint32_t state_offset(int slot) {
    return game_base() + GAMESAVE_BATTERY_SIZE + (uint32_t)slot * GAMESAVE_STATE_SIZE;
}

struct save_header {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_id;
    uint32_t size;
};

struct state_header {
    uint32_t magic;
    uint32_t rom_id;
    uint32_t gb_size;
    uint32_t ram_size;
    char build[16]; // firmware build tag, so a snapshot is build-specific
};

// Staging buffer for a flash program. Lives in RAM, as the program source must.
// Sized for the largest user (a state slot) and shared by the battery save path.
static uint8_t stage[GAMESAVE_STATE_SIZE];

// A 16-byte tag identifying this firmware build. Restoring a snapshot relies on
// the struct layout and code addresses matching, so snapshots are rejected
// across builds.
static void fill_build(char out[16]) {
    memset(out, 0, 16);
    strncpy(out, __DATE__ " " __TIME__, 16);
}

uint32_t save_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

bool save_load(uint8_t *cart_ram, uint32_t size, uint32_t rom_id) {
    if (size == 0)
        return false;
    const uint8_t *p = (const uint8_t *)(XIP_BASE + battery_offset());
    struct save_header h;
    memcpy(&h, p, sizeof(h));
    if (h.magic != SAVE_MAGIC || h.version != SAVE_VERSION || h.rom_id != rom_id ||
        h.size != size)
        return false;
    memcpy(cart_ram, p + sizeof(h), size);
    return true;
}

void save_store(const uint8_t *cart_ram, uint32_t size, uint32_t rom_id) {
    if (size == 0)
        return;
    uint32_t total = (uint32_t)sizeof(struct save_header) + size;
    if (total > GAMESAVE_BATTERY_SIZE)
        return;

    struct save_header h = {SAVE_MAGIC, SAVE_VERSION, rom_id, size};
    memcpy(stage, &h, sizeof(h));
    memcpy(stage + sizeof(h), cart_ram, size);

    // Erase aligns to a sector, program aligns to a page.
    uint32_t erase = (total + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t prog = (total + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    memset(stage + total, 0xFF, prog - total);

    uint32_t off = battery_offset();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, erase);
    flash_range_program(off, stage, prog);
    restore_interrupts(ints);
}

bool state_store(int slot, const void *gb, uint32_t gb_size, const uint8_t *cart_ram,
                 uint32_t ram_size, uint32_t rom_id) {
    if (slot < 0 || slot >= STATE_SLOTS)
        return false;
    uint32_t total = (uint32_t)sizeof(struct state_header) + gb_size + ram_size;
    if (total > GAMESAVE_STATE_SIZE)
        return false;

    struct state_header h = {STATE_MAGIC, rom_id, gb_size, ram_size, {0}};
    fill_build(h.build);
    uint8_t *p = stage;
    memcpy(p, &h, sizeof(h));
    p += sizeof(h);
    memcpy(p, gb, gb_size);
    p += gb_size;
    memcpy(p, cart_ram, ram_size);

    uint32_t off = state_offset(slot);
    uint32_t erase = (total + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t prog = (total + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    memset(stage + total, 0xFF, prog - total);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, erase);
    flash_range_program(off, stage, prog);
    restore_interrupts(ints);
    return true;
}

bool state_load(int slot, void *gb, uint32_t gb_size, uint8_t *cart_ram,
                uint32_t ram_size, uint32_t rom_id) {
    if (slot < 0 || slot >= STATE_SLOTS)
        return false;
    const uint8_t *p = (const uint8_t *)(XIP_BASE + state_offset(slot));
    struct state_header h;
    memcpy(&h, p, sizeof(h));
    char want[16];
    fill_build(want);
    if (h.magic != STATE_MAGIC || h.rom_id != rom_id || h.gb_size != gb_size ||
        h.ram_size != ram_size || memcmp(h.build, want, 16) != 0)
        return false;
    memcpy(gb, p + sizeof(h), gb_size);
    memcpy(cart_ram, p + sizeof(h) + gb_size, ram_size);
    return true;
}

void save_peek_battery(int game_slot, uint32_t *magic, uint32_t *rom_id,
                       uint32_t *size) {
    struct save_header h = {0, 0, 0, 0};
    if (game_slot >= 0 && game_slot < GAMESAVE_MAX)
        memcpy(&h, (const void *)(XIP_BASE + game_base_of(game_slot)), sizeof(h));
    if (magic)
        *magic = h.magic;
    if (rom_id)
        *rom_id = h.rom_id;
    if (size)
        *size = h.size;
}
