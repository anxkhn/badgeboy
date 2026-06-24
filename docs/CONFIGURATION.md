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

To convert a ROM by hand, for inspection:

```bash
python3 tools/rom2c.py game.gbc /tmp/gb_rom.h
```

The generated header and any built image contain copyrighted data. They are
ignored by Git and must not be committed or distributed.

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
| `DMG_PALETTE` | 0..3 | 0 | Palette for monochrome (DMG) games: 0 authentic green, 1 Game Boy Pocket grey, 2 high-contrast mono, 3 dusk amber. No effect on Game Boy Color titles. |
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

The in-game mapping is in `read_joypad()` and the HOME function handling is in the
main loop, both in `src/main.c`. The button GPIOs are fixed by hardware and live
in `config.h`.
