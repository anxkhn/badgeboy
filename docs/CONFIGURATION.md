# Configuration

Configured at build time: which ROM is embedded, how the image is scaled, the
monochrome palette, and the default color correction state. All can be passed to
`build.sh` or set as CMake cache variables.

## ROM selection

BadgeBoy embeds exactly one ROM per build. Supply your own legally obtained file.

```bash
./build.sh /absolute/path/to/game.gbc
```

or

```bash
cmake -B build -DGBC_ROM=/absolute/path/to/game.gbc ...
```

The file is converted to a flash-resident C array by `tools/rom2c.py`. The
converter prints the parsed header so you can confirm the title, mode (DMG, GBC
enhanced, or GBC only), and cartridge type. Supported sizes are anything that
fits the badge flash, which comfortably covers GB and GBC titles.

The built-in ROM becomes the first game in the on-device launcher (game index 0).
Additional games come from a separately flashed ROM pack, so you do not have to
rebuild the firmware to change your library. See the [ROM pack](#rom-pack)
subsection below, [docs/FLASHING.md](FLASHING.md) for flashing a pack, and
[docs/SAVES.md](SAVES.md) for how each game gets its own save area.

To convert a ROM by hand, for inspection:

```bash
python3 tools/rom2c.py game.gbc /tmp/gb_rom.h
```

The generated header and any built image contain copyrighted data. They are
ignored by Git and must not be committed or distributed.

## ROM pack

The launcher's additional games come from a ROM pack: a separate flash image
built on the host by `tools/pack_roms.py`. It is independent of the firmware, so
updating your library does not touch the firmware or your saves.

```bash
python3 tools/pack_roms.py -o games.uf2 game1.gb "game 2.gbc" ...
```

The tool emits a flashable `games.uf2`, derives clean display titles from the
file names (stripping bracketed and parenthesized dump tags), marks each game as
CGB or DMG, and 4 KiB-aligns each ROM. The firmware reads the pack in place from
memory-mapped flash. The total game count (built-in plus pack) is capped at 8 so
each game keeps its own save area.

Flash the pack by copying `games.uf2` to the `RP2350` BOOTSEL drive, exactly like
the firmware; it writes only the pack region. See [docs/FLASHING.md](FLASHING.md)
and [docs/SAVES.md](SAVES.md).

> [!IMPORTANT]
> Build packs only from software you legally own. Do not commit or distribute the
> generated `.uf2`; it contains copyrighted data.

## Debug build

`BADGEBOY_DEBUG` is a CMake option (default OFF) that gates on-screen save
diagnostics. Enable it with the environment variable, which `build.sh` forwards:

```bash
BADGEBOY_DEBUG=ON ./build.sh /absolute/path/to/game.gbc fullscreen
```

or directly with CMake:

```bash
cmake -B build -DBADGEBOY_DEBUG=ON ...
```

When on, it shows a SAVE verdict per game in the launcher, a live-versus-loaded
checksum line in the in-game menu, and a load result overlay when a game starts.
Normal builds show none of this.

## Display mode

The Game Boy renders at 160x144. The panel is 320x240. Three modes decide how
the frame is placed.

| Mode         | On-screen size | Appearance |
|--------------|----------------|------------|
| `FULLSCREEN` | 320x240        | fills the panel. Slight horizontal stretch because the panel area is wider than the 10:9 Game Boy ratio. This is the default. |
| `ASPECT`     | 266x240        | correct aspect ratio, thin black borders on the left and right. |
| `CENTERED`   | 160x144        | native pixels, 1:1, in the centre with a black border. This is how early builds looked. |

Select it as the second argument to `build.sh`:

```bash
./build.sh ~/roms/game.gbc fullscreen
./build.sh ~/roms/game.gbc aspect
./build.sh ~/roms/game.gbc centered
```

or with CMake:

```bash
cmake -B build -DGBC_DISPLAY_MODE=ASPECT ...
```

Scaling is nearest neighbour. The mode maps to `DISPLAY_MODE` in `src/config.h`,
which sets `GB_DRAW_W`, `GB_DRAW_H`, and the centering offsets. To add a custom
size, edit the `DISPLAY_MODE` block in `config.h`.

## Rendering options

Two rendering options have build-time defaults.

| Option | Values | Default | Meaning |
|--------|--------|---------|---------|
| `DMG_PALETTE` | 0..3 | 2 | Palette for monochrome (DMG) games: 0 authentic green, 1 Game Boy Pocket grey, 2 high-contrast mono, 3 dusk amber. The default is 2 (high-contrast mono). No effect on Game Boy Color titles. |
| `GBC_COLOR_CORRECTION` | 0 or 1 | 1 | Default state of CGB color correction. When on, colors match the real CGB LCD instead of the oversaturated raw values. Always toggleable live with HOME+B. |

```bash
cmake -B build -DGBC_ROM=game.gb -DDMG_PALETTE=1 -DGBC_COLOR_CORRECTION=0 ...
```

The palettes live in `dmg_palettes` in `src/main.c`, and the correction matrix is
`cgb_corrected_565` in the same file.

## Controls

Five front buttons cover eight Game Boy inputs. C is a shift modifier and HOME is
a function modifier.

| Button | Default | With C held |
|--------|---------|-------------|
| UP     | Up      | Left        |
| DOWN   | Down    | Right       |
| A      | A       | Start       |
| B      | B       | Select      |

While HOME is held the front buttons control the emulator instead of the game,
and the effect is latched (it persists after release):

| Combo    | Effect |
|----------|--------|
| HOME + A | cycle speed: normal, 2x, maximum |
| HOME + B | toggle GBC color correction |
| HOME + UP | take a save-state snapshot (selected slot) |
| HOME + DOWN | restore the save-state snapshot (selected slot) |
| HOME + C | open the in-game menu |

The in-game menu pauses the game. It collects, in order: speed, GBC color
correction, DMG palette, Display (the shader pass: Off, Scanlines, Dot-matrix,
Retro LCD, Vignette), Brightness (9 levels, 20 to 100 percent in 10 percent
steps), save-state slot selection, save and load state, reset, and Exit to menu
(which flushes the battery save and returns to the launcher). Inside it, UP and
DOWN move, A activates or steps a value forward, C steps back, B closes.

> [!NOTE]
> Brightness is the hardware PWM backlight (a single 16-bit PWM channel on
> GPIO 26), driven with a linear duty so every step is a real, visible level.
> There is no software framebuffer dimming. See [docs/HARDWARE.md](HARDWARE.md).

The in-game mapping is in `read_joypad()` and the HOME function handling is in the
main loop, both in `src/main.c`. The button GPIOs are fixed by hardware and live
in `config.h`.
