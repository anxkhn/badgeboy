// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 8-bit parallel (8080) ST7789 driver for the badge panel. The initialisation
// sequence and register values follow Pimoroni's ST7789 driver for the 320x240
// panel. Pixel writes go through a PIO program (st7789_parallel.pio) fed by DMA.

#include "tufty_lcd.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <math.h>

#include "st7789_parallel.pio.h" // generated from the .pio by CMake

static PIO pio = pio0;
static uint sm;
static int dma_chan;

enum {
    SWRESET = 0x01,
    SLPOUT = 0x11,
    INVON = 0x21,
    DISPON = 0x29,
    CASET = 0x2A,
    RASET = 0x2B,
    RAMWR = 0x2C,
    COLMOD = 0x3A,
    TEON = 0x35,
    MADCTL = 0x36,
    PORCTRL = 0xB2,
    GCTRL = 0xB7,
    VCOMS = 0xBB,
    LCMCTRL = 0xC0,
    VDVVRHEN = 0xC2,
    VRHS = 0xC3,
    VDVS = 0xC4,
    FRCTRL2 = 0xC6,
    PWCTRL1 = 0xD0,
    RAMCTRL = 0xB0,
    GMCTRP1 = 0xE0,
    GMCTRN1 = 0xE1
};

enum {
    MADCTL_COL = 0x40,
    MADCTL_ROW = 0x80,
    MADCTL_SWAP_XY = 0x20,
    MADCTL_SCAN = 0x10
};

static inline void wr_blocking(const uint8_t *src, size_t len) {
    dma_channel_set_trans_count(dma_chan, len, false);
    dma_channel_set_read_addr(dma_chan, src, true);
    dma_channel_wait_for_finish_blocking(dma_chan);
    while (!pio_sm_is_tx_fifo_empty(pio, sm))
        tight_loop_contents();
    sleep_us(1); // let the PIO clock out the final byte before CS deassert
}

static void cmd(uint8_t c, size_t len, const uint8_t *data) {
    gpio_put(LCD_DC, 0);
    gpio_put(LCD_CS, 0);
    wr_blocking(&c, 1);
    if (data && len) {
        gpio_put(LCD_DC, 1);
        wr_blocking(data, len);
    }
    gpio_put(LCD_CS, 1);
}
#define CMD0(c) cmd((c), 0, NULL)
#define CMDN(c, ...)                                                                   \
    do {                                                                               \
        static const uint8_t _d[] = {__VA_ARGS__};                                     \
        cmd((c), sizeof(_d), _d);                                                      \
    } while (0)

static void pio_dma_setup(void) {
    // The data bus is on GPIO 32-39 and WR on GPIO 30. A PIO addresses only a
    // 32-pin window, so move this PIO's window to GPIO 16-47.
    pio_set_gpio_base(pio, 16);

    uint offset = pio_add_program(pio, &st7789_parallel_program);
    sm = pio_claim_unused_sm(pio, true);

    pio_sm_config c = st7789_parallel_program_get_default_config(offset);
    for (int i = 0; i < 8; i++)
        pio_gpio_init(pio, LCD_D0 + i);
    pio_gpio_init(pio, LCD_WR);
    pio_sm_set_consecutive_pindirs(pio, sm, LCD_D0, 8, true);
    pio_sm_set_consecutive_pindirs(pio, sm, LCD_WR, 1, true);

    sm_config_set_out_pins(&c, LCD_D0, 8);
    sm_config_set_sideset_pins(&c, LCD_WR);
    sm_config_set_out_shift(&c, true, true, 8); // shift right, autopull, threshold 8
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, 4.0f); // about 30 MHz byte clock

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_chan, &dc, &pio->txf[sm], NULL, 0, false);
}

void lcd_set_backlight(uint8_t b) {
    float gamma = 2.8f;
    uint16_t v = (uint16_t)(powf((float)b / 255.0f, gamma) * 65535.0f + 0.5f);
    pwm_set_gpio_level(LCD_BL, v);
}

