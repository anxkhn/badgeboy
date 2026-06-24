# Hardware

The GitHub Universe 2025 badge is a custom Pimoroni Tufty 2350. This document
records the verified hardware facts BadgeBoy depends on, how the pin map was
obtained, and how to reproduce it.

## Chip

- RP2350B, the 48-GPIO package. This is load-bearing knowledge: the display data
  bus is on GPIO 32 to 39, which only exist on the B package. A 30-GPIO RP2350A
  (the common Pico 2) cannot drive this display.
- Dual Cortex-M33, 520 KB SRAM, external QSPI flash.

## Module and memory

The badge is built around what is effectively a Pimoroni Pico Plus 2 W. The
relevant figures were confirmed from the board profile, the Pimoroni spec, and
the stock firmware:

| Resource | Value | Notes |
|----------|-------|-------|
| Flash    | 16 MB QSPI, XiP | Room for a ROM library plus save slots |
| PSRAM    | 8 MB, CS on GPIO 47 | Free space for framebuffers, shaders, NES |
| Wireless | CYW43439 (WiFi and Bluetooth) | Stock MonaOS connects with `network.WLAN` |
| Audio    | none | No speaker, codec, DAC, or jack |

Two consequences for firmware design:

- There is generous storage and RAM. Multiple ROMs, save states, and a second
  console core all fit. The constraint is engineering time, not memory.
- There is no audio path at all. Stock MonaOS contains no sound code. Producing
  sound requires a hardware mod (an external I2S DAC on the Qwiic/STEMMA bus, or
  PWM plus an RC filter to a wired jack). Bluetooth earphone (A2DP) audio over
  the CYW43439 is not practical on this SDK stack.

## Display

A 320x240 ST7789 driven over an 8-bit parallel 8080 bus, not SPI. This is the
most important fact for anyone reusing this work: SPI ST7789 drivers do not
apply. Pixel data is clocked out by a PIO program and fed by DMA.

## How the pin map was obtained

The stock Pimoroni Tufty 2040/2350 pinout does not match this board. The first
clue was that the badge's own `quest` app uses an IR receiver on GPIO 21, and
GPIO 21 is a display data pin on a stock Tufty. So the pins had to be read from
the device itself rather than assumed.

The badge runs MicroPython and exposes every named pin through
`machine.Pin.board`. `tools/dump_pinout.py` prints them. To run it you talk to
the badge over its USB serial REPL.

1. Put the badge in normal (REPL) mode, not disk mode and not BOOTSEL.
2. Install mpremote and clear two common blockers on Linux:

   ```bash
   pip install mpremote
   sudo systemctl stop ModemManager        # ModemManager grabs new ttyACM ports
   sudo usermod -aG dialout "$USER"         # serial permission; re-login to apply
   ```

   Without `dialout` membership, opening `/dev/ttyACM0` fails with permission
   denied, which mpremote reports as the port being in use. ModemManager probing
   the port causes the same symptom for the first several seconds after plug-in.

3. Dump the pins:

   ```bash
   mpremote connect /dev/ttyACM0 run tools/dump_pinout.py
   ```

### Raw output from this badge

