# OS999 Boot Startup Sequence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 3D-mesh boot intro with a pixel-art Granny Smith fruit, a pixel-font `OS999` wordmark, a segmented/striped progress bar, and the exact status string `Starting Up...`, transitioning seamlessly into the desktop.

**Architecture:** A new focused sprite module (`src/os999_startup_sprite.c`) owns the indexed fruit artwork and its pseudo-3D draw routine. `src/os999_startup.c` keeps the existing phase state machine and owns layout, background, progress bar, and typography. The public header gains one accessor, `os999_startup_status_text()`, so the exact status string is assertable by the test harness.

**Tech Stack:** C11, SDL2 host, existing `Fb` framebuffer helpers (`fb_fill`, `fb_frame`), existing public-domain 8x8 bitmap font (`src/font.c`, `font_next`), existing `CHECK` test harness.

## Global Constraints

- The status string below the progress bar reads exactly `Starting Up...` — capital `S`, capital `U`, three trailing periods, no other wording.
- Progress never reports completion before the desktop is genuinely ready.
- No halo, no background icons, and no desktop content may appear on the boot display.
- Integer-only pixel scaling; no gradients inside the sprite.
- Artwork is original and authored in-repo; the supplied mockups are layout references only and are not vendored.
- C style: four-space indent, same-line opening braces, `snake_case` functions, `PascalCase` typedefs, uppercase constants, every file below 500 lines.
- Makefile builds with `-Wall -Wextra`; `make test` must pass with zero errors.
- Modify only the startup module and its test/build wiring.

---

### Task 1: Pixel-art fruit sprite module

**Files:**
- Create: `include/os999_startup_sprite.h`
- Create: `src/os999_startup_sprite.c`
- Modify: `Makefile` (add the new source to every rule that compiles `src/os999_startup.c`: `build/ospi`, `build/test_os999_startup`, `build/test_ospi`, `build/test_movie_window`, `build/test_archive_install`)
- Test: `tests/test_os999_startup.c`

**Interfaces:**
- Consumes: `Fb` framebuffer helpers from `include/draw.h`.
- Produces:
  - `void os999_startup_draw_fruit(Fb *fb, int cx, int cy, int scale, double angle);`
  - `int os999_startup_fruit_width(int scale);`
  - `int os999_startup_fruit_height(int scale);`

- [x] **Step 1: Create the sprite header**

```c
#ifndef OS999_STARTUP_SPRITE_H
#define OS999_STARTUP_SPRITE_H

#include "draw.h"

/* Original pixel-art Granny Smith fruit. `angle` is in radians; the fruit is
   squashed horizontally to fake a slow turn, and the stem stays attached. */
void os999_startup_draw_fruit(Fb *fb, int cx, int cy, int scale, double angle);
int os999_startup_fruit_width(int scale);
int os999_startup_fruit_height(int scale);

#endif
```

- [x] **Step 2: Author the sprite source**

Indexed ASCII artwork with this palette (all colours exist in the project 256-colour cube, see `src/mac_icon_palette.c`):

```c
enum { APPLE_OUTLINE = 0, APPLE_DARK, APPLE_MID, APPLE_LIGHT,
       APPLE_SHADE, APPLE_HILITE, APPLE_STEM, APPLE_STEM_DARK,
       APPLE_LEAF, APPLE_LEAF_DARK, APPLE_NONE };

static const uint32_t apple_palette[] = {
    0xFF000000u, 0xFF1E3A0Cu, 0xFF3F7A16u, 0xFF66A81Fu,
    0xFF8FCB36u, 0xFFD7F29Fu, 0xFF6B4A22u, 0xFF3F2A10u,
    0xFF4C9A22u, 0xFF2A5C12u, 0x00000000u
};
```

Body is 30x22, stem/leaf is 14x12; the body carries a baked highlight on the
upper left and a dark rim on the lower right.

- [x] **Step 3: Implement the pseudo-3D draw routine**

The silhouette is the authored pixel art and never changes. The fruit turns
by moving its shading: a dark rim travels around the far side of the body
and a small pale gloss spot travels across the lit side, the way a classic
sprite sheet fakes a spinning object. The stem and leaf keep their authored
shape and swap their lit side as the fruit turns.

```c
static int body_index(int x, int y, double angle);   /* palette index */
void os999_startup_draw_fruit(Fb *fb, int cx, int cy, int scale, double angle);
```

Pixels are placed at integer scale only, so the artwork stays crisp.

- [x] **Step 4: Add the source to the Makefile rules**