void lcd_init(void) {
    // Hold the board power rail on. Required on battery; harmless on USB.
    gpio_init(POWER_EN);
    gpio_set_dir(POWER_EN, GPIO_OUT);
    gpio_put(POWER_EN, 1);

    gpio_init(LCD_CS);
    gpio_set_dir(LCD_CS, GPIO_OUT);
    gpio_put(LCD_CS, 1);
    gpio_init(LCD_DC);
    gpio_set_dir(LCD_DC, GPIO_OUT);
    gpio_put(LCD_DC, 1);
    gpio_init(LCD_RD);
    gpio_set_dir(LCD_RD, GPIO_OUT);
    gpio_put(LCD_RD, 1);

    pwm_config pc = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(LCD_BL), 65535);
    pwm_init(pwm_gpio_to_slice_num(LCD_BL), &pc, true);
    gpio_set_function(LCD_BL, GPIO_FUNC_PWM);
    lcd_set_backlight(0);

    pio_dma_setup();

    CMD0(SWRESET);
    sleep_ms(150);
    CMD0(TEON);
    CMDN(COLMOD, 0x05); // 16 bits per pixel
    CMDN(PORCTRL, 0x0c, 0x0c, 0x00, 0x33, 0x33);
    CMDN(LCMCTRL, 0x2c);
    CMDN(VDVVRHEN, 0x01);
    CMDN(VRHS, 0x12);
    CMDN(VDVS, 0x20);
    CMDN(PWCTRL1, 0xa4, 0xa1);
    CMDN(FRCTRL2, 0x0f);
    CMDN(RAMCTRL, 0x00, 0xc0);
    CMDN(GCTRL, 0x35);
    CMDN(VCOMS, 0x1f);
    CMDN(GMCTRP1, 0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13,
         0x14, 0x29, 0x2D);
    CMDN(GMCTRN1, 0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16,
         0x14, 0x2F, 0x31);
    CMD0(INVON);
    CMD0(SLPOUT);
    CMD0(DISPON);
    sleep_ms(100);

    // The panel is mounted rotated, so apply a 180 degree rotation.
    uint8_t madctl = MADCTL_ROW | MADCTL_SWAP_XY | MADCTL_SCAN;
    cmd(MADCTL, 1, &madctl);

    lcd_fill(0x0000);
    sleep_ms(20);
    lcd_set_backlight(255);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t cd[4];
    cd[0] = x0 >> 8;
    cd[1] = x0 & 0xff;
    cd[2] = x1 >> 8;
    cd[3] = x1 & 0xff;
    cmd(CASET, 4, cd);
    cd[0] = y0 >> 8;
    cd[1] = y0 & 0xff;
    cd[2] = y1 >> 8;
    cd[3] = y1 & 0xff;
    cmd(RASET, 4, cd);
}

void lcd_fill(uint16_t colour) {
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_put(LCD_DC, 0);
    gpio_put(LCD_CS, 0);
    uint8_t c = RAMWR;
    wr_blocking(&c, 1);
    gpio_put(LCD_DC, 1);
    static uint16_t line[LCD_W];
    uint16_t be = (colour >> 8) | (colour << 8);
    for (int i = 0; i < LCD_W; i++)
        line[i] = be;
    for (int y = 0; y < LCD_H; y++)
        wr_blocking((const uint8_t *)line, LCD_W * 2);
    gpio_put(LCD_CS, 1);
}

static int g_shader = 0;

void lcd_set_shader(int mode) {
    g_shader = mode;
}

// Scale a big-endian RGB565 value by num/16, per channel. Used by the shaders
// to darken scanlines and the dot-matrix grid without leaving RGB565.
static inline uint16_t dim16_be(uint16_t be, uint32_t num) {
    uint16_t c = (uint16_t)((be >> 8) | (be << 8));
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (r * num) >> 4;
    g = (g * num) >> 4;
    b = (b * num) >> 4;
    c = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((c >> 8) | (c << 8));
}

