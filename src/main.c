// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "config.h"
#include "tufty_lcd.h"
#include "save.h"
#include "font8x8_basic.h"

// Peanut-GB build options. These must be set before including the core.
#define ENABLE_LCD 1
#define ENABLE_SOUND 0            // the badge has no speaker
#define PEANUT_FULL_GBC_SUPPORT 1 // Game Boy Color support is gated behind this
#include "gb_rom.h"               // const unsigned char gb_rom[]; GB_ROM_SIZE
#include "peanut_gb.h"

// Game Boy joypad bits. Active low: clearing a bit means the input is pressed.
enum {
    JOYP_A = 0x01,
    JOYP_B = 0x02,
    JOYP_SELECT = 0x04,
    JOYP_START = 0x08,
    JOYP_RIGHT = 0x10,
    JOYP_LEFT = 0x20,
    JOYP_UP = 0x40,
    JOYP_DOWN = 0x80
};

struct priv_t {
    uint8_t cart_ram[32 * 1024]; // cartridge save RAM, persisted to flash
    uint16_t fb[GB_W * GB_H];    // RGB565 framebuffer, byte swapped for the panel
};
static struct priv_t priv;
static struct gb_s gb;

// Set whenever the game writes cartridge RAM, with the time of the last write.
// The main loop persists the save to flash once writes have settled.
static volatile bool ram_dirty = false;
static volatile uint32_t ram_write_us = 0;

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
    if (addr < sizeof(p->cart_ram)) {
        p->cart_ram[addr] = v;
        ram_dirty = true;
        ram_write_us = time_us_32();
    }
}

// A stable id for the loaded ROM, hashed from its header (title and checksums).
// Used to tag saves so one game never loads another game's save.
static uint32_t rom_id(void) {
    uint32_t h = 2166136261u; // FNV-1a
    for (int i = 0x134; i <= 0x14F; i++) {
        h ^= gb_rom[i];
        h *= 16777619u;
    }
    return h;
}

static void gb_err(struct gb_s *gb, const enum gb_error_e e, const uint16_t a) {
    (void)gb;
    (void)e;
    (void)a; // continue running on non-fatal errors
}

// Live emulator settings the menu adjusts.
static int g_speed = 0;       // 0 normal, 1 two times, 2 maximum
static int g_slot = 0;        // selected save-state slot
static uint32_t g_rom_id = 0; // set in main, used by save and load

static void draw_line(struct gb_s *gb, const uint8_t pixels[160],
                      const uint_fast8_t line); // defined below

// Restore the callbacks and priv pointer to this build's code and data. Used
// after a save-state load, which overwrites the whole gb_s including pointers.
static void relink_gb(void) {
    gb.gb_rom_read = &rom_read;
    gb.gb_cart_ram_read = &ram_read;
    gb.gb_cart_ram_write = &ram_write;
    gb.gb_error = &gb_err;
    gb.direct.priv = &priv;
    gb.display.lcd_draw_line = &draw_line;
}

// Convert a Game Boy Color RGB555 value (red in the high bits) to raw RGB565.
static inline uint16_t cgb_raw_565(uint16_t c) {
    uint16_t r = (c >> 10) & 0x1F;
    uint16_t g = (c >> 5) & 0x1F;
    uint16_t b = c & 0x1F;
    uint16_t g6 = (g << 1) | (g >> 4);
    return (uint16_t)((r << 11) | (g6 << 5) | b);
}

// Same conversion, but through the Gambatte color matrix so colors match the
// real CGB LCD rather than the oversaturated raw values. Integer shifts and
// adds only; outputs are already in the 0..31 range, so no clamping is needed.
static inline uint16_t cgb_corrected_565(uint16_t c) {
    int r = (c >> 10) & 0x1F;
    int g = (c >> 5) & 0x1F;
    int b = c & 0x1F;
    int R = (13 * r + 2 * g + b) >> 4;
    int G = (3 * g + b) >> 2;
    int B = (2 * g + 14 * b) >> 4;
    int g6 = (G << 1) | (G >> 4);
    return (uint16_t)((R << 11) | (g6 << 5) | B);
}

// Whether CGB color correction is active. Toggled from the menu or HOME+B.
static volatile bool color_correct = (GBC_COLOR_CORRECTION != 0);

