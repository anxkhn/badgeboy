# font8x8

An 8x8 monochrome bitmap font used to render the BadgeBoy in-game menu.

- `font8x8_basic.h`: basic Latin glyphs (U+0000 to U+007F).
- Author: Daniel Hepper, based on public domain VGA fonts by Marcel Sondaar and
  IBM.
- License: Public Domain.
- Upstream: https://github.com/dhepper/font8x8

The file is vendored unmodified. Each glyph is eight bytes, one per row, with the
least significant bit as the leftmost pixel.