Add `src/os999_startup_sprite.c` next to every `src/os999_startup.c` in the
Makefile and `config/*.mk`. Both files reference the startup module in many
test rules, so the pair is expressed once:

```make
STARTUP_SRC := src/os999_startup.c src/os999_startup_sprite.c
STARTUP_HDR := include/os999_startup.h include/os999_startup_sprite.h
```

Every rule then links `$(STARTUP_SRC)`.

- [x] **Step 5: Build and check warnings**

Run: `make build/test_os999_startup`
Expected: compiles clean under `-Wall -Wextra`.

- [x] **Step 6: Commit**

```bash
git add include/os999_startup_sprite.h src/os999_startup_sprite.c Makefile
git commit -m "Add original pixel-art Granny Smith fruit sprite for the boot intro"
```

---

### Task 2: Background, typography, and exact status text

**Files:**
- Modify: `include/os999_startup.h`
- Modify: `src/os999_startup.c`
- Test: `tests/test_os999_startup.c`

**Interfaces:**
- Produces: `const char *os999_startup_status_text(void);` returning `"Starting Up..."`.

- [x] **Step 1: Write the failing test**

```c
CHECK(strcmp(os999_startup_status_text(), "Starting Up...") == 0);
```

- [x] **Step 2: Run it and see it fail**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: compile error, `os999_startup_status_text` undeclared.

- [x] **Step 3: Implement the background and typography**

- Replace the gradient fill with the mockup palette: `0xFF2E3A55u` at the top
  to `0xFF586C93u` at the bottom.
- Darken every second row slightly for a subtle scanline texture.
- Draw `OS999` with `bitmap_draw_scaled` at scale 5, directly beneath the
  fruit, with a one-pixel offset dark extrusion drawn first.
- Draw `Made for the fun of it.` with `bitmap_draw_scaled` at scale 2.
- Draw the status with `bitmap_draw_scaled` at scale 2 directly below the
  progress bar.
- Delete the FreeType wordmark path (`typeface_startup_ready`,
  `typeface_width`, `typeface_draw`) from the intro so the boot screen never
  depends on the desktop typeface.

- [x] **Step 4: Implement the accessor**

```c
const char *os999_startup_status_text(void) { return "Starting Up..."; }
```

- [x] **Step 5: Run the test**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: PASS, `0 failures`.

- [x] **Step 6: Commit**

```bash
git add include/os999_startup.h src/os999_startup.c tests/test_os999_startup.c
git commit -m "Render the OS999 wordmark and exact Starting Up... status in the pixel font"
```

---

### Task 3: Segmented striped progress bar with truthful progress

**Files:**
- Modify: `src/os999_startup.c`
- Test: `tests/test_os999_startup.c`

**Interfaces:**
- Produces: `double os999_startup_progress(const Os999Startup *startup, uint32_t now);`

- [x] **Step 1: Write the failing test**

```c
os999_startup_begin(&s, 0, 0);
CHECK(os999_startup_progress(&s, 0) < 0.01);
CHECK(os999_startup_progress(&s, 2000) > 0.4);
CHECK(os999_startup_progress(&s, 100000) <= 0.92);   /* pending work never completes */
s.ready = 1;
CHECK(os999_startup_progress(&s, 100000) == 1.0);    /* completion is truthful */
```

- [x] **Step 2: Run it and see it fail**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: compile error, `os999_startup_progress` undeclared.

- [x] **Step 3: Implement the progress function**

```c
double os999_startup_progress(const Os999Startup *s, uint32_t now) {
    if (!s) return 0.0;
    if (s->ready) return 1.0;
    uint32_t elapsed = now - s->phase_started;
    double t = (double)elapsed / (double)SPIN_MS;
    if (t > 1.0) t = 1.0;
    return t * 0.92;   /* capped until the desktop is really ready */
}
```

- [x] **Step 4: Implement the bar renderer**

```c
static void progress_bar(Fb *fb, int x, int y, int w, int h,
                          double progress, uint32_t now, int animate);
```

- 1px dark outer border, 1px light top/left bevel, 1px dark bottom/right bevel.
- 24 segments with 2px gaps; a segment is filled when
  `segment_index + 1 <= progress * 24`.
- Each filled segment is filled with diagonal stripes of period 6px whose
  phase scrolls with `now` (frozen when reduced motion or not animating).
- Unfilled segments show the inset track colour.

- [x] **Step 5: Wire the bar into the layout**

Place the bar centred between the tagline and the status text.