// Palettes for monochrome (DMG) games. DMG_PALETTE sets the power-on choice;
// dmg_pal_idx is the live value the menu adjusts. Plain RGB565; the draw path
// byte swaps them for the panel.
#if DMG_PALETTE < 0 || DMG_PALETTE > 3
#error "DMG_PALETTE must be 0..3"
#endif
static const char *dmg_pal_names[4] = {"Green", "Grey", "Mono", "Amber"};
static int dmg_pal_idx = DMG_PALETTE;
static const uint16_t dmg_palettes[4][4] = {
    {0x9DE1, 0x8D61, 0x3306, 0x09C1}, // 0 authentic DMG green
    {0xFFFF, 0xAD55, 0x52AA, 0x0000}, // 1 Game Boy Pocket grey
    {0xFFFF, 0xC618, 0x4208, 0x0000}, // 2 high-contrast mono
    {0xFF36, 0xD50B, 0x8AC3, 0x28A0}, // 3 dusk amber
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
        const uint16_t *pal = dmg_palettes[dmg_pal_idx];
        for (int x = 0; x < GB_W; x++) {
            uint16_t c = pal[pixels[x] & 0x03];
            row[x] = (uint16_t)((c >> 8) | (c << 8));
        }
    }
}

static void buttons_init(void) {
    const int pins[] = {BTN_A, BTN_B, BTN_C, BTN_UP, BTN_DOWN, BTN_HOME};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

static inline bool down(int pin) {
    return !gpio_get(pin);
}

// Draw a small filled block into the byte-swapped framebuffer.
static void put_block(uint16_t *fb, int x0, int y0, int w, int h, uint16_t col) {
    uint16_t sw = (uint16_t)((col >> 8) | (col << 8));
    for (int y = y0; y < y0 + h && y < GB_H; y++)
        for (int x = x0; x < x0 + w && x < GB_W; x++)
            fb[y * GB_W + x] = sw;
}

// On-screen status overlay drawn into the top-left of the frame: one to three
// bars for the speed level (green, yellow, red), and a marker for the color
// correction state (cyan on, grey off).
static void draw_indicator(uint16_t *fb, int speed, bool cc) {
    const uint16_t speed_col[3] = {0x07E0, 0xFFE0, 0xF800};
    for (int i = 0; i <= speed; i++)
        put_block(fb, 4 + i * 8, 4, 6, 6, speed_col[speed]);
    put_block(fb, 4, 14, 6, 6, cc ? 0x07FF : 0x8410);
}

// Draw one 8x8 glyph into the byte-swapped framebuffer. The font stores the
// least significant bit as the leftmost pixel.
static void draw_char(uint16_t *fb, int x0, int y0, char ch, uint16_t col) {
    if ((unsigned char)ch > 0x7F)
        ch = '?';
    uint16_t sw = (uint16_t)((col >> 8) | (col << 8));
    const char *g = font8x8_basic[(unsigned char)ch];
    for (int r = 0; r < 8; r++) {
        int y = y0 + r;
        if (y < 0 || y >= GB_H)
            continue;
        unsigned char bits = (unsigned char)g[r];
        for (int c = 0; c < 8; c++) {
            if (!((bits >> c) & 1))
                continue;
            int x = x0 + c;
            if (x < 0 || x >= GB_W)
                continue;
            fb[y * GB_W + x] = sw;
        }
    }
}

static void draw_text(uint16_t *fb, int x0, int y0, const char *s, uint16_t col) {
    for (int x = x0; *s; s++, x += 8)
        draw_char(fb, x, y0, *s, col);
}

// Halve every channel of every pixel to dim the frame behind the menu. Works on
// the byte-swapped RGB565 buffer using the standard halving mask.
static void dim_fb(uint16_t *fb) {
    for (int i = 0; i < GB_W * GB_H; i++) {
        uint16_t c = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        c = (uint16_t)((c >> 1) & 0x7BEF);
        fb[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}

// Map the five front buttons to the eight Game Boy inputs. C is a shift
// modifier: holding it remaps UP/DOWN/A/B to Left/Right/Start/Select, which
// gives access to all eight inputs from five physical buttons.
static uint8_t read_joypad(void) {
    uint8_t jp = 0xFF;
    if (!down(BTN_C)) {
        if (down(BTN_UP))
            jp &= ~JOYP_UP;
        if (down(BTN_DOWN))
            jp &= ~JOYP_DOWN;
        if (down(BTN_A))
            jp &= ~JOYP_A;
        if (down(BTN_B))
            jp &= ~JOYP_B;
    } else {
        if (down(BTN_UP))
            jp &= ~JOYP_LEFT;
        if (down(BTN_DOWN))
            jp &= ~JOYP_RIGHT;
        if (down(BTN_A))
            jp &= ~JOYP_START;
        if (down(BTN_B))
            jp &= ~JOYP_SELECT;
    }
    return jp;
}

// In-game menu. Opened with HOME+C. While open the game is paused and the front
// buttons drive the menu: UP/DOWN move, A activates or steps a value forward, C
// steps a value back, B closes.
enum {
    MI_RESUME,
    MI_SPEED,
    MI_COLOR,
    MI_PALETTE,
    MI_SLOT,
    MI_SAVE,
    MI_LOAD,
    MI_RESET,
    MI_COUNT
};

static bool g_menu_open = false;
static int g_menu_sel = 0;
static char g_status[22] = "";

static void menu_open(void) {
    dim_fb(priv.fb); // dim the paused frame once; the panel is opaque
    g_menu_open = true;
    g_menu_sel = 0;
    g_status[0] = '\0';
}

static void menu_label(int item, char *out, int n) {
    const char *speed_s[3] = {"Normal", "2x", "Max"};
    switch (item) {
    case MI_RESUME:
        snprintf(out, n, "Resume");
        break;
    case MI_SPEED:
        snprintf(out, n, "Speed:   %s", speed_s[g_speed]);
        break;
    case MI_COLOR:
        snprintf(out, n, "Color:   %s", color_correct ? "On" : "Off");
        break;
    case MI_PALETTE:
        snprintf(out, n, "Palette: %s", dmg_pal_names[dmg_pal_idx]);
        break;
    case MI_SLOT:
        snprintf(out, n, "Slot:    %d", g_slot);
        break;
    case MI_SAVE:
        snprintf(out, n, "Save state");
        break;
    case MI_LOAD:
        snprintf(out, n, "Load state");
        break;
    case MI_RESET:
        snprintf(out, n, "Reset game");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

// Apply A (dir +1) or C (dir -1) to the selected item. Returns true to close.
static bool menu_activate(int dir) {
    switch (g_menu_sel) {
    case MI_RESUME:
        return true;
    case MI_SPEED:
        g_speed = (g_speed + 3 + dir) % 3;
        break;
    case MI_COLOR:
        color_correct = !color_correct;
        break;
    case MI_PALETTE:
        dmg_pal_idx = (dmg_pal_idx + 4 + dir) % 4;
        break;
    case MI_SLOT:
        g_slot = (g_slot + STATE_SLOTS + dir) % STATE_SLOTS;
        break;
    case MI_SAVE:
        if (dir > 0) {
            bool ok = state_store(g_slot, &gb, sizeof(gb), priv.cart_ram,
                                  sizeof(priv.cart_ram), g_rom_id);
            snprintf(g_status, sizeof(g_status),
                     ok ? "Saved to slot %d" : "Save failed", g_slot);
        }
        break;
    case MI_LOAD:
        if (dir > 0) {
            bool ok = state_load(g_slot, &gb, sizeof(gb), priv.cart_ram,
                                 sizeof(priv.cart_ram), g_rom_id);
            if (ok) {
                relink_gb();
                return true;
            }
            snprintf(g_status, sizeof(g_status), "Slot %d empty", g_slot);
        }
        break;
    case MI_RESET:
        if (dir > 0) {
            gb_reset(&gb);
            return true;
        }
        break;
    }
    return false;
}

// One iteration of the menu: handle input, render, blit. Closes on B or Resume.
static void menu_step(void) {
    static bool pu, pd, pa, pb, pc;
    static bool primed = false;
    bool u = down(BTN_UP), d = down(BTN_DOWN);
    bool a = down(BTN_A), b = down(BTN_B), c = down(BTN_C);

    // On the first step after opening, swallow whatever is still held (the HOME
    // and C from the opening combo) so it is not read as a fresh press.
    if (!primed) {
        pu = u;
        pd = d;
        pa = a;
        pb = b;
        pc = c;
        primed = true;
    }

    if (u && !pu)
        g_menu_sel = (g_menu_sel + MI_COUNT - 1) % MI_COUNT;
    if (d && !pd)
        g_menu_sel = (g_menu_sel + 1) % MI_COUNT;
    if (a && !pa) {
        if (menu_activate(+1)) {
            g_menu_open = false;
            primed = false;
        }
    }
    if (c && !pc)
        menu_activate(-1);
    if (b && !pb) {
        g_menu_open = false;
        primed = false;
    }
    pu = u;
    pd = d;
    pa = a;
    pb = b;
    pc = c;

    // Opaque panel over the dimmed frame.
    const int px = 8, py = 6, pw = GB_W - 16, ph = GB_H - 12;
    put_block(priv.fb, px, py, pw, ph, 0x0010);         // panel
    put_block(priv.fb, px, py, pw, 1, 0x5AEB);          // top border
    put_block(priv.fb, px, py + ph - 1, pw, 1, 0x5AEB); // bottom border
    draw_text(priv.fb, px + 6, py + 4, "BadgeBoy menu", 0xFFE0);

    for (int i = 0; i < MI_COUNT; i++) {
        int y = py + 18 + i * 12;
        char line[22];
        menu_label(i, line, sizeof(line));
        if (i == g_menu_sel) {
            put_block(priv.fb, px + 2, y - 1, pw - 4, 10, 0x315A);
            draw_text(priv.fb, px + 6, y, line, 0xFFFF);
        } else {
            draw_text(priv.fb, px + 6, y, line, 0xC618);
        }
    }
    if (g_status[0])
        draw_text(priv.fb, px + 6, py + ph - 11, g_status, 0x07FF);

    if (g_menu_open == false)
        primed = false; // reset for next open
    lcd_blit_gb(priv.fb);
    sleep_ms(16);
}

int main(void) {
    stdio_init_all();
    lcd_init();
    lcd_fill(0x0000);
    buttons_init();

    enum gb_init_error_e err =
        gb_init(&gb, &rom_read, &ram_read, &ram_write, &gb_err, &priv);
    if (err != GB_INIT_NO_ERROR) {
        // Pulse the backlight to signal an init failure.
        while (1) {
            lcd_set_backlight(255);
            sleep_ms(200);
            lcd_set_backlight(0);
            sleep_ms(200);
        }
    }
    gb_init_lcd(&gb, &draw_line);
    gb.direct.frame_skip = 0;

    // Load this ROM's persisted cartridge save, if any. Battery-backed games
    // report a non-zero save size; ROMs without battery RAM report zero.
    uint32_t rid = rom_id();
    g_rom_id = rid;
    uint32_t save_size = gb_get_save_size(&gb);
    if (save_size > sizeof(priv.cart_ram))
        save_size = sizeof(priv.cart_ram);
    save_load(priv.cart_ram, save_size, rid);
    uint32_t saved_crc = save_size ? save_crc32(priv.cart_ram, save_size) : 0;

    // HOME is a function modifier, like a Fn key. While HOME is held the front
    // buttons control the emulator instead of the game, and their effect is
    // latched, so a change persists after the buttons are released:
    //   HOME + A    cycle speed: normal -> 2x -> max -> normal
    //   HOME + B    toggle GBC color correction
    //   HOME + UP   take a save-state snapshot (slot 0)
    //   HOME + DOWN restore the save-state snapshot (slot 0)
    //   HOME + C    open the in-game menu (slot selection, reset, and the above)
    // The cartridge battery save persists automatically; it needs no button.
    //
    // Fast-forward runs several emulated frames per displayed frame and skips
    // rendering and the (blocking) blit on the intermediate frames, so it speeds
    // up even though the display blit is the per-frame bottleneck.
    const uint32_t frame_us = 16743; // 59.7 Hz
    const int frames_per_blit[3] = {1, 2, 6};
    bool prev_a = false, prev_b = false, prev_up = false, prev_dn = false,
         prev_c = false;
    bool do_snapshot = false, do_restore = false;
    uint32_t indicator_until = 0;
    uint32_t note_until = 0; // transient marker for state save/restore
    uint16_t note_col = 0;

    while (1) {
        uint32_t t0 = time_us_32();

        // The menu is modal: while it is open the game is paused.
        if (g_menu_open) {
            menu_step();
            continue;
        }

        uint8_t jp;
        if (down(BTN_HOME)) {
            bool a = down(BTN_A), b = down(BTN_B);
            bool up = down(BTN_UP), dn = down(BTN_DOWN), c = down(BTN_C);
            if (a && !prev_a) {
                g_speed = (g_speed + 1) % 3;
                indicator_until = t0 + 1500000;
            }
            if (b && !prev_b) {
                color_correct = !color_correct;
                indicator_until = t0 + 1500000;
            }
            if (up && !prev_up)
                do_snapshot = true;
            if (dn && !prev_dn)
                do_restore = true;
            if (c && !prev_c) {
                menu_open();
            }
            prev_a = a;
            prev_b = b;
            prev_up = up;
            prev_dn = dn;
            prev_c = c;
            jp = 0xFF; // no game input while in function mode
        } else {
            prev_a = prev_b = prev_up = prev_dn = prev_c = false;
            jp = read_joypad();
        }
        gb.direct.joypad = jp;

        if (g_menu_open)
            continue; // HOME+C just opened the menu this frame

        int n = frames_per_blit[g_speed];
        for (int i = 0; i < n; i++) {
            // Render only the final emulated frame of the batch.
            gb.display.lcd_draw_line = (i == n - 1) ? &draw_line : NULL;
            gb_run_frame(&gb);
        }

        // Restore a snapshot: overwrite the machine state and cart RAM, then
        // re-link the callbacks and priv pointer to this build's code and data.
        if (do_restore) {
            bool ok = state_load(g_slot, &gb, sizeof(gb), priv.cart_ram,
                                 sizeof(priv.cart_ram), rid);
            if (ok) {
                relink_gb();
                ram_dirty = true; // let the restored cart RAM reach flash
                ram_write_us = time_us_32();
            }
            note_until = time_us_32() + 800000;
            note_col = ok ? 0x07E0 : 0xF800; // green ok, red failed
            do_restore = false;
        }

        if (g_speed != 0 || t0 < indicator_until)
            draw_indicator(priv.fb, g_speed, color_correct);
        if (time_us_32() < note_until)
            put_block(priv.fb, GB_W - 10, 4, 6, 6, note_col);

        lcd_blit_gb(priv.fb);

        // Take a snapshot: freeze briefly while the slot is erased and written.
        // Show a marker first so the pause is explained.
        if (do_snapshot) {
            put_block(priv.fb, GB_W - 10, 4, 6, 6, 0xF81F); // magenta
            lcd_blit_gb(priv.fb);
            bool ok = state_store(g_slot, &gb, sizeof(gb), priv.cart_ram,
                                  sizeof(priv.cart_ram), rid);
            note_until = time_us_32() + 800000;
            note_col = ok ? 0xF81F : 0xF800;
            do_snapshot = false;
        }

        // The cartridge battery save persists automatically once game writes
        // have settled for 1.5 s. A CRC check skips redundant writes to limit
        // flash wear. The flash program briefly freezes the emulator.
        bool settled = ram_dirty && (time_us_32() - ram_write_us > 1500000);
        if (save_size && settled) {
            uint32_t crc = save_crc32(priv.cart_ram, save_size);
            ram_dirty = false;
            if (crc != saved_crc) {
                put_block(priv.fb, GB_W - 10, 14, 6, 6, 0xFFFF);
                lcd_blit_gb(priv.fb);
                save_store(priv.cart_ram, save_size, rid);
                saved_crc = crc;
            }
        }

        // Speed 0 and 2x both pace to one 59.7 Hz display frame; 2x simply runs
        // two emulated frames inside that window. Maximum speed does not pace.
        if (g_speed != 2) {
            int32_t rem = (int32_t)frame_us - (int32_t)(time_us_32() - t0);
            if (rem > 0)
                sleep_us(rem);
        }
    }
}
