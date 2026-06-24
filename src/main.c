// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "config.h"
#include "tufty_lcd.h"

// Peanut-GB build options. These must be set before including the core.
#define ENABLE_LCD              1
#define ENABLE_SOUND            0   // the badge has no speaker
#define PEANUT_FULL_GBC_SUPPORT 1   // Game Boy Color support is gated behind this
#include "gb_rom.h"                 // const unsigned char gb_rom[]; GB_ROM_SIZE
#include "peanut_gb.h"

// Game Boy joypad bits. Active low: clearing a bit means the input is pressed.
enum { JOYP_A=0x01, JOYP_B=0x02, JOYP_SELECT=0x04, JOYP_START=0x08,
       JOYP_RIGHT=0x10, JOYP_LEFT=0x20, JOYP_UP=0x40, JOYP_DOWN=0x80 };

struct priv_t {
    uint8_t  cart_ram[32 * 1024];   // cartridge save RAM (volatile for now)
    uint16_t fb[GB_W * GB_H];       // RGB565 framebuffer, byte swapped for the panel
};
static struct priv_t priv;
static struct gb_s   gb;

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    return gb_rom[addr];
}

static uint8_t ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    struct priv_t *p = gb->direct.priv;
    return (addr < sizeof(p->cart_ram)) ? p->cart_ram[addr] : 0xFF;
}

static void ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t v) {
    struct priv_t *p = gb->direct.priv;
    if (addr < sizeof(p->cart_ram)) p->cart_ram[addr] = v;
}

static void gb_err(struct gb_s *gb, const enum gb_error_e e, const uint16_t a) {
    (void)gb; (void)e; (void)a;     // continue running on non-fatal errors
}

// Convert a Game Boy Color RGB555 value (red in the high bits) to RGB565.
static inline uint16_t rgb555_to_565(uint16_t c) {
    uint16_t r = (c >> 10) & 0x1F;
    uint16_t g = (c >> 5)  & 0x1F;
    uint16_t b =  c        & 0x1F;
    uint16_t g6 = (g << 1) | (g >> 4);
    return (uint16_t)((r << 11) | (g6 << 5) | b);
}

// Fallback palette used when a monochrome (DMG) game is loaded.
static const uint16_t dmg_pal[4] = { 0x9772, 0x7409, 0x42E5, 0x1942 };

// Called by the core once per scanline with 160 pixel values.
static void draw_line(struct gb_s *gb, const uint8_t pixels[160],
                      const uint_fast8_t line) {
    struct priv_t *p = gb->direct.priv;
    uint16_t *row = &p->fb[line * GB_W];
    if (gb->cgb.cgbMode) {
        for (int x = 0; x < GB_W; x++) {
            uint16_t c = rgb555_to_565(gb->cgb.fixPalette[pixels[x] & 0x3F]);
            row[x] = (uint16_t)((c >> 8) | (c << 8));
        }
    } else {
        for (int x = 0; x < GB_W; x++) {
            uint16_t c = dmg_pal[pixels[x] & 0x03];
            row[x] = (uint16_t)((c >> 8) | (c << 8));
        }
    }
}

static void buttons_init(void) {
    const int pins[] = { BTN_A, BTN_B, BTN_C, BTN_UP, BTN_DOWN };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

static inline bool down(int pin) { return !gpio_get(pin); }

// Map the five front buttons to the eight Game Boy inputs. C is a shift
// modifier: holding it remaps UP/DOWN/A/B to Left/Right/Start/Select, which
// gives access to all eight inputs from five physical buttons.
static uint8_t read_joypad(void) {
    uint8_t jp = 0xFF;
    if (!down(BTN_C)) {
        if (down(BTN_UP))   jp &= ~JOYP_UP;
        if (down(BTN_DOWN)) jp &= ~JOYP_DOWN;
        if (down(BTN_A))    jp &= ~JOYP_A;
        if (down(BTN_B))    jp &= ~JOYP_B;
    } else {
        if (down(BTN_UP))   jp &= ~JOYP_LEFT;
        if (down(BTN_DOWN)) jp &= ~JOYP_RIGHT;
        if (down(BTN_A))    jp &= ~JOYP_START;
        if (down(BTN_B))    jp &= ~JOYP_SELECT;
    }
    return jp;
}

int main(void) {
    stdio_init_all();
    lcd_init();
    lcd_fill(0x0000);
    buttons_init();

    enum gb_init_error_e err = gb_init(&gb, &rom_read, &ram_read, &ram_write,
                                       &gb_err, &priv);
    if (err != GB_INIT_NO_ERROR) {
        // Pulse the backlight to signal an init failure.
        while (1) {
            lcd_set_backlight(255); sleep_ms(200);
            lcd_set_backlight(0);   sleep_ms(200);
        }
    }
    gb_init_lcd(&gb, &draw_line);
    gb.direct.frame_skip = 0;

    const uint32_t frame_us = 16743;   // 59.7 Hz
    while (1) {
        uint32_t t0 = time_us_32();
        gb.direct.joypad = read_joypad();
        gb_run_frame(&gb);
        lcd_blit_gb(priv.fb);
        int32_t rem = (int32_t)frame_us - (int32_t)(time_us_32() - t0);
        if (rem > 0) sleep_us(rem);
    }
}
