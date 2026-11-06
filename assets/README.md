# OS Pi Assets

Fifty-nine wallpapers and sixty-four interface sounds for OS Pi. Each one
follows the look of a different period of Mac desktop history, from 1-bit
patterns to translucent glass. All of them are original: most are made
procedurally, and the AI-made ones are noted below.
None of them contains or traces Apple artwork, audio, or trademarks, which
keeps the repository's clean-room rule. MIT licensed, the same as the repository.

## Wallpapers

`wallpapers/manifest.json` lists the id, title, era, year, and file name for
each wallpaper.

| Folder   | Size        | Notes                                             |
|----------|-------------|---------------------------------------------------|
| `8k/`    | 7680 x 4320 | Master files, rendered natively                   |
| `4k/`    | 3840 x 2160 | Downscaled from the 8K master                     |
| `1080p/` | 1920 x 1080 | Downscaled from the 8K master                     |
| `16x10/` | 2560 x 1600 | Rendered natively for 16:10 Mac displays          |
| `4x3/`   | 1024 x 768  | Rendered natively for the OS Pi window size       |
| `tiles/` | 8 or 16 px  | 1x tiles for the six classic pixel patterns       |

How they scale:

- Wallpapers 01 to 06 are pixel patterns, saved as PNG. Scale a tile with
  nearest-neighbor filtering only, by an integer factor of `round(H / 384)`.
  The patterns then stay sharp at any resolution. The runtime can tile
  them straight from `tiles/` and skip the large files.
- Wallpapers 07 to 30 are JPEG at quality 93 with full 4:4:4 color. Every
  image is a function of resolution-independent coordinates, with the
  composition centered on the screen. Any aspect ratio renders correctly
  without stretching. For a size that isn't listed, render that size
  directly (see below) or cover-fit the nearest larger file.
- Gradients use triangular dither, so they don't band at 8K.

## Big-cat wallpapers (31 to 39)

These nine photographs follow the cat-named releases from 2001 to 2012:
Cheetah, Puma, Jaguar, Panther, Tiger, Leopard, Snow Leopard, Lion, and
Mountain Lion. They are original images, made with AI image generation
(Nano Banana Pro, 2752 x 1536), and they don't reuse any Apple box or
marketing art. `tools/cats.py` applies a light denoise and a 4x
Real-ESRGAN general v3 upscale, then crops each size with a horizontal
anchor so the cat stays in frame at 16:9, 16:10, and 4:3. The source
images can't be regenerated exactly, so the committed JPEGs are the
masters. `wallpapers/cats-contact-sheet.jpg` previews all nine.

## Fruit-era wallpapers (40 to 59)

Twenty wallpapers in the spirit of the late-1990s fruit-colored desktops:
blueberry, tangerine, lime, grape, and strawberry hazes with floating glass
beads, glossy 3D shapes, perspective fields, a pastel daisy collage, and
a few still-life photos. The names and compositions are original. Other
people's wallpapers were looked at only for the general mood; none was
copied, traced, or given to the image model.

- Procedural (40, 41, 44, 46, 49, 50, 54, 56): `tools/fruit.py`, rendered
  natively at every size by `tools/fruit_render.py`, like 07 to 30.
  50 Pi Desktop is a periwinkle default desktop built around the OS Pi mark.
- AI-made (42, 43, 45, 47, 48, 51, 52, 53, 55, 57, 58, 59): Nano Banana Pro
  at 2752 x 1536, finished by `tools/fruit_ai.py` with the same denoise,
  4x upscale, and anchored crops as the big-cat set. The committed JPEGs
  are the masters.

`wallpapers/fruit-contact-sheet.jpg` previews all twenty.

## Sounds

`sounds/manifest.json` lists each sound with its category (system, alert,
ui, or finder) and its length.

- `sounds/*.wav`: 48 kHz, 16-bit PCM, stereo, peaks at or below -1 dBFS,
  with loudness matched across the set.
- `sounds/classic/*.wav`: 22.05 kHz, 8-bit unsigned, mono, for
  period-style playback paths.
- `sounds/audition-reel.mp3`: all 30 sounds in order, so you can listen
  through the set quickly.

## Classic 9 sounds

`sounds/classic9/` holds 34 more sounds in the spirit of late-1990s
desktops: a big startup chord, alert sounds (plain beep, tin chime, eep,
quack, droplet, indigo, temple bowl, uh-oh, voltage, whit, pong, squeak
toy, wah-wah), and a full Platinum-style interface set (menus, window zoom
and shade, drag, buttons, checkboxes, scroll, grow box, spring-loaded
folders, Trash). They use the same formats and loudness as `sounds/` and
include an `audition-reel.mp3`. Each is synthesized from scratch; nothing
is sampled or copied from any original recording.

## Icons

`icons/` holds 82 icons and 7 cursors for the whole desktop, as pixel art
(16 and 32 px) and as smooth HiDPI versions up to 1024 px. See
`icons/README.md` for the format, the C loader, and integration notes.

## Regenerate

The output is deterministic. Running these commands again produces the
same files.

```bash
python3 -m pip install -r assets/tools/requirements.txt
cd assets/tools
python3 render.py ../wallpapers            # all sizes, about 6 minutes on 2 cores
python3 render.py /tmp/prev --preview      # 960x540 previews, a few seconds
python3 fruit_render.py /tmp/fruit         # procedural fruit-era set, about 2 minutes
python3 sfx.py ../sounds
python3 sfx_classic9.py ../sounds/classic9
(cd icons && python3 build.py ../../icons && python3 build_hd.py ../../icons)
```

To render a custom size, call `render.render(entry, W, H)` from Python.
