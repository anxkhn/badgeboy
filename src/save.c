// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "save.h"

// Flash is laid out from the top down. The firmware lives at the bottom (a few
// MB), so these reserved regions never overlap program code. All erase and
// program functions run from RAM with interrupts disabled; this is safe because
// BadgeBoy is single core and never starts core1.
//
//   [ firmware ............ ][ state slots ][ battery save ]   (top of 16 MB)
//
#define SAVE_MAGIC   0x56534242u           // "BBSV"
#define SAVE_VERSION 1u
#define SAVE_REGION  (64u * 1024u)         // battery save area at the very top
#define SAVE_OFFSET  (PICO_FLASH_SIZE_BYTES - SAVE_REGION)

#define STATE_MAGIC     0x54534242u        // "BBST"
#define STATE_SLOT_SIZE (128u * 1024u)     // generous, holds gb_s + cart RAM
#define STATE_REGION    (STATE_SLOTS * STATE_SLOT_SIZE)
#define STATE_OFFSET    (PICO_FLASH_SIZE_BYTES - SAVE_REGION - STATE_REGION)

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
    char     build[16];      // firmware build tag, so a snapshot is build-specific
};

// Staging buffer for a flash program. Lives in RAM, as the program source must.
// Sized for the largest user (a state slot) and shared by the battery save path.
static uint8_t stage[STATE_SLOT_SIZE];

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
    const uint8_t *p = (const uint8_t *)(XIP_BASE + SAVE_OFFSET);
    struct save_header h;
    memcpy(&h, p, sizeof(h));
    if (h.magic != SAVE_MAGIC || h.version != SAVE_VERSION ||
        h.rom_id != rom_id || h.size != size)
        return false;
    memcpy(cart_ram, p + sizeof(h), size);
    return true;
}

void save_store(const uint8_t *cart_ram, uint32_t size, uint32_t rom_id) {
    if (size == 0)
        return;
    uint32_t total = (uint32_t)sizeof(struct save_header) + size;
    if (total > SAVE_REGION)
        return;

    struct save_header h = { SAVE_MAGIC, SAVE_VERSION, rom_id, size };
    memcpy(stage, &h, sizeof(h));
    memcpy(stage + sizeof(h), cart_ram, size);

    // Erase aligns to a sector, program aligns to a page.
    uint32_t erase = (total + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t prog  = (total + FLASH_PAGE_SIZE - 1)   & ~(FLASH_PAGE_SIZE - 1);
    memset(stage + total, 0xFF, prog - total);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SAVE_OFFSET, erase);
    flash_range_program(SAVE_OFFSET, stage, prog);
    restore_interrupts(ints);
}

bool state_store(int slot, const void *gb, uint32_t gb_size,
                 const uint8_t *cart_ram, uint32_t ram_size, uint32_t rom_id) {
    if (slot < 0 || slot >= STATE_SLOTS)
        return false;
    uint32_t total = (uint32_t)sizeof(struct state_header) + gb_size + ram_size;
    if (total > STATE_SLOT_SIZE)
        return false;

    struct state_header h = { STATE_MAGIC, rom_id, gb_size, ram_size, { 0 } };
    fill_build(h.build);
    uint8_t *p = stage;
    memcpy(p, &h, sizeof(h));      p += sizeof(h);
    memcpy(p, gb, gb_size);        p += gb_size;
    memcpy(p, cart_ram, ram_size);

    uint32_t off   = STATE_OFFSET + (uint32_t)slot * STATE_SLOT_SIZE;
    uint32_t erase = (total + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t prog  = (total + FLASH_PAGE_SIZE - 1)   & ~(FLASH_PAGE_SIZE - 1);
    memset(stage + total, 0xFF, prog - total);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, erase);
    flash_range_program(off, stage, prog);
    restore_interrupts(ints);
    return true;
}

bool state_load(int slot, void *gb, uint32_t gb_size,
                uint8_t *cart_ram, uint32_t ram_size, uint32_t rom_id) {
    if (slot < 0 || slot >= STATE_SLOTS)
        return false;
    const uint8_t *p = (const uint8_t *)(XIP_BASE + STATE_OFFSET +
                                         (uint32_t)slot * STATE_SLOT_SIZE);
    struct state_header h;
    memcpy(&h, p, sizeof(h));
    char want[16];
    fill_build(want);
    if (h.magic != STATE_MAGIC || h.rom_id != rom_id ||
        h.gb_size != gb_size || h.ram_size != ram_size ||
        memcmp(h.build, want, 16) != 0)
        return false;
    memcpy(gb, p + sizeof(h), gb_size);
    memcpy(cart_ram, p + sizeof(h) + gb_size, ram_size);
    return true;
}
