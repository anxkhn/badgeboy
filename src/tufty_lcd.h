// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TUFTY_LCD_H
#define TUFTY_LCD_H

#include <stdint.h>
#include <stddef.h>

// Initialise the PIO, DMA, control pins, and run the ST7789 init sequence.
void lcd_init(void);

// Set the backlight brightness, 0 to 255, gamma corrected.
void lcd_set_backlight(uint8_t brightness);

// Set the backlight to a linear PWM duty cycle, 0 to 100 percent (no gamma), so
// low percentages stay visible. This is a pure hardware backlight level.
void lcd_set_backlight_pct(uint8_t pct);

// Fill the whole panel with a single RGB565 colour.
void lcd_fill(uint16_t colour565);

// Scale and push a GB_W by GB_H framebuffer into the display window. The
// framebuffer must hold big-endian RGB565 values (see main.c).
void lcd_blit_gb(const uint16_t *fb);

// Select the display shader applied during lcd_blit_gb. 0 off, 1 scanlines,
// 2 dot-matrix grid. The effect tracks the source pixel grid as it scales.
void lcd_set_shader(int mode);

// Push a full LCD_W by LCD_H native-resolution frame to the panel. Used to draw
// the mod menu crisply, bypassing the Game Boy upscale. Big-endian RGB565.
void lcd_blit_full(const uint16_t *fb);

#endif // TUFTY_LCD_H
