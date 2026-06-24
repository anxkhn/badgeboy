# BadgeBoy

A Game Boy and Game Boy Color emulator for the GitHub Universe 2025 badge
(a custom Pimoroni Tufty 2350, RP2350B). It runs the
[Peanut-GB](https://github.com/deltabeard/Peanut-GB) core as bare-metal RP2350
firmware, drives the badge's 8-bit parallel ST7789 display through PIO and DMA,
and reads the five front buttons for input.

Version 0.7.0. Runs on hardware: a Game Boy Color title boots, renders full
screen, and is playable, with color correction, fast-forward, battery and
save-state saves, display shaders, brightness control, and a native-resolution
in-game mod menu. An on-device launcher picks between a built-in game and games
from a separately flashed ROM library, each with its own saves.

![status](https://img.shields.io/badge/status-v0.7.0-blue) ![platform](https://img.shields.io/badge/platform-RP2350B-informational) ![license](https://img.shields.io/badge/license-GPLv3-green)

## What it does

- Emulates original Game Boy (DMG) and Game Boy Color (CGB) titles.
- On-device ROM browser: a built-in game plus games from a separately flashed
  ROM library (the ROM pack), with soft-reset switching from the launcher.
- Renders to the badge's 320x240 ST7789 panel with three selectable scaling
  modes: full screen, correct aspect ratio, or native centered.
- Game Boy Color color correction that matches the real CGB LCD, toggleable live.
- Latched fast-forward (2x and maximum).
- Per-game battery saves persisted to flash and reloaded on boot, plus per-game
  save-state slots, so each game keeps its own progress.
- Save-state snapshots with multiple slots: capture and restore the exact moment.
- Display shaders: scanlines, dot-matrix grid, retro LCD, and vignette.
- Brightness control over the hardware PWM backlight (9 levels).
- A native-resolution in-game mod menu (HOME+C) for all of the above.
- Four selectable palettes for monochrome Game Boy games.
- Embeds one ROM of your choice into flash at build time as the launcher's first
  game.
- Maps the five front buttons to the eight Game Boy inputs using a shift modifier.

> [!WARNING]
> This is a custom firmware image. Flashing it replaces the stock MonaOS
> launcher. MonaOS can be restored at any time, see
> [docs/FLASHING.md](docs/FLASHING.md).

## What it does not do

- No audio. The badge has no speaker, so sound emulation is compiled out.
- No Game Boy Advance. GBA is well beyond the RP2350 and is out of scope.

## Requirements

You need an ARM bare-metal toolchain and the usual build tools. The Pico SDK is
fetched automatically on first build.

On Debian or Ubuntu:

```bash
sudo apt update
sudo apt install -y gcc-arm-none-eabi cmake ninja-build build-essential python3
```

| Tool                 | Purpose                                  |
|----------------------|------------------------------------------|
| `gcc-arm-none-eabi`  | cross compiler for the RP2350 (Cortex-M33) |
| `cmake` (3.13+)      | build configuration                      |
| `ninja-build`        | build backend                            |
| `build-essential`    | host compiler and make, used by the SDK  |
| `python3`            | ROM to C header conversion               |
| `git`                | fetches the Pico SDK on first build      |

To read the badge pin map yourself you also need `mpremote`
(`pip install mpremote`). See [docs/HARDWARE.md](docs/HARDWARE.md).

## Quick start

Build with your own legally obtained ROM:

```bash
git clone https://github.com/anxkhn/badgeboy.git
cd badgeboy
./build.sh /absolute/path/to/your_game.gbc fullscreen
```

The first build clones the Pico SDK into `~/pico-sdk`. The result is
`build/badgeboy.uf2`.

Flash it:

1. On the badge, hold BOOTSEL, tap RESET, release BOOTSEL. The badge mounts as
   a drive named `RP2350`.
2. Copy the image across:
   ```bash
   cp build/badgeboy.uf2 /run/media/$USER/RP2350/
   ```

The badge reboots into the emulator. Full instructions, including how to put
MonaOS back, are in [docs/FLASHING.md](docs/FLASHING.md).

## Multiple games

The firmware is one flash image; the game library is a separate flash image (the
ROM pack). The built-in ROM is the launcher's first game; pack games are added
after it (up to 8 total, each with its own saves). Build a pack from your own
legally obtained ROMs and flash it on its own, without rebuilding the firmware:

```bash
python3 tools/pack_roms.py -o games.uf2 game1.gb "game 2.gbc" ...
cp games.uf2 /run/media/$USER/RP2350/      # BOOTSEL, exactly like the firmware
```

The pack writes only its own flash region, so it leaves the firmware and your
saves intact. On boot, a single game launches straight in; with more than one,
the launcher lists each game with a CGB or DMG tag and switches with a soft
reset. See [docs/FLASHING.md](docs/FLASHING.md) and [docs/SAVES.md](docs/SAVES.md).

## Controls

The badge has five front buttons but the Game Boy needs eight inputs (Up, Down,
Left, Right, A, B, Start, Select). BadgeBoy uses C as a shift modifier: while C
is held, UP, DOWN, A, and B are remapped to the four inputs that have no
dedicated button. This gives access to all eight inputs from five buttons.

| Button   | Default | With C held |
|----------|---------|-------------|
| UP       | Up      | Left        |
| DOWN     | Down    | Right       |
| A        | A       | Start       |
| B        | B       | Select      |
| C        | shift modifier | shift modifier |
| HOME     | function modifier | function modifier |

HOME is a function modifier, like a Fn key. While HOME is held the front buttons
control the emulator instead of the game, and the effect is latched, so it stays
set after you let go:

- **HOME + A**: cycle speed, normal then 2x then maximum then back to normal.
- **HOME + B**: toggle Game Boy Color color correction, to compare the corrected
  and raw palettes live.
- **HOME + UP**: take a save-state snapshot (instant, anywhere in the game).
- **HOME + DOWN**: restore the last snapshot.
- **HOME + C**: open the in-game menu.

> [!TIP]
> The in-game menu collects everything in one place: speed, color correction, DMG
> palette, Display (the shader pass: Off, Scanlines, Dot-matrix, Retro LCD,
> Vignette), Brightness (9 levels, 20 to 100 percent), the save-state slot, save
> and load state, reset, and Exit to menu (back to the launcher). While it is open
> the game pauses; UP and DOWN move, A activates or steps a value forward, C steps
> it back, and B closes. Brightness drives the hardware PWM backlight.

There are two independent kinds of save, and each game keeps its own (see
[docs/SAVES.md](docs/SAVES.md)):

- **Battery save** is the game's own save (its in-game Save menu). BadgeBoy writes
  it to flash automatically a moment after you save, and reloads it on the next
  boot, so it survives power off. You restore it the normal way, through the
  game's Continue or Load menu. No button is involved.
- **Save state** is a full snapshot of the exact moment, taken with HOME+UP and
  restored with HOME+DOWN, independent of the game's own save. A magenta marker
  shows a snapshot being written; green or red marks a restore as found or not.

## Customization

Selected at build time.

- ROM: `-DGBC_ROM=/abs/path/game.gbc`, or pass the path as the first argument
  to `build.sh`.
- Display mode: `-DGBC_DISPLAY_MODE=FULLSCREEN|ASPECT|CENTERED`, or pass it as
  the second argument to `build.sh`.
- DMG palette for monochrome games: `-DDMG_PALETTE=0..3`
  (0 green, 1 grey, 2 high-contrast, 3 amber). The default is 2 (high-contrast).
- GBC color correction default: `-DGBC_COLOR_CORRECTION=0|1`. It is on by default
  and can also be toggled live with C+HOME.

```bash
./build.sh ~/roms/game.gbc aspect      # correct aspect ratio, thin side borders
./build.sh ~/roms/game.gbc centered    # native 160x144 in the centre
```

See [docs/CONFIGURATION.md](docs/CONFIGURATION.md) for details.

## Documentation

| Document | Contents |
|----------|----------|
| [docs/HARDWARE.md](docs/HARDWARE.md)         | Verified badge pin map, display bus, RP2350B notes |
| [docs/BUILDING.md](docs/BUILDING.md)         | Toolchain, SDK, build options |
| [docs/FLASHING.md](docs/FLASHING.md)         | Flashing the firmware, a ROM pack, and restoring MonaOS |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | ROM selection, ROM packs, display modes, controls, debug build |
| [docs/SAVES.md](docs/SAVES.md)               | Battery saves, save states, per-game areas, troubleshooting |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the firmware is put together |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | Emulation core choice and known limits |
| [ROADMAP.md](ROADMAP.md)                     | Planned features, tiered by effort, with status |
| [AGENTS.md](AGENTS.md)                        | Working context for contributors and coding agents |

## Contributing

Contributions are welcome: bug reports, hardware findings, documentation, and
code. Because this is firmware for a specific device, most changes need testing
on the badge. See [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and
[AGENTS.md](AGENTS.md) for working context and the roadmap.

## Legal

> [!IMPORTANT]
> BadgeBoy ships no game ROMs. Supply only software you legally own. Generated
> ROM headers and built images contain copyrighted data and are excluded from
> version control by `.gitignore`. Do not commit or distribute them.

## Acknowledgements

- [Peanut-GB](https://github.com/deltabeard/Peanut-GB) by Mahyar Koshkouei, and
  the `cgb` branch contributors, for the emulator core (MIT).
- Pimoroni for the Tufty 2350 hardware and the ST7789 parallel driver design
  that the display layer is modelled on.
- The Raspberry Pi Pico SDK team.

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE). The vendored
Peanut-GB core remains under its own MIT license; see
[third_party/peanut-gb/LICENSE](third_party/peanut-gb/LICENSE).
