"""Shared helpers for OS Pi procedural wallpapers.

Coordinates are resolution independent: X runs from -aspect/2 to +aspect/2
(centered), Y runs from 0 (top) to 1 (bottom). Every wallpaper is a pure
function of (W, H), so it renders natively at any size, including 8K.
"""
import numpy as np
import cv2

F = np.float32


def hexrgb(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], F)


def grid(W, H):
    x = (np.arange(W, dtype=F) + 0.5 - W / 2) / H
    y = (np.arange(H, dtype=F) + 0.5) / H
    return np.meshgrid(x, y)


def axes(W, H):
    x = (np.arange(W, dtype=F) + 0.5 - W / 2) / H
    y = (np.arange(H, dtype=F) + 0.5) / H
    return x, y


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def ramp(t, stops):
    """Map scalar field t through [(pos, '#hex'), ...] -> (..., 3)."""
    pos = [p for p, _ in stops]
    cols = np.stack([hexrgb(c) for _, c in stops])
    out = np.empty(t.shape + (3,), F)
    for c in range(3):
        out[..., c] = np.interp(t, pos, cols[:, c])
    return out


def mix(a, b, t):
    if np.ndim(t) >= 2:
        t = np.asarray(t)[..., None]
    return a + (b - a) * t


def solid(W, H, col):
    return np.broadcast_to(hexrgb(col), (H, W, 3)).astype(F).copy()


def _hash(ix, iy, seed):
    n = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263
         + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n = n ^ (n >> 16)
    return (n & 0xFFFF).astype(F) / F(65535.0)


def vnoise(x, y, seed=0):
    ix = np.floor(x)
    iy = np.floor(y)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3 - 2 * fx)
    uy = fy * fy * (3 - 2 * fy)
    a = _hash(ix, iy, seed)
    b = _hash(ix + 1, iy, seed)
    c = _hash(ix, iy + 1, seed)
    d = _hash(ix + 1, iy + 1, seed)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x, y, octaves=5, seed=0, lac=2.0, gain=0.5):
    tot = np.zeros_like(x, dtype=F)
    amp, f, norm = 1.0, 1.0, 0.0
    for o in range(octaves):
        tot += F(amp) * vnoise(x * f, y * f, seed + o * 17)
        norm += amp
        amp *= gain
        f *= lac
    return tot / F(norm)


def noise1(x, seed=0, octaves=4):
    """1D fractal noise for ridge lines, evaluated on 1D arrays."""
    return fbm(x, np.full_like(x, F(seed * 0.37 + 0.5)), octaves, seed)


def lowres(fn, W, H, d=4):
    """Evaluate a smooth field at 1/d resolution and upsample cubically."""
    w, h = max(2, W // d), max(2, H // d)
    X, Y = grid(w, h)
    X *= F(w / W * H / h)  # keep identical coordinate span
    out = fn(X, Y).astype(F)
    return cv2.resize(out, (W, H), interpolation=cv2.INTER_CUBIC)


def stars(W, H, count_per_unit, seed=1, sigma=1.0, ymax=1.0, bright=1.0):
    """Soft stars whose size and density scale with resolution."""
    rng = np.random.default_rng(seed)
    aspect = W / H
    n = int(count_per_unit * aspect * ymax)
    xs = rng.uniform(0, W, n).astype(int)
    ys = (rng.uniform(0, 1, n) ** 1.0 * ymax * H).astype(int).clip(0, H - 1)
    mag = rng.power(0.35, n).astype(F) * F(bright)
    img = np.zeros((H, W), F)
    np.add.at(img, (ys, xs.clip(0, W - 1)), mag)
    s = sigma * H / 2160.0
    if s > 0.35:
        k = int(s * 6) | 1
        img = cv2.GaussianBlur(img, (k, k), s) * F(2 * np.pi * s * s)
    return np.clip(img, 0, 1.5)


def blur(img, sigma_units, H):
    s = max(0.5, sigma_units * H)
    return cv2.GaussianBlur(img, (0, 0), s)


def vignette(img, X, Y, strength=0.35, cx=0.0, cy=0.5):
    r = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2)
    v = 1 - F(strength) * smooth(0.35, 1.25, r)
    img *= v[..., None]
    return img


def to_u8(img, seed=7):
    """Quantize with triangular dither so 8K gradients never band."""
    H = img.shape[0]
    out = np.empty(img.shape, np.uint8)
    rng = np.random.default_rng(seed)
    step = 512
    for y0 in range(0, H, step):
        blk = img[y0:y0 + step]
        d = (rng.random(blk.shape, dtype=F) - rng.random(blk.shape, dtype=F))
        out[y0:y0 + step] = np.clip(blk * 255.0 + 0.5 + d, 0, 255).astype(np.uint8)
    return out