// Edge falloff weight in sixteenths: full (16) in the centre, tapering toward a
// floor near each edge. Used to build the vignette.
static int edge_weight(int i, int n) {
    const int margin = 28, floor = 11;
    int d = i < (n - 1 - i) ? i : (n - 1 - i);
    if (d >= margin)
        return 16;
    return floor + (16 - floor) * d / margin;
}

void lcd_blit_gb(const uint16_t *fb) {
    // Nearest-neighbour scale of the 160x144 frame into the configured window.
    static uint16_t outline[GB_DRAW_W];
    static int colmap[GB_DRAW_W];
    static bool coledge[GB_DRAW_W];
    static int coldim[GB_DRAW_W];
    static int rowdim[GB_DRAW_H];
    static bool maps_ready = false;
    if (!maps_ready) {
        for (int ox = 0; ox < GB_DRAW_W; ox++) {
            colmap[ox] = (ox * GB_W) / GB_DRAW_W;
            coledge[ox] = (ox == 0) || (colmap[ox] != colmap[ox - 1]);
            coldim[ox] = edge_weight(ox, GB_DRAW_W);
        }
        for (int oy = 0; oy < GB_DRAW_H; oy++)
            rowdim[oy] = edge_weight(oy, GB_DRAW_H);
        maps_ready = true;
    }

    set_window(GB_X_OFF, GB_Y_OFF, GB_X_OFF + GB_DRAW_W - 1, GB_Y_OFF + GB_DRAW_H - 1);
    gpio_put(LCD_DC, 0);
    gpio_put(LCD_CS, 0);
    uint8_t c = RAMWR;
    wr_blocking(&c, 1);
    gpio_put(LCD_DC, 1);

    int prev_src = -1;
    for (int oy = 0; oy < GB_DRAW_H; oy++) {
        int src = (oy * GB_H) / GB_DRAW_H;
        const uint16_t *srow = &fb[src * GB_W];
        bool scan_dark = (oy & 1);
        bool row_edge = (src != prev_src);
        switch (g_shader) {
        case 1: // Scanlines: darken alternate physical rows.
            for (int ox = 0; ox < GB_DRAW_W; ox++) {
                uint16_t px = srow[colmap[ox]];
                outline[ox] = scan_dark ? dim16_be(px, 10) : px;
            }
            break;
        case 2: // Dot-matrix: darken the leading edge of each source cell.
            for (int ox = 0; ox < GB_DRAW_W; ox++) {
                uint16_t px = srow[colmap[ox]];
                outline[ox] = (row_edge || coledge[ox]) ? dim16_be(px, 11) : px;
            }
            break;
        case 3: // Retro LCD: dot-matrix grid and scanlines together.
            for (int ox = 0; ox < GB_DRAW_W; ox++) {
                uint16_t px = srow[colmap[ox]];
                uint32_t f = 16;
                if (scan_dark)
                    f = 12;
                if (row_edge || coledge[ox])
                    f = (f * 12) >> 4;
                outline[ox] = (f < 16) ? dim16_be(px, f) : px;
            }
            break;
        case 4: // Vignette: darken toward the edges and corners.
            for (int ox = 0; ox < GB_DRAW_W; ox++) {
                uint16_t px = srow[colmap[ox]];
                uint32_t f = ((uint32_t)coldim[ox] * rowdim[oy]) >> 4;
                outline[ox] = (f < 16) ? dim16_be(px, f) : px;
            }
            break;
        default: // 0: no shader.
            for (int ox = 0; ox < GB_DRAW_W; ox++)
                outline[ox] = srow[colmap[ox]];
            break;
        }
        prev_src = src;
        wr_blocking((const uint8_t *)outline, GB_DRAW_W * 2);
    }
    gpio_put(LCD_CS, 1);
}

void lcd_blit_full(const uint16_t *fb) {
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_put(LCD_DC, 0);
    gpio_put(LCD_CS, 0);
    uint8_t c = RAMWR;
    wr_blocking(&c, 1);
    gpio_put(LCD_DC, 1);
    wr_blocking((const uint8_t *)fb, LCD_W * LCD_H * 2);
    gpio_put(LCD_CS, 1);
}
