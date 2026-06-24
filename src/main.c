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
#include "tamzen7x14.h"

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

// Native-resolution canvas for the mod menu, drawn at the panel's full 320x240
// so text is crisp instead of upscaled from the Game Boy framebuffer.
static uint16_t menu_fb[LCD_W * LCD_H];

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

// A short on-screen message shown for a moment after an action (save, load).
static char g_toast[16] = "";
static uint32_t g_toast_until = 0;
static void toast(const char *m) {
    snprintf(g_toast, sizeof(g_toast), "%s", m);
    g_toast_until = time_us_32() + 1200000;
}

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

// Tamzen 7x14 menu font. FONT_ADV and FONT_LH are the horizontal and vertical
// advances (glyph size plus one pixel of spacing).
#define FONT_W 7
#define FONT_H 14
#define FONT_ADV 8
#define FONT_LH 15

// Draw one glyph into the byte-swapped framebuffer. The font is indexed from
// 0x20 (space) and stores the most significant bit as the leftmost column.
static void draw_char(uint16_t *fb, int x0, int y0, char ch, uint16_t col) {
    unsigned char uc = (unsigned char)ch;
    if (uc < 0x20 || uc > 0x7F)
        uc = '?';
    uint16_t sw = (uint16_t)((col >> 8) | (col << 8));
    const unsigned char *g = font_tamzen7x14[uc - 0x20];
    for (int r = 0; r < FONT_H; r++) {
        int y = y0 + r;
        if (y < 0 || y >= GB_H)
            continue;
        unsigned char bits = g[r];
        for (int c = 0; c < FONT_W; c++) {
            if (!((bits >> (7 - c)) & 1))
                continue;
            int x = x0 + c;
            if (x < 0 || x >= GB_W)
                continue;
            fb[y * GB_W + x] = sw;
        }
    }
}

// Draw text with a one-pixel drop shadow so it stays readable over game pixels.
static void draw_text(uint16_t *fb, int x0, int y0, const char *s, uint16_t col) {
    for (int x = x0; *s; s++, x += FONT_ADV) {
        draw_char(fb, x + 1, y0 + 1, *s, 0x0000); // shadow
        draw_char(fb, x, y0, *s, col);
    }
}

// Native-resolution drawing into menu_fb (320x240, byte-swapped RGB565).
static void m_fill(int x, int y, int w, int h, uint16_t col) {
    uint16_t be = (uint16_t)((col >> 8) | (col << 8));
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= LCD_H)
            continue;
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < LCD_W)
                menu_fb[yy * LCD_W + xx] = be;
    }
}

// Filled rectangle with rounded corners of radius r (Material-style surfaces).
static void m_round(int x, int y, int w, int h, uint16_t col, int r) {
    uint16_t be = (uint16_t)((col >> 8) | (col << 8));
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int cx = xx < r ? r : (xx >= w - r ? w - 1 - r : xx);
            int cy = yy < r ? r : (yy >= h - r ? h - 1 - r : yy);
            int dx = xx - cx, dy = yy - cy;
            if (dx * dx + dy * dy > r * r)
                continue;
            int px = x + xx, py = y + yy;
            if ((unsigned)px < LCD_W && (unsigned)py < LCD_H)
                menu_fb[py * LCD_W + px] = be;
        }
    }
}

// Draw a glyph scaled by an integer factor, with a one-step drop shadow.
static void m_glyph(int x0, int y0, char ch, uint16_t col, int s) {
    unsigned char uc = (unsigned char)ch;
    if (uc < 0x20 || uc > 0x7F)
        uc = '?';
    const unsigned char *g = font_tamzen7x14[uc - 0x20];
    uint16_t fg = (uint16_t)((col >> 8) | (col << 8));
    uint16_t sh = 0x0000;
    for (int r = 0; r < FONT_H; r++) {
        unsigned char bits = g[r];
        for (int c = 0; c < FONT_W; c++) {
            if (!((bits >> (7 - c)) & 1))
                continue;
            int px = x0 + c * s, py = y0 + r * s;
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++) {
                    int xx = px + dx, yy = py + dy;
                    if ((unsigned)(xx + s) < LCD_W && (unsigned)(yy + s) < LCD_H)
                        menu_fb[(yy + s) * LCD_W + (xx + s)] = sh; // shadow
                }
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++) {
                    int xx = px + dx, yy = py + dy;
                    if ((unsigned)xx < LCD_W && (unsigned)yy < LCD_H)
                        menu_fb[yy * LCD_W + xx] = fg;
                }
        }
    }
}

static void m_text(int x, int y, const char *s, uint16_t col, int scale) {
    for (; *s; s++, x += (FONT_W + 1) * scale)
        m_glyph(x, y, *s, col, scale);
}

static int m_text_w(const char *s, int scale) {
    int n = 0;
    while (*s++)
        n++;
    return n * (FONT_W + 1) * scale;
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
    g_menu_open = true;
    g_menu_sel = 0;
    g_status[0] = '\0';
}

static const char *menu_name(int item) {
    switch (item) {
    case MI_RESUME:
        return "Resume";
    case MI_SPEED:
        return "Speed";
    case MI_COLOR:
        return "Color filter";
    case MI_PALETTE:
        return "Palette";
    case MI_SLOT:
        return "Save slot";
    case MI_SAVE:
        return "Save state";
    case MI_LOAD:
        return "Load state";
    case MI_RESET:
        return "Reset game";
    }
    return "";
}

