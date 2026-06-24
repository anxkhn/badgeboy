// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "save.h"

// The save area occupies the top SAVE_REGION bytes of flash. The firmware lives
// at the bottom of flash (a few MB), so this never overlaps program code. The
// erase and program functions run from RAM with interrupts disabled; this is
// safe because BadgeBoy is single core and never starts core1.
#define SAVE_MAGIC   0x56534242u           // "BBSV"
#define SAVE_VERSION 1u
#define SAVE_REGION  (64u * 1024u)         // reserved bytes at the top of flash
#define SAVE_OFFSET  (PICO_FLASH_SIZE_BYTES - SAVE_REGION)

struct save_header {
    uint32_t magic;
    uint32_t version;
    uint32_t rom_id;
    uint32_t size;
};

// Staging buffer for a flash program. Lives in RAM, as the program source must.
static uint8_t stage[SAVE_REGION];

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