- [x] **Step 6: Run the test**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: PASS.

- [x] **Step 7: Commit**

```bash
git add src/os999_startup.c tests/test_os999_startup.c
git commit -m "Add a segmented striped progress bar that tracks real initialization"
```

**Implementation note (8 Nov):** the bar shipped differently from the steps
above, after the live window showed two defects the unit tests did not catch.
The zebra now runs across the whole track instead of restarting per segment,
its bands advance to the right (the planned phase scrolled left), the empty
track carries a dim zebra so the bar is alive before any progress, and the
fill is clipped at the progress edge instead of stepping segment by segment.
`os999_startup_progress()` completes when the intro leaves the spin phase, not
when the desktop session appears, so the fill crosses the track during the
boot. The bar checks live in their own binary, `tests/test_os999_startup_bar.c`.

---

### Task 4: Drop the 3D mesh and its halo

**Files:**
- Modify: `src/os999_startup.c`

**Interfaces:**
- Consumes: `os999_startup_draw_fruit` from Task 1.

- [x] **Step 1: Remove the old renderer**

Delete `emblem_shadow`, `apple_light`, `apple_vertex`, `apple_triangle`,
`emblem`, the `depth_buffer` scratch allocation, and the `mix`-based fruit
shading that produced the halo. `os999_startup_release()` stays as a no-op
because the renderer no longer owns scratch memory.

- [x] **Step 2: Call the sprite renderer**

```c
os999_startup_draw_fruit(fb, L.fruit_x, L.fruit_y, L.fruit_scale, angle);
```

with `angle` advancing only while work is pending, easing to zero before the
reveal so the fruit lands upright.

- [x] **Step 3: Build and run**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: PASS.

- [x] **Step 4: Commit**

```bash
git add src/os999_startup.c
git commit -m "Replace the 3D boot mesh and shadow halo with the pixel-art fruit"
```

---

### Task 5: Frame-level regression tests

**Files:**
- Modify: `tests/test_os999_startup.c`

**Interfaces:**
- Consumes: `os999_startup_status_text`, `os999_startup_progress`, the frame
  renderer.

- [x] **Step 1: Add pixel assertions**

- Fruit: green body pixels exist inside the fruit rectangle.
- No halo: the row band just below the fruit contains only background
  gradient colours, never the dark outline colour.
- Wordmark: light pixels exist in the wordmark band.
- Status: light pixels exist in the status band, and the status band is below
  the progress bar band.
- Bar: the filled region grows as progress advances.
- No desktop bleed: a boot frame contains no pixel equal to the desktop
  colour.

- [x] **Step 2: Add the frame dump mode**

When `OS999_STARTUP_DUMP` is set to a path prefix, the test binary writes one
PPM per phase (`spin`, `settle`, `hold`, `reveal`) so the frames can be
inspected visually outside the test.

- [x] **Step 3: Run the full startup test**

Run: `make build/test_os999_startup && ./build/test_os999_startup`
Expected: PASS with zero failures.

- [x] **Step 4: Commit**

```bash
git add tests/test_os999_startup.c
git commit -m "Cover the pixel-art boot frame, progress bar, and status string"
```

---

### Task 6: Full build, suite, visual check, and branch commit

**Files:**
- Modify: `docs/DEVELOPMENT-CHECKLIST.md` (record what the boot screen now shows)
- Modify: `docs/superpowers/plans/2026-11-07-boot-startup-sequence.md` (tick the boxes)

**Interfaces:**
- Consumes: everything from Tasks 1-5.

- [x] **Step 1: Build the app**

Run: `make build/ospi`
Expected: compiles clean under `-Wall -Wextra`.

- [x] **Step 2: Run the full suite**

Run: `make test`
Expected: every suite passes, including `./build/test_os999_startup`.

- [x] **Step 3: Inspect the rendered frames**

Run: `OS999_STARTUP_DUMP=/tmp/os999-boot ./build/test_os999_startup`, convert
the PPMs to PNG with `sips`, and read them to confirm the fruit, wordmark,
bar, and status are laid out correctly with no halo and no icons.

- [x] **Step 4: Update the checklist notes**

Record the rendered boot screen, the exact status string, and the validation
commands in `docs/DEVELOPMENT-CHECKLIST.md` under S04/S05/S10.

- [x] **Step 5: Commit**

```bash
git add docs/DEVELOPMENT-CHECKLIST.md docs/superpowers
git commit -m "Record the OS999 boot startup sequence validation"
```
