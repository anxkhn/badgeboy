# Tamzen 5x9

The Tamzen bitmap font, used to render the BadgeBoy in-game mod menu.

- `tamzen5x9.h`: the 5x9 regular face, converted from `Tamzen5x9r.bdf` to a C
  array. Each glyph is nine rows, one byte per row, with bit 7 as the leftmost
  column; the array is indexed from 0x20 (space).
- Author: Suraj N. Kurapati, derived from Tamsyn by Scott Fial.
- License: free (see `LICENSE`): permission to use, copy, modify, and distribute.
- Upstream: https://github.com/sunaku/tamzen-font

The header is generated from the upstream BDF; the original `.bdf` is not
vendored. The font data itself is unmodified.
