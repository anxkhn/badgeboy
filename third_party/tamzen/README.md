# Tamzen 7x14

The Tamzen bitmap font, used to render the BadgeBoy in-game mod menu.

- `tamzen7x14.h`: the 7x14 regular face, converted from `Tamzen7x14r.bdf` to a C
  array. Each glyph is 14 rows, one byte per row, with bit 7 as the leftmost
  column; the array is indexed from 0x20 (space).
- Author: Suraj N. Kurapati, derived from Tamsyn by Scott Fial.
- License: free (see `LICENSE`): permission to use, copy, modify, and distribute.
- Upstream: https://github.com/sunaku/tamzen-font

The header is generated from the upstream BDF; the original `.bdf` is not
vendored. The font data itself is unmodified.