```
        BUTTON_A = Pin(GPIO7,  mode=IN,  pull=PULL_UP)
        BUTTON_B = Pin(GPIO8,  mode=IN,  pull=PULL_UP)
        BUTTON_C = Pin(GPIO9,  mode=IN,  pull=PULL_UP)
     BUTTON_DOWN = Pin(GPIO6,  mode=IN,  pull=PULL_UP)
     BUTTON_HOME = Pin(GPIO22, mode=IN,  pull=PULL_UP)
       BUTTON_UP = Pin(GPIO10, mode=IN,  pull=PULL_UP)
   LCD_BACKLIGHT = Pin(GPIO26, mode=ALT, pull=PULL_DOWN, alt=PWM)
          LCD_CS = Pin(GPIO27, mode=OUT, pull=PULL_DOWN)
          LCD_RS = Pin(GPIO28, mode=OUT)
          LCD_WR = Pin(GPIO30, mode=ALT, pull=PULL_DOWN, alt=7)
          LCD_RD = Pin(GPIO31, mode=OUT, pull=PULL_DOWN)
         LCD_DB0 = Pin(GPIO32, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB1 = Pin(GPIO33, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB2 = Pin(GPIO34, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB3 = Pin(GPIO35, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB4 = Pin(GPIO36, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB5 = Pin(GPIO37, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB6 = Pin(GPIO38, mode=ALT, pull=PULL_DOWN, alt=7)
         LCD_DB7 = Pin(GPIO39, mode=ALT, pull=PULL_DOWN, alt=7)
           IR_RX = Pin(GPIO21, mode=ALT, pull=PULL_DOWN, alt=31)
           IR_TX = Pin(GPIO20, mode=ALT, pull=PULL_DOWN, alt=31)
        POWER_EN = Pin(GPIO41, mode=OUT, pull=PULL_DOWN)
```

(Other pins are present too: I2C on 4 and 5, charge and battery sense, the
wireless block on 23 to 29, and CL0 to CL3 on 0 to 3. BadgeBoy does not use them.)

## Verified pin map (what BadgeBoy uses)

These values are in `src/config.h`.

### Display

| Signal      | GPIO  | Notes                                |
|-------------|-------|--------------------------------------|
| CS          | 27    | chip select, software driven         |
| DC / RS     | 28    | data/command (register select)       |
| WR          | 30    | write strobe, PIO side-set           |
| RD          | 31    | read strobe, held high               |
| D0 .. D7    | 32-39 | 8-bit data bus, 8 consecutive GPIOs  |
| BACKLIGHT   | 26    | PWM, gamma corrected in software     |
| (reset)     | none  | no reset line; SWRESET used instead  |

### Power

| Signal     | GPIO | Notes                          |
|------------|------|--------------------------------|
| POWER_EN   | 41   | driven high to hold the rail   |

### Buttons (active low, internal pull-ups)

| Button | GPIO |
|--------|------|
| A      | 7    |
| B      | 8    |
| C      | 9    |
| UP     | 10   |
| DOWN   | 6    |
| HOME   | 22   |

UP is GPIO 10 on this board, not 22. On the stock Tufty, 22 is UP; here 22 is
HOME. This is one of several differences from stock hardware.

### Infrared (not used by BadgeBoy)

| Signal | GPIO |
|--------|------|
| IR_RX  | 21   |
| IR_TX  | 20   |

## Reaching the data bus from PIO

A PIO state machine can only address a 32-pin window. The data bus (32-39) and
WR (30) are above GPIO 31, so the LCD PIO calls `pio_set_gpio_base(pio0, 16)`,
which gives it a window of GPIO 16 to 47. CS and DC are ordinary software-driven
outputs and are not affected by the window.

## Bring-up symptom table

If you change the display path, use this to localise faults. Fixes are in
`src/tufty_lcd.c` unless noted.

| Symptom                       | Likely cause            | Fix |
|-------------------------------|-------------------------|-----|
| Fully dark, no glow           | backlight or power rail | check `LCD_BL` PWM and `POWER_EN`; the LEDs on GPIO 0-3 may also gate light |
| Backlight on, no image        | data bus or PIO window  | confirm `pio_set_gpio_base(pio, 16)`, `LCD_D0`, and the WR pin |
| Garbled or torn pixels        | PIO clock too fast      | raise the clock divider (currently 4.0) toward 6 to 8 |
| Image upside down or mirrored | MADCTL                  | adjust the row, column, and swap bits |
| Image shifted or clipped      | window offsets          | check `set_window` and `GB_X_OFF`, `GB_Y_OFF` |
| Backlight pulses rhythmically | firmware hard fault     | the init-failure signal in `main.c`; gb_init failed |
