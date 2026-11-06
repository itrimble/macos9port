"""Pixel-art toolkit for OS Pi icons.

Icons are drawn aliased on a 32x32 RGBA canvas using only colors from the
classic 8-bit system palette (see mac_icon_color in src/mac_icon_palette.c),
so they encode losslessly to icl8 + ICN# mask data.
"""
import numpy as np
from PIL import Image, ImageDraw

CUBE = [255, 204, 153, 102, 51, 0]
RAMP = [238, 221, 187, 170, 136, 119, 85, 68, 34, 17]


def palette():
    pal = []
    for i in range(256):
        if i < 216:
            pal.append((CUBE[i // 36], CUBE[(i // 6) % 6], CUBE[i % 6]))
        else:
            s = i - 216
            v = RAMP[s % 10] if s < 40 else 0
            r = v if (s < 10 or s >= 30) else 0
            g = v if (10 <= s < 20 or s >= 30) else 0
            b = v if (20 <= s < 30 or s >= 30) else 0
            pal.append((r, g, b))
    return np.array(pal, np.int32)


PAL = palette()
PAL_INDEX = {tuple(c): i for i, c in enumerate(PAL.tolist())}


def c(h):
    h = h.lstrip('#')
    rgb = tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    if rgb not in PAL_INDEX:
        raise ValueError(f"#{h} is not in the system palette")
    return rgb + (255,)


# Named palette-safe colors.
K, W = c('000000'), c('ffffff')
G1, G2, G3, G4, G5, G6, G7, G8 = (c(x) for x in
                                  ('eeeeee', 'dddddd', 'bbbbbb', 'aaaaaa', '888888', '777777', '555555', '444444'))
G9 = c('222222')
FOLD, FOLD_HI, FOLD_LO, FOLD_DK = c('9999ff'), c('ccccff'), c('6666cc'), c('333366')
RED, RED_HI, RED_DK = c('ff3333'), c('ff9999'), c('990000')
ORANGE, YELLOW, YEL_DK = c('ff9900'), c('ffcc00'), c('cc9900')
GREEN, GREEN_HI, GREEN_DK = c('33cc33'), c('99ff99'), c('006600')
BLUE, BLUE_HI, BLUE_DK = c('3366cc'), c('99ccff'), c('003399')
TEAL, TEAL_HI, TEAL_DK = c('33cccc'), c('99ffff'), c('006666')
PURPLE, PURPLE_HI, PURPLE_DK = c('9966cc'), c('cc99ff'), c('330066')
BROWN, BROWN_HI, TAN = c('996633'), c('cc9966'), c('ffcc99')
PINK = c('ff99cc')
NAVY = c('000066')


class Icon:
    def __init__(self, size=32):
        self.size = size
        self.im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    # --- low level -------------------------------------------------------
    def px(self, x, y, col):
        if 0 <= x < self.size and 0 <= y < self.size:
            self.im.putpixel((x, y), col)

    def rect(self, x0, y0, x1, y1, col):
        self.d.rectangle((x0, y0, x1, y1), fill=col)

    def frame(self, x0, y0, x1, y1, col):
        self.d.rectangle((x0, y0, x1, y1), outline=col)

    def line(self, pts, col, w=1):
        self.d.line(pts, fill=col, width=w)

    def poly(self, pts, col):
        self.d.polygon(pts, fill=col)

    def ellipse(self, box, col, outline=None):
        self.d.ellipse(box, fill=col, outline=outline)

    def glyph(self, rows, x, y, cmap):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in cmap:
                    self.px(x + i, y + j, cmap[ch])

    # --- beveled shapes --------------------------------------------------
    def shape(self, draw_fn, fill, hi=None, lo=None, outline=K):
        m = Image.new('L', (self.size, self.size), 0)
        draw_fn(ImageDraw.Draw(m))
        m = np.array(m) > 0
        inner = m.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            inner &= np.roll(np.roll(m, dy, 0), dx, 1)
        inner[0, :] = inner[-1, :] = False
        inner[:, 0] = inner[:, -1] = False
        inner &= m
        edge = m & ~inner
        arr = np.array(self.im)

        def put(mask, col):
            if col is not None:
                arr[mask] = col

        put(inner, fill)
        if hi is not None:
            up = np.roll(edge, 1, 0) | np.roll(edge, 1, 1)
            put(inner & up, hi)
        if lo is not None:
            dn = np.roll(edge, -1, 0) | np.roll(edge, -1, 1)
            put(inner & dn & ~(np.roll(edge, 1, 0) | np.roll(edge, 1, 1)), lo)
        put(edge, outline)
        self.im = Image.fromarray(arr)
        self.d = ImageDraw.Draw(self.im)
        return m

    def box(self, x0, y0, x1, y1, fill, hi=None, lo=None, outline=K, r=0):
        if r:
            return self.shape(lambda d: d.rounded_rectangle((x0, y0, x1, y1), r, fill=1),
                              fill, hi, lo, outline)
        return self.shape(lambda d: d.rectangle((x0, y0, x1, y1), fill=1), fill, hi, lo, outline)

    def pgon(self, pts, fill, hi=None, lo=None, outline=K):
        return self.shape(lambda d: d.polygon(pts, fill=1), fill, hi, lo, outline)

    def oval(self, box, fill, hi=None, lo=None, outline=K):
        return self.shape(lambda d: d.ellipse(box, fill=1), fill, hi, lo, outline)

    def shadow(self, x0, y0, x1, y1, col=G7):
        self.rect(x0, y0, x1, y1, col)

    # --- export ----------------------------------------------------------
    def rgba(self):
        return np.array(self.im)

    def mask(self):
        a = np.array(self.im)[..., 3] > 0
        # fill enclosed holes so the mask matches classic ICN# masks
        from scipy.ndimage import binary_fill_holes
        return binary_fill_holes(a)

    def indices(self):
        a = np.array(self.im)
        out = np.zeros(a.shape[:2], np.uint8)
        vis = a[..., 3] > 0
        for y, x in zip(*np.nonzero(vis)):
            out[y, x] = PAL_INDEX[tuple(int(v) for v in a[y, x, :3])]
        # inside-mask transparent holes become white, as on classic Mac
        m = self.mask()
        out[m & ~vis] = 0
        return out
