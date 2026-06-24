# Flashing and restoring

Flashing BadgeBoy replaces the stock MonaOS firmware. MonaOS can be restored at
any time. Read the restore section before you flash if you want to keep MonaOS.

## Back up first

The badge exposes its data partition as a USB drive (double tap RESET to enter
disk mode). That partition is separate from the firmware. Copy it somewhere safe
before experimenting:

```bash
cp -a /run/media/$USER/BADGER ~/badge-backup-$(date +%Y%m%d)
```

This does not back up MonaOS itself. The MonaOS firmware image comes from the
badge releases page (see below).

## Flash BadgeBoy

1. Put the badge into BOOTSEL: hold BOOTSEL, tap RESET, release BOOTSEL. The
   badge mounts as a drive named `RP2350`.
2. Copy the image:
   ```bash
   cp build/badgeboy.uf2 /run/media/$USER/RP2350/
   ```
3. The badge reboots into the emulator.

## USB identifiers

Useful when scripting or checking state with `lsusb`:

| USB id        | Meaning                          |
|---------------|----------------------------------|
| `2e8a:000f`   | RP2350 BOOTSEL (ready to flash)  |
| `2e8a:0009`   | a running Pico application (BadgeBoy) |
| `2e8a:0005`   | MonaOS MicroPython               |

## Restore MonaOS

1. Download the latest factory `.uf2` from the badge releases page:
   https://github.com/badger/home/releases
2. Put the badge into BOOTSEL. On this badge you can also hold HOME and tap
   RESET to reach the flashing flow.
3. Copy the factory `.uf2` to the `RP2350` drive.
4. Restore the data partition if you changed it:
   ```bash
   cp -a ~/badge-backup-YYYYMMDD/BADGER/. /run/media/$USER/RP2350/
   ```

## Troubleshooting

- The `RP2350` drive does not appear: re-enter BOOTSEL. There can be a short
  delay before the host mounts it. Retry the copy.
- `cp` reports an error at the end of the copy: the bootloader can reboot as soon
  as it has the full image, which can interrupt the write notification. Verify by
  checking that the badge re-enumerated (`lsusb`).
- Permission denied on the serial port when using `tools/dump_pinout.py`: add
  your user to the `dialout` group and stop `ModemManager` from grabbing the
  port. See the comments in that script.
