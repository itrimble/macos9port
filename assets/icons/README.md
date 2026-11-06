# OS Pi Icons

82 original pixel-art icons and 7 cursors for the whole desktop: volumes,
Trash, folders, documents, applications, hardware, alerts, badges, and the
Pi logo. Every icon is drawn by hand-written code in `../tools/icons/`
using only colors from the 8-bit system palette, so it converts losslessly
to classic icon data. No Apple artwork, fonts, or resources are used.

## Files

- `ospi-icons.icl8`: the whole set in one binary file (format below).
- `png/<size>/<name>.png`: 16, 32, 64, 128, 256, 512, and 1024 px. Sizes
  above 32 are exact nearest-neighbor scales, so they stay crisp on Retina
  and 8K displays. 16 px uses the dedicated small icon.
- `hd/<size>/<name>.png`: true high-resolution versions at 64, 128, 256,
  512, and 1024 px for HiDPI (Retina) screens. They are redrawn from the
  same shapes at high resolution, so curves and outlines are smooth rather
  than enlarged pixels. Use `png/` for the authentic pixel look and `hd/`
  for crisp HiDPI rendering.
- `ospi-icons@2x.rgba` and `ospi-icons@4x.rgba`: the whole HD set as 64 px
  and 128 px RGBA, for drawing 32-point desktop icons on 2x and 4x
  framebuffers (format below).
- `app/OSPi.icns`: the app icon, smooth from 64 px to 1024 px (the 32 px
  slot uses the pixel art).
- `cursors/<name>.curs`: 68-byte cursor data (16x16 data, 16x16 mask,
  hotspot v,h as big-endian int16), plus PNG previews at 16, 32, and 64 px.
  `cursors/hd/<name>@2x.png` and `@4x.png` are smooth 32 px and 64 px
  versions for HiDPI screens; hotspots scale by the same factor.
- `manifest.json`: the category of each icon and the hotspot of each cursor.
- `contact-sheet.png`: every icon at 32 px with its 16 px version.
- `contact-sheet-hd.png`: every HD icon at 128 px.
- `c/`: small C11 loaders for the 1x set (`ospi_icon_set.*`) and the HD
  sets (`ospi_icon_hd.*`), each with a test.

## Binary format (OSPIICN1)

```
"OSPIICN1"            8 bytes
count                 uint32, big-endian
count records of 1464 bytes:
  name                24 bytes, NUL padded
  icl8                1024 bytes, 32x32 palette indices, row-major
  mask32              128 bytes, 32x32 1-bit, MSB first
  ics8                256 bytes, 16x16 palette indices
  mask16              32 bytes, 16x16 1-bit, MSB first
```

Indices follow `mac_icon_color()` in `src/mac_icon_palette.c`: 0 to 215 is
the color cube and 215 is black. Outlines use 215.

## HD binary format (OSPIHD01)

```
"OSPIHD01"            8 bytes
count                 uint32, big-endian
size                  uint32, big-endian (64 for @2x, 128 for @4x)
count records:
  name                24 bytes, NUL padded
  rgba                size*size*4 bytes, straight alpha, row-major
```

## Using them in OS Pi

The 32x32 data is the same layout as `Item.guest_icon[1024]` and
`Item.guest_icon_mask[128]`, so `guest_icon()` in `src/paint.c` can draw
these without changes:

```c
#include "ospi_icon_set.h"

static OspiIconSet icons;
ospi_icon_set_load(&icons, "assets/icons/ospi-icons.icl8");
item->has_guest_icon = ospi_icon_copy(&icons, "trash_full",
                                      item->guest_icon, item->guest_icon_mask);
```

The asset branch does not edit `src/`, `include/`, the `Makefile`, or
`Info.plist`. To use the app icon, copy `app/OSPi.icns` into the bundle's
`Contents/Resources/` and add `CFBundleIconFile` = `OSPi` to `Info.plist`.

Suggested mapping from item kinds: `ITEM_FOLDER` to `folder`,
`ITEM_SYSTEM` to `system_folder`, `ITEM_APPLICATION` to `application`,
and `ITEM_FILE` to `document`. Use the type-specific icons when the file
type is known.

### On a HiDPI framebuffer

When the window's drawable size is twice its point size (for example from
`SDL_GetRendererOutputSize`), load the @2x set and alpha-blend each 64 px
icon into the 32-point icon rectangle at 2x pixel scale:

```c
#include "ospi_icon_hd.h"

static OspiHdIconSet hd;
ospi_hd_icon_set_load(&hd, "assets/icons/ospi-icons@2x.rgba");
const uint8_t *px = ospi_hd_icon_find(&hd, "folder");
/* for each x, y in 0..hd.size-1:
   blend ospi_hd_icon_argb(px, hd.size, x, y) at (2 * icon_x + x, 2 * icon_y + y) */
```

## Test the loaders

```bash
cd assets/icons/c
cc -std=c11 -Wall -Wextra -pedantic ospi_icon_set.c test_icon_set.c -o /tmp/t
/tmp/t ../ospi-icons.icl8
cc -std=c11 -Wall -Wextra -pedantic ospi_icon_hd.c test_icon_hd.c -o /tmp/thd
/tmp/thd ../ospi-icons@2x.rgba
```

## Regenerate

```bash
cd assets/tools/icons
python3 build.py ../../icons       # pixel set, about 5 seconds
python3 build_hd.py ../../icons    # HiDPI set, about 1 to 2 minutes
```

The 16 px icons are made automatically by halving the 32 px art. Hand-edit
any that need extra polish.
