#!/bin/sh
# Build the showcase stage against the real OS999 desktop sources and render.
#   cd keyboard && sh tools/showcase/build.sh [OUTDIR]
# Needs FreeType, Pillow, numpy and ffmpeg. Wallpapers come from
# assets/wallpapers/4x3 (converted to the BMPs the desktop loads).
set -e
cd "$(dirname "$0")/../.."
ROOT=..
OUT=${1:-build/showcase}
mkdir -p build "$OUT" "$ROOT/build/wallpapers"
# The desktop's CORE list, read from the top-level Makefile so it never drifts.
OS_CORE=$(awk '/^CORE :=/{f=1} f{print} f&&!/\\$/{exit}' "$ROOT/Makefile" | sed 's/^CORE :=//; s/\\//g' |
          tr ' \t' '\n\n' | grep '\.c$' | sed "s|^|$ROOT/|" | tr '\n' ' ')
KBD="src/kbd_core.c src/kbd_keys.c src/kbd_layout.c src/kbd_text.c src/kbd_touch.c src/kbd_pad.c
     src/kbd_motion.c src/kbd_anim.c src/kbd_gfx.c src/kbd_skin.c src/kbd_draw.c src/kbd_draw_over.c
     src/kbd_synth.c src/kbd_mixer.c"
${CC:-cc} -std=c11 -O2 -Iinclude -Isrc -I$ROOT/include -I$ROOT/src -I$ROOT/assets/icons/c -D_DEFAULT_SOURCE \
    -DOSPI_FREETYPE $(pkg-config --cflags freetype2) tools/showcase/stage.c $KBD $OS_CORE $ROOT/src/os999_startup.c \
    $(pkg-config --libs freetype2) -lm -o "$ROOT/build/os999-showcase-stage"
python3 - "$ROOT" <<'P'
import sys, glob, os
from PIL import Image
root = sys.argv[1]
for src in glob.glob(f'{root}/assets/wallpapers/4x3/*'):
    wid = os.path.basename(src)[:2]
    dst = f'{root}/build/wallpapers/{wid}.bmp'
    if not os.path.exists(dst):
        Image.open(src).convert('RGB').resize((1024, 768), Image.LANCZOS).save(dst)
P
python3 tools/showcase/showcase.py "$ROOT/build/os999-showcase-stage" "$ROOT/fonts/ChicagoFLF.ttf" "$OUT"
python3 tools/showcase/showcase.py "$ROOT/build/os999-showcase-stage" "$ROOT/fonts/ChicagoFLF.ttf" "$OUT" stills
