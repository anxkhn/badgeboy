#!/usr/bin/env python3
# BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
# Copyright (C) 2026 Anas Khan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run this on the badge (in MicroPython REPL mode, not disk mode) to print the
# real pin map, then copy the GPIO numbers into src/config.h.
#
#   pip install mpremote
#   sudo systemctl stop ModemManager            # stop it grabbing the port
#   sudo usermod -aG dialout "$USER"             # serial access (re-login after)
#   mpremote connect /dev/ttyACM0 run tools/dump_pinout.py
#
# It prints every named board pin: display bus, buttons, IR, backlight, power.

import machine

print("=== machine.Pin.board pins ===")
board = machine.Pin.board
for name in dir(board):
    if name.startswith("__"):
        continue
    try:
        print(f"{name:>16} = {getattr(board, name)}")
    except Exception as e:
        print(f"{name:>16} ! {e}")

try:
    import board as b
    print("\n=== board module constants ===")
    for name in dir(b):
        if name.startswith("__"):
            continue
        print(f"{name:>16} = {getattr(b, name)}")
except Exception as e:
    print("\n(no separate board module:", e, ")")
