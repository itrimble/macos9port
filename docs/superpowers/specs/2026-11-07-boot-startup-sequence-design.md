# OS999 boot startup sequence — design

Date: 2026-11-07
Branch: `feature/boot-startup-sequence`
Scope: the boot sequence renderer (`src/os999_startup.c`) and its asset loader
component only. The desktop, menus, windows, and application launching are
untouched.

## Problem

The current intro does not match the supplied OS999 boot mockup:

- The centre graphic is a software-rendered 3D Granny Smith mesh. It was
  accepted as "much better" than earlier revisions, but it is not the
  pixel-art fruit mark in the reference, and it carries a soft elliptical
  shadow halo under the fruit (`emblem_shadow`).
- The wordmark uses the smooth FreeType Chicago face, not the retro pixel
  style the reference shows.
- There is no progress indicator and no boot status text, so the screen gives
  no readable indication of what the intro is doing.
- The reference mockup frames the status row with a striped progress bar.
  The surrounding mockup icons (hard-disk badge, document, `OS9 ->` smiley)
  are not part of the product and must not appear.

The status string is a hard requirement: it reads exactly `Starting Up...`
(capital `S`, capital `U`, three trailing periods).

## Target

A late-90s Mac OS boot display:

1. Original pixel-art Granny Smith fruit, centred, drawn from a small indexed
   palette taken from the project's existing 256-colour icon cube.
2. `OS999` wordmark in the retro 8x8 pixel font, directly beneath the fruit.
3. `Made for the fun of it.` tagline beneath the wordmark (checklist S05 keeps
   this string readable; it stays secondary).
4. A beveled, segmented/striped progress bar that tracks the real
   initialization sequence.
5. `Starting Up...` in the pixel font directly below the progress bar.
6. A deep blue-grey gradient background with a subtle scanline texture, no
   halo, no background icons, and no desktop content bleeding through.
7. A seamless cross-fade into the live desktop when initialization completes.

## Design

### Fruit sprite (`src/os999_startup_sprite.c`, `include/os999_startup_sprite.h`)

- The fruit is authored as readable ASCII art in the source: a 30x22 body and
  a 14x12 stem/leaf piece, each row one character per pixel mapped to a
  palette index. This keeps the artwork reviewable and editable in a diff.
- Palette is limited to nine entries and every entry is a colour already
  reachable in the project's 256-colour cube (`src/mac_icon_palette.c`):
  outline black, three body greens, a pale highlight, two stem browns, and
  two leaf greens. There are no gradients inside the sprite.
- Drawing is pseudo-3D, the standard retro trick for a spinning sprite: the
  body is squashed horizontally by `|cos(angle)|` and mirrored when the fruit
  turns past edge-on, so the baked highlight swaps sides at the narrowest
  point instead of popping. The stem and leaf keep a minimum width so they
  stay attached to the body at every angle, which addresses the detached leaf
  defect in the previous 3D revision.
- Integer scaling only, so pixels stay square and crisp at any window size.

### Typography

- Wordmark, tagline, and status text all use the project's public-domain 8x8
  bitmap font (`src/font.c`, `font_next`) at integer scales (5x, 2x, 2x).
- The boot screen no longer depends on the FreeType typeface being loaded.
  The wordmark is chunky and pixelated in every run, matching the reference.
- The wordmark is drawn with a one-pixel offset dark extrusion, the way the
  reference mockup renders its gold lettering.

### Progress bar

- 420x20 at a 1024-wide window, centred, scaled down on smaller windows.
- Classic construction: 1px dark outer border, 1px light top/left bevel,
  1px dark bottom/right bevel, inset track.
- The whole track carries one continuous zebra of diagonal bands, 4 pixels on
  and 4 off, repeating every 8 pixels. The bands advance to the right by one
  pixel every 25 ms, about 40 pixels a second, so the bar reads as animated.
- The filled region is the same zebra in bright colours, clipped at the
  progress edge; a bright cap marks the head of the fill.
- The unfilled track carries the same zebra in dim colours, so the bar is
  alive from the first frame and the fill is seen crossing it.
- 2px separator gaps split the track into 24 segments and are drawn over the
  zebra, so the bar reads as discrete steps rather than a smooth smear.

### Truthful progress

`os999_startup_frame` already receives a `ready` flag that the caller sets
only after the session is created and the first desktop frame is painted.

- While work is pending, progress rises with elapsed time but is capped at
  0.92, so the bar never claims completion that has not happened.
- The bar reaches 1.0 only when `ready` is true.
- The error phase freezes the bar at its last value and shows the existing
  retry/close dialog.
- Reduced motion keeps the same layout but freezes the stripes and uses the
  existing short timings.

### Layout

Vertical block, centred horizontally and placed slightly above the vertical
centre of the window:

```
        +--------------------+
        |                    |   fruit (pixel art, pseudo-3D spin)
        |                    |
        +--------------------+
             OS999           |   wordmark, pixel font 5x
   Made for the fun of it.   |   tagline, pixel font 2x
   [==================]      |   progress bar, 24 striped segments
        Starting Up...       |   status, pixel font 2x
```

### Background

- Vertical gradient, dark blue-grey at the top to a lighter blue-grey at the
  bottom, matching the reference CRT panel.
- Every second row is darkened slightly for a subtle scanline texture.
- The background is fully opaque, so desktop icons and windows can never
  appear behind the intro.

## Verification

`tests/test_os999_startup.c` extends the existing `CHECK` harness with:

- the status string exposed through `os999_startup_status_text()` compared
  byte-for-byte against `Starting Up...`, so a typo cannot ship;
- progress is monotonic, stays below completion while the desktop is not
  ready, and reaches completion when it is;
- the fruit paints green body pixels where expected;
- the region below the fruit contains no shadow halo pixels;
- wordmark and status glyph pixels are present in the rendered frame;
- progress-bar segments are present and change as progress advances;
- the zebra bands advance to the right at a visible rate, the empty track is
  striped too, the lit edge moves right as the boot runs, and reduced motion
  freezes the bands;
- the boot frame does not contain desktop pixels;
- the reveal still ends on the desktop frame.

A dump mode in the same test binary (`OS999_STARTUP_DUMP=<path>`) writes
PPM frames of each phase for visual inspection.

## Out of scope

- The desktop, wallpaper, menus, windows, and icon placement.
- The FreeType typeface, the desktop's own artwork, and the menu-bar fruit.
- Any change to the startup on/off preference or the preview/replay trigger.
