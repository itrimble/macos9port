"""Icons drawn from supplied bitmaps rather than from shapes.

quake3: the Quake III Arena icon, sources/quake3.gif (32x32), taken unchanged from code/unix/quake3.gif in
id Software's Quake III Arena source release, which is licensed GPL-2.0 (the engine tree is kept separately).
The 32-pixel set snaps it to the classic system palette; the HiDPI sets resample the same bitmap smoothly.
"""
import os
import numpy as np
from PIL import Image
from core import PAL

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
