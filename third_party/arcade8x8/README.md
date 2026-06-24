# arcade8x8

An 8x8 monochrome bitmap font in a classic arcade style, used to render the
BadgeBoy in-game mod menu.

- `arcade8x8.h`: printable ASCII glyphs (0x20 to 0x7F).
- Author: Takayuki Matsuoka.
- License: CC0-1.0 (public domain dedication).
- Upstream: https://gist.github.com/t-mat/80af1caf3329f93ef993ebaa079e69d1

Vendored unmodified apart from an include guard and an array rename. Each glyph
is eight bytes, one per row, with the most significant bit as the leftmost pixel,
and the array is indexed from 0x20 (space).
