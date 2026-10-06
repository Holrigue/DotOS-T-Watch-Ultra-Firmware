# DotOS design language

Direction set from the Pinterest inspirations (mock: [`ui-direction.png`](ui-direction.png),
source [`ui-direction.html`](ui-direction.html)). It is a direction, not final art.

## Principles
- **Black base.** The panel is AMOLED, so black pixels are off: cheaper and sharper.
- **One orange accent** per screen. Everything else is black, cream or quiet grey.
- **Cream "inverted" tile** for the focused or selected item.
- **Hatch and dot fills** (diagonal stripes, dot grids) for trends and goals, the same
  shapes as the Dot watch face, so home and apps read as one family.
- **Squircle tiles** with big radii that nest with the rounded panel (410 x 502).
- **Bold grotesque type**; pixel/LCD type only for timers (Seiko reference).

## Tokens (`src/theme.h`)
| Token | Value | Use |
|---|---|---|
| `ARGUS_BG` | `#000000` | base |
| `ARGUS_TILE` | `#161616` | tile |
| `ARGUS_RAISED` | `#202020` | control inside a tile |
| `ARGUS_CREAM` | `#F4F2EC` | focused / inverted tile |
| `ARGUS_ACCENT` | `#FF5A1A` by default | the accent: the wearer's Face > Accent colour (`_ACTIVE` / `_DIM` are tints of it) |
| `ARGUS_QUIET` | `#8A8A86` | secondary text |
| `ARGUS_R_TILE` / `_ROW` / `_PILL` | 46 / 34 / circle | corner radii |

Helpers: `argus_style_tile(obj, ArgusTile::Normal|Focus|Accent)`, `argus_tile_text(kind)`,
`argus_style_pill_slider(slider)`.

## Accent colours
`ARGUS_ACCENT` is an expression, not a constant: it returns the colour picked in Face > Accent
(Orange by default; Pale orange, Red, Atomic purple, Cyan, Green, Grey), so tiles, toggle pills
and icons all follow it. Never cache it in a `static` or a file-scope `const`. Threat red (HADES) and the Offense red keep their meaning.

## Order of work
1. Tokens and helpers (this step).
2. Music player as a cassette.
3. Apps menu as tiles.
4. Data screens (health, battery, detectors) with the tile/chart shapes.
5. Settings rows and pill sliders; timers with the LCD theme.
