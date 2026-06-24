# Contributing

Contributions are welcome: bug reports, hardware findings, documentation, and
code. This is firmware for a specific device, so most changes need to be tested
on the badge itself.

## Ways to help

- Report a bug or a game that misbehaves. Include the game title, the cartridge
  type (run `tools/rom2c.py` to print it), the display mode, and what you saw.
- Improve documentation. The hardware notes and bring-up table are meant to save
  the next person time, so corrections and additions are valuable.
- Pick up a roadmap item. See the roadmap in [AGENTS.md](AGENTS.md). Good first
  items are battery-backed saves and a multi-game launcher.

## Workflow

1. Fork the repository on GitHub and clone your fork.
2. Create a branch:
   ```bash
   git checkout -b feature/short-description
   ```
3. Make your change. Keep commits focused and write clear messages. The first
   line should be a concise summary in the imperative mood, for example
   "Add aspect-correct display mode".
4. Build and test on hardware:
   ```bash
   ./build.sh /abs/path/to/known_good_game.gbc fullscreen
   ```
   Flash the result and confirm the image is upright, correctly scaled, and that
   input responds. Note in your pull request which ROM and display mode you used.
5. Push your branch and open a pull request against `main`. Describe what you
   changed, why, and how you tested it. Screenshots or a short video of the badge
   help a lot for display changes.

## Coding guidelines

- Plain C11 against the Pico SDK. No RTOS and no dynamic allocation in the run
  loop.
- Keep verified hardware values in `src/config.h`, not scattered through the code.
- Match the existing style: short comments that explain why, not what.
- Record new hardware findings in [docs/HARDWARE.md](docs/HARDWARE.md) and, if
  relevant to future work, in [AGENTS.md](AGENTS.md).

## Do not commit

- ROM files (`.gb`, `.gbc`), generated ROM headers (`gb_rom.h`), or built images
  (`.uf2`, `.elf`, `.bin`). These contain copyrighted data and are excluded by
  `.gitignore`. Pull requests that add them will be asked to remove them.

## Licensing of contributions

By submitting a contribution you agree that it is licensed under the GNU General
Public License v3.0 or later, the same license as the project. See
[LICENSE](LICENSE). The vendored Peanut-GB core remains under its own MIT license.

## Reporting security or safety issues

Flashing replaces the stock firmware. If you find a change that can leave a badge
in an unrecoverable state, open an issue and describe the conditions clearly so
others can avoid it.
