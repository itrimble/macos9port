"""Icons for the built-in media applications, and icons drawn from supplied bitmaps.

quake3: the Quake III Arena icon, sources/quake3.gif (32x32), taken unchanged from code/unix/quake3.gif in
id Software's Quake III Arena source release, which is licensed GPL-2.0 (the engine tree is kept separately).
The 32-pixel set snaps it to the classic system palette; the HiDPI sets resample the same bitmap smoothly.
"""
import os
import numpy as np
from PIL import Image
from core import PAL, K, W, G1, G3, G5, G7, G8, G9, RED, ORANGE, YELLOW, GREEN, GREEN_DK, BLUE_HI, BLUE_DK, NAVY

REG = {}
_SOURCES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sources')


def icon(name, cat):
    def wrap(fn):
        REG[name] = (cat, fn)
        return fn
    return wrap


def _bitmap(path):
    """RGBA with a transparent background: the GIF's white backdrop becomes alpha."""
    im = Image.open(path).convert('RGB')
    a = np.array(im).astype(np.int32)
    dist = 255 - a.min(axis=2)                      # distance from white, 0 for the backdrop
    alpha = np.clip(dist * 3, 0, 255).astype(np.uint8)
    return np.dstack([a.astype(np.uint8), alpha])


@icon("quake3", "app")
def quake3(ic):
    src = _bitmap(os.path.join(_SOURCES, 'quake3.gif'))
    if ic.im.size == (32, 32):
        # Pixel art: nearest system-palette color, fully on or off.
        out = np.zeros((32, 32, 4), np.uint8)
        rgb = src[..., :3].astype(np.int32)
        for y in range(32):
            for x in range(32):
                if src[y, x, 3] < 96:
                    continue
                best = int(((PAL - rgb[y, x]) ** 2).sum(axis=1).argmin())
                out[y, x] = (*PAL[best], 255)
        ic.im.alpha_composite(Image.fromarray(out, 'RGBA'))
    else:
        big = Image.fromarray(src, 'RGBA').resize(ic.im.size, Image.LANCZOS)
        ic.im.alpha_composite(big)


@icon("movie_player", "app")
def movie_player(ic):
    """A film frame: dark strip with sprocket holes around a screen showing a play triangle."""
    ic.shadow(5, 29, 28, 29)
    ic.box(2, 3, 29, 28, G9, G7, K)
    for x in (4, 9, 14, 19, 24):                 # sprocket holes, top and bottom
        ic.rect(x, 5, x + 2, 7, G1)
        ic.rect(x, 24, x + 2, 26, G1)
    ic.box(5, 9, 26, 22, BLUE_DK, BLUE_HI, NAVY, outline=K)
    ic.pgon([(12, 11), (12, 20), (21, 15)], W, None, None, outline=G3)


@icon("amp_player", "app")
def amp_player(ic):
    """A compact player: metal body, a black display with a green-to-red spectrum, transport buttons."""
    ic.shadow(5, 29, 28, 29)
    ic.box(2, 3, 29, 28, G8, G5, K)
    ic.box(4, 5, 27, 17, K, None, None, outline=G9)
    for x, h in ((6, 6), (9, 10), (12, 8), (15, 11), (18, 5), (21, 9), (24, 7)):   # spectrum bars
        top = 16 - h
        for y in range(top, 16):
            col = RED if y < top + 2 else YELLOW if y < top + 5 else GREEN
            ic.rect(x, y, x + 1, y, col)
    ic.rect(4, 19, 27, 19, G9)
    for x in (5, 10, 15, 20):                    # transport buttons
        ic.box(x, 21, x + 4, 26, G3, G1, G7, outline=K)
    ic.pgon([(7, 22), (7, 25), (9, 23)], K, None, None, outline=K)
    ic.rect(25, 22, 26, 25, ORANGE)
