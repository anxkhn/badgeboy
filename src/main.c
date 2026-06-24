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

// Convert a Game Boy Color RGB555 value (red in the high bits) to raw RGB565.
static inline uint16_t cgb_raw_565(uint16_t c) {
    uint16_t r = (c >> 10) & 0x1F;
    uint16_t g = (c >> 5)  & 0x1F;
    uint16_t b =  c        & 0x1F;
    uint16_t g6 = (g << 1) | (g >> 4);
    return (uint16_t)((r << 11) | (g6 << 5) | b);
}

// Same conversion, but through the Gambatte color matrix so colors match the
// real CGB LCD rather than the oversaturated raw values. Integer shifts and
// adds only; outputs are already in the 0..31 range, so no clamping is needed.
static inline uint16_t cgb_corrected_565(uint16_t c) {
    int r = (c >> 10) & 0x1F;
    int g = (c >> 5)  & 0x1F;
    int b =  c        & 0x1F;
    int R = (13 * r + 2 * g + b)  >> 4;
    int G = (3 * g + b)           >> 2;
    int B = (2 * g + 14 * b)      >> 4;
    int g6 = (G << 1) | (G >> 4);
    return (uint16_t)((R << 11) | (g6 << 5) | B);
}

// Whether CGB color correction is active. Toggled at runtime with C+HOME.
static volatile bool color_correct = (GBC_COLOR_CORRECTION != 0);

// Palettes for monochrome (DMG) games, selected by DMG_PALETTE. Plain RGB565;
// the draw path byte swaps them for the panel.
#if DMG_PALETTE < 0 || DMG_PALETTE > 3
#error "DMG_PALETTE must be 0..3"
#endif
static const uint16_t dmg_palettes[4][4] = {
    { 0x9DE1, 0x8D61, 0x3306, 0x09C1 },   // 0 authentic DMG green
    { 0xFFFF, 0xAD55, 0x52AA, 0x0000 },   // 1 Game Boy Pocket grey
    { 0xFFFF, 0xC618, 0x4208, 0x0000 },   // 2 high-contrast mono
    { 0xFF36, 0xD50B, 0x8AC3, 0x28A0 },   // 3 dusk amber
};

// Called by the core once per scanline with 160 pixel values.
static void draw_line(struct gb_s *gb, const uint8_t pixels[160],
                      const uint_fast8_t line) {
    struct priv_t *p = gb->direct.priv;
    uint16_t *row = &p->fb[line * GB_W];
    if (gb->cgb.cgbMode) {
        // Resolve the 64 fixed palette entries once per line, then index them.
        uint16_t lut[0x40];
        if (color_correct)
            for (int i = 0; i < 0x40; i++)
                lut[i] = cgb_corrected_565(gb->cgb.fixPalette[i]);
        else
            for (int i = 0; i < 0x40; i++)
                lut[i] = cgb_raw_565(gb->cgb.fixPalette[i]);
        for (int x = 0; x < GB_W; x++) {
            uint16_t c = lut[pixels[x] & 0x3F];
            row[x] = (uint16_t)((c >> 8) | (c << 8));
        }
    } else {
        const uint16_t *pal = dmg_palettes[DMG_PALETTE];
        for (int x = 0; x < GB_W; x++) {
            uint16_t c = pal[pixels[x] & 0x03];
            row[x] = (uint16_t)((c >> 8) | (c << 8));
        }
    }
}

static void buttons_init(void) {
    const int pins[] = { BTN_A, BTN_B, BTN_C, BTN_UP, BTN_DOWN, BTN_HOME };
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

    // HOME is a function modifier, like a Fn key. While HOME is held the front
    // buttons control the emulator instead of the game, and their effect is
    // latched, so a speed change persists after the buttons are released:
    //   HOME + A  cycle speed: normal -> 2x -> max -> normal
    //   HOME + B  toggle GBC color correction
    const uint32_t frame_us = 16743;   // 59.7 Hz
    int  speed = 0;                    // 0 normal, 1 two times, 2 maximum
    bool prev_a = false, prev_b = false;

    while (1) {
        uint32_t t0 = time_us_32();

        if (down(BTN_HOME)) {
            bool a = down(BTN_A), b = down(BTN_B);
            if (a && !prev_a) speed = (speed + 1) % 3;
            if (b && !prev_b) color_correct = !color_correct;
            prev_a = a;
            prev_b = b;
            gb.direct.joypad = 0xFF;    // no game input while in function mode
        } else {
            prev_a = prev_b = false;
            gb.direct.joypad = read_joypad();
        }

        gb.direct.frame_skip = (speed == 2) ? 1 : 0;
        gb_run_frame(&gb);
        lcd_blit_gb(priv.fb);

        if (speed != 2) {
            uint32_t target = (speed == 1) ? (frame_us / 2) : frame_us;
            int32_t rem = (int32_t)target - (int32_t)(time_us_32() - t0);
            if (rem > 0) sleep_us(rem);
        }
    }
}
