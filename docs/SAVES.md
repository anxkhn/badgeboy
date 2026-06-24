# Saves and save states

BadgeBoy has two independent kinds of persistence, both handled by the flash
storage layer in `src/save.c`:

- **Battery saves**, the game's own save, persisting the cartridge's
  battery-backed SRAM.
- **Save states**, a full snapshot of the running machine, independent of the
  game's own save.

Each game in the launcher gets its own save area, so games never overwrite one
another. This document covers the mechanics and the debugging lessons learned.

## Game Boy battery saves

A Game Boy battery save persists the cartridge's battery-backed SRAM. For Pokemon
Red that is 32 KiB (four 8 KiB banks through the MBC3 chip). The game writes its
save into SRAM during the in-game SAVE, and on boot it reads SRAM back and
validates checksums to offer CONTINUE. BadgeBoy's job is only to persist and
restore that SRAM faithfully; the game's own logic decides whether a CONTINUE is
offered.

### Flat cart-RAM model

Peanut-GB exposes cart RAM through read and write callbacks that pass a flat
offset already adjusted for banking:

```
offset = (gb_address - 0xA000) + cart_ram_bank * 0x2000
```

Because the offset is already flat, one contiguous buffer is correct and no
per-bank handling is needed. The save buffer is a single contiguous SRAM image.

### Save size

The save size comes from ROM header byte `0x149`, mapped through
`{0, 0x800, 0x2000, 0x8000, 0x20000}` by `gb_get_save_size`. MBC2 is the special
case at `0x200`. BadgeBoy persists exactly this many bytes.

## On-flash format and tagging

`src/save.c` stores a battery save as a header followed by the SRAM bytes:

```
{ magic 0x56534242 "BBSV", version, rom_id, size }  then the SRAM bytes
```

`rom_id` is an FNV-1a hash of ROM header bytes `0x134` to `0x14F`, so a save
never loads for the wrong game. `save_load` accepts a stored save only when the
magic, version, `rom_id`, and size all match; otherwise it reports no save and
the game starts fresh.

## Per-game save areas

Saves live in the flash region above the ROM pack, starting at `GAMESAVE_BASE`
(`0xC80000`) and running to the top of flash (`0x1000000`). The layout is defined
in `src/flash_layout.h`:

- 8 game slots (`GAMESAVE_MAX`), 448 KiB each. 8 times 448 KiB fills exactly to
  the top.
- Each slot holds one 64 KiB battery-save block plus three 128 KiB save-state
  slots.

`save_set_game(index)` selects which per-game area the battery and state
functions use, keyed by the launcher index. The launcher sets this when a game is
chosen, so each game reads and writes only its own area.

## Save states

A save state is a full snapshot of the running machine: the whole `gb_s` struct
plus cart RAM, written to one of the three numbered state slots in the active
game's area. Each snapshot is tagged with the firmware build, so a state only
restores into the same build (the `gb_s` layout can change between builds). After
a load the emulator callbacks and the `priv` pointer are re-linked, because the
restored struct contains stale pointers from the build that saved it.

Save states are taken with HOME+UP and restored with HOME+DOWN, with slot
selection in the in-game menu. They are independent of the battery save.

## Auto-save policy

The battery save commits to flash automatically (`run_game` in `src/main.c`):

- A short time after the game's SRAM writes settle, about 1.2 s of quiet.
- Or after the data has been dirty for a few seconds, for games that write SRAM
  continuously (for example an in-game clock that never goes quiet).
- Rate limited to at most once every few seconds.
- Skipped by a CRC check when nothing changed, to limit flash wear.
- Force-flushed on Exit to menu, so returning to the launcher never loses a save.

> [!NOTE]
> This replaced an earlier 30 second throttle that could block the real in-game
> save from ever reaching flash. The symptom was a game that always showed NEW
> GAME and never CONTINUE.

## Lessons learned and troubleshooting

Three findings from bringing the save system up are worth recording.

### The store/load logic is host-verified

The store and load logic was proven byte-perfect off-device with a host test that
mocked flash as a RAM buffer: store then load round-trips exactly, and the loader
correctly rejects a wrong size or a wrong `rom_id`. So when saves misbehaved, the
storage logic was not the suspect, which pointed the investigation at the ROM
dump and the auto-save timing instead.

### A bad ROM dump can break saving

A hacked Pokemon Red dump (flagged `[S][BF]`) would not produce a usable
CONTINUE, while the canonical clean dump (USA, Europe, SGB Enhanced) saves and
continues correctly.

> [!TIP]
> Use clean, canonical ROM dumps. A bad or hacked dump can break the game's own
> save validation even when BadgeBoy persists the SRAM perfectly.

### The startup visual glitch

A "memory corrupted" looking glitch on the first instant a game started was
leftover framebuffer content: the launcher's or a previous game's last frame, or
uninitialised video memory shown before the game set up its tiles. It was not a
real corruption. The fix is to clear the framebuffer and the panel to black at
game start, and to run a few frames without drawing so the game initialises VRAM
before anything is shown.

## Debugging diagnostics

On-screen save diagnostics are gated behind the `BADGEBOY_DEBUG` build flag
(default OFF). When enabled, the launcher shows a SAVE verdict per game, the
in-game menu shows a live-versus-loaded checksum line, and a load result overlay
appears when a game starts. See [CONFIGURATION.md](CONFIGURATION.md).
