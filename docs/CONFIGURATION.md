# Configuration

Two things are configured at build time: which ROM is embedded, and how the
image is scaled on the panel. Both can be passed to `build.sh` or set as CMake
cache variables.

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

## Controls

Five front buttons cover eight Game Boy inputs. C is a shift modifier.

| Button | Default | With C held |
|--------|---------|-------------|
| UP     | Up      | Left        |
| DOWN   | Down    | Right       |
| A      | A       | Start       |
| B      | B       | Select      |

The mapping is in `read_joypad()` in `src/main.c`. To change it, edit that
function. The button GPIOs themselves are fixed by hardware and live in
`config.h`.
