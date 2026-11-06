"""High-resolution renderer for the OS Pi icons.

The icon drawings in set_a/set_b use 32-unit coordinates. HiIcon replays
them on a k-times larger canvas: rectangles, ovals, polygons, arcs, and
lines are redrawn as true shapes (not enlarged pixels), outlines and bevels
keep their proportions, and bitmap emblems are smoothed. The result is
supersampled down to each target size with premultiplied alpha.
"""
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter, binary_fill_holes
from core import Icon


def scale2x(p):
    """Scale2x (EPX) on a boolean bitmap; keeps diagonal strokes connected."""
    pad = np.pad(p, 1, mode='edge')
    a, d = pad[:-2, 1:-1], pad[2:, 1:-1]
    c, b = pad[1:-1, :-2], pad[1:-1, 2:]
    out = np.zeros((p.shape[0] * 2, p.shape[1] * 2), p.dtype)
    out[0::2, 0::2] = np.where((c == a) & (c != d) & (a != b), c, p)
    out[0::2, 1::2] = np.where((a == b) & (a != c) & (b != d), b, p)
    out[1::2, 0::2] = np.where((d == c) & (d != b) & (c != a), c, p)
    out[1::2, 1::2] = np.where((b == d) & (b != a) & (d != c), b, p)
    return out


class ScaledDraw:
    """ImageDraw proxy that maps 32-unit pixel coordinates to a k-scale canvas."""

    def __init__(self, draw, k):
        self.dr, self.k = draw, k

    def _box(self, b):
        k = self.k
        x0, y0, x1, y1 = b
        return (x0 * k, y0 * k, (x1 + 1) * k - 1, (y1 + 1) * k - 1)

    def _pt(self, p):
        k = self.k
        return ((p[0] + 0.5) * k - 0.5, (p[1] + 0.5) * k - 0.5)

    def rectangle(self, b, fill=None, outline=None, width=1):
        self.dr.rectangle(self._box(b), fill=fill, outline=outline, width=max(1, width * self.k))

    def rounded_rectangle(self, b, r, fill=None, outline=None, width=1):
        self.dr.rounded_rectangle(self._box(b), r * self.k, fill=fill, outline=outline,
                                  width=max(1, width * self.k))

    def ellipse(self, b, fill=None, outline=None, width=1):
        self.dr.ellipse(self._box(b), fill=fill, outline=outline, width=max(1, width * self.k))

    def arc(self, b, start, end, fill=None, width=1):
        self.dr.arc(self._box(b), start, end, fill=fill, width=max(1, width * self.k))

    def polygon(self, pts, fill=None, outline=None):
        p = [self._pt(q) for q in pts]
        self.dr.polygon(p, fill=fill)
        # 1x polygons cover their boundary pixels; grow by half a unit to match.
        self.dr.line(p + [p[0]], fill=fill, width=self.k, joint='curve')
        for q in p:
            r = self.k / 2
            self.dr.ellipse((q[0] - r, q[1] - r, q[0] + r, q[1] + r), fill=fill)

    def line(self, pts, fill=None, width=1, joint=None):
        p = [self._pt(q) for q in pts]
        w = max(1, width * self.k)
        self.dr.line(p, fill=fill, width=w, joint='curve')
        if len(p) == 1 or all(q == p[0] for q in p):
            x, y = p[0]
            self.dr.rectangle((x - w / 2 + 0.5, y - w / 2 + 0.5, x + w / 2 - 0.5, y + w / 2 - 0.5),
                              fill=fill)
            return
        for q in (p[0], p[-1]):  # square caps, like 1x pixel lines
            self.dr.rectangle((q[0] - w / 2 + 0.5, q[1] - w / 2 + 0.5,
                               q[0] + w / 2 - 0.5, q[1] + w / 2 - 0.5), fill=fill)


class HiIcon(Icon):
    def __init__(self, k):
        self.k = k
        self.size = 32
        self.im = Image.new('RGBA', (32 * k, 32 * k), (0, 0, 0, 0))
        self._sync()

    def _sync(self):
        self.d = ScaledDraw(ImageDraw.Draw(self.im), self.k)

    def px(self, x, y, col):
        k = self.k
        if 0 <= x < 32 and 0 <= y < 32:
            ImageDraw.Draw(self.im).rectangle((x * k, y * k, x * k + k - 1, y * k + k - 1), fill=col)

    def glyph(self, rows, x, y, cmap):
        k = self.k
        arr = np.array(self.im)
        for ch, col in cmap.items():
            bm = np.zeros((32, 32), np.float32)
            for j, row in enumerate(rows):
                for i, c in enumerate(row):
                    if c == ch and 0 <= x + i < 32 and 0 <= y + j < 32:
                        bm[y + j, x + i] = 1
            if not bm.any():
                continue
            big = bm > 0
            while big.shape[0] < 32 * k:
                big = scale2x(big)
            m = gaussian_filter(big.astype(np.float32), 0.12 * k) > 0.5
            arr[m] = col
        self.im = Image.fromarray(arr)
        self._sync()

    def shape(self, draw_fn, fill, hi=None, lo=None, outline=(0, 0, 0, 255)):
        k = self.k
        mi = Image.new('L', self.im.size, 0)
        draw_fn(ScaledDraw(ImageDraw.Draw(mi), k))
        m = np.array(mi) > 0
        dist = distance_transform_edt(m)
        inner = dist > k
        arr = np.array(self.im)

        def sh(a, dy, dx):
            out = np.zeros_like(a)
            h, w = a.shape
            out[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = \
                a[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
            return out

        if fill is not None:
            arr[inner] = fill
        hmask = inner & ~(sh(inner, k, 0) & sh(inner, 0, k))
        if hi is not None:
            arr[hmask] = hi
        if lo is not None:
            arr[inner & ~(sh(inner, -k, 0) & sh(inner, 0, -k)) & ~hmask] = lo
        if outline is not None:
            arr[m & ~inner] = outline
        self.im = Image.fromarray(arr)
        self._sync()
        return m

    def mask(self):
        return binary_fill_holes(np.array(self.im)[..., 3] > 0)

    def solid(self):
        """Fill enclosed transparent holes with white, as the 1x icons do."""
        arr = np.array(self.im)
        holes = self.mask() & (arr[..., 3] == 0)
        arr[holes] = (255, 255, 255, 255)
        return Image.fromarray(arr)


def downsample(im, size):
    return im.convert('RGBa').resize((size, size), Image.LANCZOS).convert('RGBA')


def render_hd(fn, k=64):
    ic = HiIcon(k)
    fn(ic)
    return ic.solid()
