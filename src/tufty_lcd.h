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

// Fill the whole panel with a single RGB565 colour.
void lcd_fill(uint16_t colour565);

// Scale and push a GB_W by GB_H framebuffer into the display window. The
// framebuffer must hold big-endian RGB565 values (see main.c).
void lcd_blit_gb(const uint16_t *fb);

#endif // TUFTY_LCD_H
