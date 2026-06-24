// BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
// Copyright (C) 2026 Anas Khan
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware configuration for the GitHub Universe 2025 badge (custom Pimoroni
// Tufty 2350, RP2350B). Every GPIO below was read from the badge firmware with
// tools/dump_pinout.py. See docs/HARDWARE.md for the full method and output.

#ifndef CONFIG_H
#define CONFIG_H

// Firmware version, shown in the mod menu. Keep in step with the release tag.
#define BADGEBOY_VERSION "0.6.0"

// Display: 8-bit parallel (8080) ST7789. The data bus is on GPIO 32-39, which
// only exist on the RP2350B package, so the build targets an RP2350B board and
// the PIO shifts its GPIO base to 16 (see tufty_lcd.c).
#define LCD_CS 27   // chip select
#define LCD_DC 28   // data/command (board name: LCD_RS)
#define LCD_WR 30   // write strobe, driven by the PIO side-set
#define LCD_RD 31   // read strobe, held high
#define LCD_D0 32   // data bus D0..D7 = GPIO 32..39
#define LCD_BL 26   // backlight, PWM
#define POWER_EN 41 // board power rail, driven high to hold it on
// There is no display reset line on this board; the ST7789 software reset is used.

#define LCD_W 320 // panel width
#define LCD_H 240 // panel height

// Buttons, active low with internal pull-ups.
#define BTN_A 7
#define BTN_B 8
#define BTN_C 9
#define BTN_UP 10 // note: UP is GPIO 10 on this board, not 22
#define BTN_DOWN 6
#define BTN_HOME 22 // back button, reserved

// Native Game Boy resolution.
#define GB_W 160
#define GB_H 144

// Display scaling, selected at build time with -DDISPLAY_MODE.
//   FULLSCREEN: stretch to 320x240, fills the panel.
//   ASPECT:     266x240, correct aspect ratio with side borders.
//   CENTERED:   native 160x144 in the centre with a black border.
#define DISP_FULLSCREEN 0
#define DISP_ASPECT 1
#define DISP_CENTERED 2

#ifndef DISPLAY_MODE
#define DISPLAY_MODE DISP_FULLSCREEN
#endif

#if DISPLAY_MODE == DISP_CENTERED
#define GB_DRAW_W 160
#define GB_DRAW_H 144
#elif DISPLAY_MODE == DISP_ASPECT
#define GB_DRAW_W 266
#define GB_DRAW_H 240
#else
#define GB_DRAW_W 320
#define GB_DRAW_H 240
#endif

#define GB_X_OFF ((LCD_W - GB_DRAW_W) / 2)
#define GB_Y_OFF ((LCD_H - GB_DRAW_H) / 2)

// GBC color correction. When 1, Game Boy Color titles are rendered through the
// Gambatte color matrix, which reproduces the washed look of the real CGB LCD
// instead of the oversaturated raw RGB555. This is the power-on default; it can
// be toggled at runtime with C+HOME. Set to 0 to default to raw colors.
#ifndef GBC_COLOR_CORRECTION
#define GBC_COLOR_CORRECTION 1
#endif

// DMG palette for original (non-color) Game Boy titles. Index into the
// dmg_palettes table in main.c:
//   0 authentic DMG green   1 Game Boy Pocket grey
//   2 high-contrast mono     3 dusk amber
#ifndef DMG_PALETTE
#define DMG_PALETTE 2
#endif

#endif // CONFIG_H
