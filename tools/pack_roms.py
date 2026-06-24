#!/usr/bin/env python3
# BadgeBoy - Game Boy / Game Boy Color emulator for the GitHub Universe badge
# Copyright (C) 2026 Anas Khan
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pack several Game Boy ROMs into a single image and emit a flashable UF2.

The ROM pack is a separate flash image from the firmware: it is written at a
fixed high offset, so reflashing your game library leaves the firmware in place.
Drag the resulting .uf2 onto the badge in BOOTSEL just like the firmware.

Usage:
    tools/pack_roms.py -o games.uf2 game1.gb "game 2.gbc" ...

Layout (little endian), matching src/flash_layout.h:
    header   : magic, version, count, reserved          (16 bytes)
    entries  : count * { title[32], offset, size, flags, pad[23] }  (64 each)
    <pad to 4 KB>
    rom data : each ROM padded up to a 4 KB boundary
"""

import argparse
import os
import re
import struct
import sys

# Must match src/flash_layout.h.
ROMPACK_OFFSET = 4 * 1024 * 1024  # 0x400000
ROMPACK_MAX_SIZE = 0xC80000 - ROMPACK_OFFSET
ROMPACK_MAGIC = 0x4B504242  # "BBPK"
ROMPACK_VERSION = 1
TITLE_LEN = 32
ENTRY_SIZE = 64
HEADER_SIZE = 16
FLAG_CGB = 0x01
SECTOR = 4096

XIP_BASE = 0x10000000
UF2_FAMILY_ABSOLUTE = 0xE48BFF57  # RP2 absolute/data family, accepted by BOOTSEL


def rom_title(data: bytes, fallback: str) -> str:
    """A clean display name, taken from the file name with dump tags removed.

    File names like "Pokemon Pinball (E) (M5) [C][!].gbc" read far better than the
    cramped, upper-case cartridge header, so the file name wins; the header is only
    a last resort for an unnamed file.
    """
    name = os.path.splitext(os.path.basename(fallback))[0]
    name = re.sub(r"[\(\[].*?[\)\]]", "", name)   # drop (E), [!], [C], ...
    name = name.replace("_", " ").replace(" - ", " ")
    name = re.sub(r"\s+", " ", name).strip(" -")
    if not name:
        raw = data[0x134 : (0x13F if (data[0x143] & 0x80) else 0x143)]
        name = "".join(chr(b) for b in raw if 0x20 <= b < 0x7F).strip()
    if not name:
        name = "Game"
    return name[: TITLE_LEN - 1]


def is_cgb(data: bytes) -> bool:
    return bool(data[0x143] & 0x80)


def build_pack(rom_paths):
    roms = []
    for path in rom_paths:
        with open(path, "rb") as f:
            data = f.read()
        if len(data) < 0x150:
            sys.exit(f"error: {path} is too small to be a Game Boy ROM")
        roms.append((path, data))

    count = len(roms)
    table_end = HEADER_SIZE + count * ENTRY_SIZE
    data_start = (table_end + SECTOR - 1) & ~(SECTOR - 1)

    entries = bytearray()
    blob = bytearray()
    cursor = data_start
    for path, data in roms:
        title = rom_title(data, path).encode("ascii", "replace")
        title = title[: TITLE_LEN - 1].ljust(TITLE_LEN, b"\x00")
        flags = FLAG_CGB if is_cgb(data) else 0
        entries += struct.pack(
            f"<{TITLE_LEN}sIIB23x", title, cursor, len(data), flags
        )
        blob += data
        pad = (-len(data)) % SECTOR
        blob += b"\xff" * pad
        cursor += len(data) + pad

    header = struct.pack("<IIII", ROMPACK_MAGIC, ROMPACK_VERSION, count, 0)
    table = header + entries
    table += b"\xff" * (data_start - len(table))
    image = bytes(table) + bytes(blob)

    if len(image) > ROMPACK_MAX_SIZE:
        sys.exit(
            f"error: pack is {len(image)} bytes, over the "
            f"{ROMPACK_MAX_SIZE} byte limit ({ROMPACK_MAX_SIZE // 1024 // 1024} MB)"
        )
    return image, roms


def to_uf2(image: bytes, load_addr: int) -> bytes:
    UF2_MAGIC0 = 0x0A324655
    UF2_MAGIC1 = 0x9E5D5157
    UF2_MAGIC_END = 0x0AB16F30
    FLAG_FAMILY = 0x00002000
    PAYLOAD = 256

    padded = image + b"\xff" * ((-len(image)) % PAYLOAD)
    num_blocks = len(padded) // PAYLOAD
    out = bytearray()
    for i in range(num_blocks):
        chunk = padded[i * PAYLOAD : (i + 1) * PAYLOAD]
        block = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC0,
            UF2_MAGIC1,
            FLAG_FAMILY,
            load_addr + i * PAYLOAD,
            PAYLOAD,
            i,
            num_blocks,
            UF2_FAMILY_ABSOLUTE,
        )
        block += chunk + b"\x00" * (476 - PAYLOAD)
        block += struct.pack("<I", UF2_MAGIC_END)
        out += block
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="Pack Game Boy ROMs into a UF2 image.")
    ap.add_argument("-o", "--output", required=True, help="output .uf2 path")
    ap.add_argument("--bin", help="also write the raw pack image to this path")
    ap.add_argument("roms", nargs="+", help="ROM files to include, in menu order")
    args = ap.parse_args()

    if len(args.roms) > 64:
        sys.exit("error: at most 64 ROMs per pack")

    image, roms = build_pack(args.roms)
    uf2 = to_uf2(image, XIP_BASE + ROMPACK_OFFSET)

    with open(args.output, "wb") as f:
        f.write(uf2)
    if args.bin:
        with open(args.bin, "wb") as f:
            f.write(image)

    print(f"packed {len(roms)} ROM(s), {len(image)} bytes -> {args.output}")
    for path, data in roms:
        kind = "CGB" if is_cgb(data) else "DMG"
        print(f"  [{kind}] {rom_title(data, path)}  ({len(data)} bytes)")


if __name__ == "__main__":
    main()
