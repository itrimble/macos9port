"""Classic-era pixel patterns, drawn with exact integer scaling.

Each pattern is an original small tile. At render time every tile pixel
becomes a square block of round(H / 384) device pixels, so the pattern
stays crisp at 768p, 4K, and 8K alike.
"""
import numpy as np
from core import hexrgb, F


def _tile(rows, palette):
    idx = np.array([[int(c) for c in r] for r in rows])
    pal = np.stack([hexrgb(p) for p in palette])
    return pal[idx]


def halftone():
    rows = ["10101010", "01010101"] * 4
    return _tile(rows, ["#ffffff", "#000000"])


def basket():
    rows = ["11100001", "11100001", "11100001", "00011111",
            "00011110", "00011110", "00011110", "11100001"]
    return _tile(rows, ["#f2f2f2", "#1a1a1a"])


def tidal():
    rows = ["00112233", "01122330", "11223300", "12233001",
            "22330011", "23300112", "33001122", "30011223"]
    return _tile(rows, ["#2f5f9e", "#3a72b5", "#4a86c8", "#3a72b5"])


def periwinkle():
    rng = np.random.default_rng(1991)
    base = np.zeros((16, 16), int)
    pts = rng.integers(0, 16, (26, 2))
    for i, (y, x) in enumerate(pts):
        base[y, x] = 1 if i % 3 else 2
    rows = ["".join(str(v) for v in r) for r in base]
    return _tile(rows, ["#6f7fd1", "#8291dc", "#5d6cbd"])


def ripple():
    t = np.zeros((16, 16), int)
    for x in range(16):
        y = int(round(3.5 + 2.5 * np.sin(x / 16 * 2 * np.pi)))
        t[y % 16, x] = 1
        t[(y + 8) % 16, x] = 2
    rows = ["".join(str(v) for v in r) for r in t]
    return _tile(rows, ["#a3acbf", "#c4cbd9", "#8790a6"])


def bubbles():
    t = np.zeros((16, 16), int)
    yy, xx = np.mgrid[0:16, 0:16]
    for cy, cx, r in ((4, 4, 3.2), (11, 12, 2.4), (12, 3, 1.6)):
        d = np.sqrt((yy - cy) ** 2 + (xx - cx) ** 2)
        t[(d > r - 0.8) & (d <= r + 0.2)] = 1
        t[(np.abs(yy - (cy - r * 0.45)) < 0.6) &
          (np.abs(xx - (cx - r * 0.45)) < 0.6)] = 2
    rows = ["".join(str(v) for v in r) for r in t]
    return _tile(rows, ["#8a78c8", "#a99ae0", "#e6e0ff"])


TILES = {
    "halftone": halftone, "basket": basket, "tidal": tidal,
    "periwinkle": periwinkle, "ripple": ripple, "bubbles": bubbles,
}


def block_size(H):
    return max(1, int(round(H / 384)))


def render_pattern(name, W, H):
    tile = TILES[name]()
    s = block_size(H)
    big = np.repeat(np.repeat(tile, s, 0), s, 1)
    th, tw = big.shape[:2]
    reps = (H // th + 2, W // tw + 2, 1)
    full = np.tile(big, reps)
    oy = ((H // 2) % th)
    ox = ((W // 2) % tw)
    return full[th - oy: th - oy + H, tw - ox: tw - ox + W].astype(F)


def tile_image(name):
    return TILES[name]()