// Value text for items that have one (settings); empty for actions.
static void menu_value(int item, char *out, int n) {
    const char *speed_s[3] = {"Normal", "2x", "Max"};
    switch (item) {
    case MI_SPEED:
        snprintf(out, n, "%s", speed_s[g_speed]);
        break;
    case MI_COLOR:
        snprintf(out, n, "%s", color_correct ? "On" : "Off");
        break;
    case MI_PALETTE:
        snprintf(out, n, "%s", dmg_pal_names[dmg_pal_idx]);
        break;
    case MI_SLOT:
        snprintf(out, n, "%d", g_slot);
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

// The DMG palette only affects monochrome games, so hide it on Color games.
static bool menu_item_visible(int item) {
    if (item == MI_PALETTE)
        return !gb.cgb.cgbMode;
    return true;
}

// Move the selection to the next visible item in the given direction.
static int menu_move(int sel, int dir) {
    for (int k = 0; k < MI_COUNT; k++) {
        sel = (sel + MI_COUNT + dir) % MI_COUNT;
        if (menu_item_visible(sel))
            break;
    }
    return sel;
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
        g_menu_sel = menu_move(g_menu_sel, -1);
    if (d && !pd)
        g_menu_sel = menu_move(g_menu_sel, +1);
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

    // Material-style dark menu, drawn at the panel's native 320x240.
    enum {
        C_BG = 0x1082,     // near-black surface
        C_CARD = 0x2945,   // elevated selected row
        C_ACCENT = 0xCDFF, // lavender (Material You primary)
        C_TEAL = 0x06D8,   // value / confirmation accent
        C_TEXT = 0xEF7D,   // primary text
        C_DIM = 0x9CD3,    // secondary text
        C_FAINT = 0x6B6D,  // hints
    };
    m_fill(0, 0, LCD_W, LCD_H, C_BG);

    // Header: title, accent underline, version.
    m_text(16, 10, "Mod Menu", C_TEXT, 2);
    m_round(16, 42, 64, 4, C_ACCENT, 2);
    {
        const char *ver = "v" BADGEBOY_VERSION;
        m_text(LCD_W - 16 - m_text_w(ver, 1), 16, ver, C_DIM, 1);
    }
    m_fill(0, 52, LCD_W, 1, C_CARD);

    // Item list.
    const int top = 56, rh = 20;
    int row = 0;
    for (int i = 0; i < MI_COUNT; i++) {
        if (!menu_item_visible(i))
            continue;
        int y = top + row * rh;
        row++;
        bool sel = (i == g_menu_sel);
        uint16_t name_c = sel ? C_TEXT : C_DIM;
        uint16_t val_c = sel ? C_ACCENT : C_FAINT;
        if (sel) {
            m_round(12, y - 1, LCD_W - 24, rh - 2, C_CARD, 6);
            m_round(16, y + 3, 4, rh - 10, C_ACCENT, 2);
        }
        m_text(28, y + 3, menu_name(i), name_c, 1);
        char val[16];
        menu_value(i, val, sizeof(val));
        if (val[0])
            m_text(LCD_W - 24 - m_text_w(val, 1), y + 3, val, val_c, 1);
    }

    // Footer: a status line after an action, otherwise control hints.
    m_fill(16, LCD_H - 24, LCD_W - 32, 1, C_CARD);
    if (g_status[0]) {
        m_text((LCD_W - m_text_w(g_status, 1)) / 2, LCD_H - 18, g_status, C_TEAL, 1);
    } else {
        const char *hint = "Up/Dn  A select  C back  B close";
        m_text((LCD_W - m_text_w(hint, 1)) / 2, LCD_H - 18, hint, C_FAINT, 1);
    }

    if (g_menu_open == false)
        primed = false; // reset for next open
    lcd_blit_full(menu_fb);
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
            if (a && !prev_a)
                g_speed = (g_speed + 1) % 3;
            if (b && !prev_b)
                color_correct = !color_correct;
            if (up && !prev_up)
                do_snapshot = true;
            if (dn && !prev_dn)
                do_restore = true;
            if (c && !prev_c)
                menu_open();
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
            char m[16];
            if (state_load(g_slot, &gb, sizeof(gb), priv.cart_ram,
                           sizeof(priv.cart_ram), rid)) {
                relink_gb();
                ram_dirty = true; // let the restored cart RAM reach flash
                ram_write_us = time_us_32();
                snprintf(m, sizeof(m), "Loaded [%d]", g_slot);
            } else {
                snprintf(m, sizeof(m), "No state [%d]", g_slot);
            }
            toast(m);
            do_restore = false;
        }

        // A short text message in the corner, shown for a moment after actions.
        if (time_us_32() < g_toast_until)
            draw_text(priv.fb, 5, GB_H - FONT_H - 2, g_toast, 0x07FF);

        lcd_blit_gb(priv.fb);

        // Take a snapshot: this briefly freezes while the slot is written.
        if (do_snapshot) {
            char m[16];
            bool ok = state_store(g_slot, &gb, sizeof(gb), priv.cart_ram,
                                  sizeof(priv.cart_ram), rid);
            snprintf(m, sizeof(m), ok ? "Saved [%d]" : "Save fail", g_slot);
            toast(m);
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
                save_store(priv.cart_ram, save_size, rid);
                saved_crc = crc;
                toast("Game saved");
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
