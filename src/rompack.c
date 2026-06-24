// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rompack.h"
#include "flash_layout.h"

#include "pico/stdlib.h"
#include "hardware/flash.h" // XIP_BASE via addressmap

// Base of the ROM pack in the memory-mapped flash window.
static const uint8_t *pack_base(void) {
    return (const uint8_t *)(XIP_BASE + ROMPACK_OFFSET);
}

static const struct rompack_header *pack_header(void) {
    return (const struct rompack_header *)pack_base();
}

int rompack_count(void) {
    const struct rompack_header *h = pack_header();
    if (h->magic != ROMPACK_MAGIC || h->version != ROMPACK_VERSION)
        return 0;
    if (h->count == 0 || h->count > 64)
        return 0;
    return (int)h->count;
}

bool rompack_get(int i, struct game *out) {
    if (i < 0 || i >= rompack_count())
        return false;
    const struct rompack_entry *e =
        (const struct rompack_entry *)(pack_base() + sizeof(struct rompack_header)) + i;

    // Reject an entry that points outside the pack region: a corrupt or
    // truncated pack must never hand the emulator a wild pointer.
    if (e->offset >= ROMPACK_MAX_SIZE || e->size == 0 ||
        e->size > ROMPACK_MAX_SIZE - e->offset)
        return false;

    out->title = e->title;
    out->rom = pack_base() + e->offset;
    out->size = e->size;
    out->cgb = (e->flags & ROMPACK_FLAG_CGB) != 0;
    return true;
}
