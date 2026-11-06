"""Wallpapers inspired by the translucent, Aqua, and cosmic eras."""
import numpy as np
import cv2
from core import (F, grid, axes, smooth, ramp, mix, solid, fbm, noise1,
                  lowres, stars, hexrgb, vignette, blur)
from patterns import block_size


def _bbox(W, H, cx, cy, r):
    x0 = int(max(0, (cx - r) * H + W / 2)); x1 = int(min(W, (cx + r) * H + W / 2) + 1)
    y0 = int(max(0, (cy - r) * H)); y1 = int(min(H, (cy + r) * H) + 1)
    return slice(y0, y1), slice(x0, x1)


def _bubble(img, X, Y, W, H, cx, cy, r, tint, a=1.0):
    sy, sx = _bbox(W, H, cx, cy, r * 1.05)
    if sy.start >= sy.stop or sx.start >= sx.stop:
        return
    x, y = X[sy, sx], Y[sy, sx]
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / F(r)
    inside = smooth(1.0, 0.985, d)
    body = inside * (0.10 + 0.22 * d ** 4)
    rim = np.exp(-((d - 0.985) / 0.02) ** 2) * 0.55
    hd = np.sqrt((x - cx + 0.42 * r) ** 2 + ((y - cy + 0.45 * r) * 1.4) ** 2) / F(r)
    hi = np.exp(-(hd / 0.16) ** 2) * 0.85 * inside
    region = img[sy, sx]
    region[:] = mix(region, tint, (body + rim) * F(a))
    region += (hi * F(a))[..., None]


def translucent(W, H):
    X, Y = grid(W, H)
    r = np.sqrt(X ** 2 + (Y - 0.35) ** 2)
    img = ramp(r, [(0, '#3fd0dc'), (0.45, '#1596b0'), (1.2, '#063f63')])
    rng = np.random.default_rng(1998)
    a = W / H
    for _ in range(16):
        rad = float(rng.uniform(0.05, 0.24))
        cx = float(rng.uniform(-a / 2, a / 2)); cy = float(rng.uniform(0.0, 1.0))
        _bubble(img, X, Y, W, H, cx, cy, rad, hexrgb('#bff6ff'), rng.uniform(0.5, 1))
    return vignette(img, X, Y, 0.3)


def five_flavors(W, H):
    X, Y = grid(W, H)
    t = (X * 0.8 + Y * 0.6) / 0.42
    cols = np.stack([hexrgb(c) for c in
                     ('#2b6fd6', '#7a3fb8', '#ff8a1e', '#79c43a', '#e2305c')])
    i = np.floor(t).astype(int) % 5
    f = t - np.floor(t)
    a = cols[i]; b = cols[(i + 1) % 5]
    img = mix(a, b, smooth(0.86, 1.0, f))
    img = mix(img, np.ones(3, F), 0.25 * np.exp(-((f - 0.3) / 0.12) ** 2))
    gloss = smooth(0.55, 0.0, Y) * 0.22
    img += gloss[..., None]
    s = block_size(H)
    rows = ((np.arange(H) // s) % 2).astype(F)
    img *= (1 - 0.035 * rows)[:, None, None]
    return vignette(img, X, Y, 0.25)


def graphite(W, H):
    X, Y = grid(W, H)
    r = np.sqrt((X * 0.8) ** 2 + (Y + 0.15) ** 2)
    img = ramp(r, [(0, '#6a6f78'), (0.6, '#3a3d44'), (1.4, '#141518')])
    arc = np.sqrt(X ** 2 + (Y - 1.9) ** 2)
    img += (np.exp(-((arc - 1.45) / 0.05) ** 2) * 0.08)[..., None]
    s = block_size(H)
    rows = ((np.arange(H) // s) % 2).astype(F)
    img *= (1 - 0.05 * rows)[:, None, None]
    return img


def aqua_swoosh(W, H):
    X, Y = grid(W, H)
    img = ramp(Y + 0.15 * X, [(-0.4, '#0b4cc0'), (0.6, '#062a7a'), (1.4, '#020a2a')])
    light = np.zeros((H, W), F)
    for k, (amp, ph, w, g) in enumerate([(0.22, 0.0, 0.10, 0.8), (0.18, 0.9, 0.05, 0.9),
                                         (0.26, 2.1, 0.16, 0.5), (0.15, 3.0, 0.03, 1.0),
                                         (0.20, 4.2, 0.07, 0.7)]):
        x = X[0]
        yc = 0.62 + amp * np.sin(1.25 * x + ph) + 0.06 * np.sin(2.7 * x + ph * 2)
        yc = yc - 0.18 * x
        dy = Y - yc[None, :]
        light += F(g) * np.exp(-(dy / w) ** 2) * (0.6 + 0.4 * np.exp(-(dy / (w * 0.15)) ** 2))
    silk = lowres(lambda x, y: fbm(x * 3, y * 6, 4, 11), W, H, 4)
    light *= 0.75 + 0.5 * silk
    tint = ramp(Y, [(0, '#9fe0ff'), (1, '#3c8cff')])
    img = img + tint * (1 - np.exp(-light * 1.1))[..., None]
    return vignette(img, X, Y, 0.35)


def aqua_pinstripe(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#f5f8fc'), (1, '#dfe6ee')])
    s = block_size(H)
    rows = ((np.arange(H) // s) % 2).astype(F)
    img *= (1 - 0.03 * rows)[:, None, None]
    blue = hexrgb('#1c7cf0')
    for off, w, a in ((-0.15, 0.22, 0.30), (0.12, 0.10, 0.45), (0.32, 0.05, 0.55)):
        yc = 0.55 + off + 0.25 * np.sin(X[0] * 1.1 + off * 4) - 0.1 * X[0]
        d = (Y - yc[None, :]) / w
        band = smooth(1.0, 0.2, np.abs(d)) * a
        shade = mix(blue, hexrgb('#9fd4ff'), smooth(1, -1, d))
        img = mix(img, shade, band)
    return img


def brushed_metal(W, H):
    X, Y = grid(W, H)
    th = min(H, 2160)
    tw = int(W * th / H)
    rng = np.random.default_rng(2003)
    n = rng.random((th, tw), dtype=F)
    n = cv2.blur(n, (max(3, tw // 6), 1))
    n = cv2.blur(n, (max(3, tw // 40), 1))
    n = (n - n.mean()) / (n.std() + 1e-6)
    n = cv2.resize(n, (W, H), interpolation=cv2.INTER_LINEAR)
    ang = np.arctan2(Y - 0.5, X)
    rr = np.sqrt(X ** 2 + (Y - 0.5) ** 2)
    sheen = 0.5 + 0.5 * np.cos(2 * ang) * smooth(0.0, 0.35, rr)
    base = 0.50 + 0.18 * sheen - 0.10 * smooth(0.0, 1.0, np.abs(X) / (W / H))
    v = base + 0.035 * n
    img = np.stack([v * 0.97, v * 0.99, v * 1.03], -1).astype(F)
    return vignette(img, X, Y, 0.2)


def aurora_blue(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#010614'), (0.6, '#04234a'), (1, '#0a4c7a')])
    def field(x, y):
        yc = 0.42 + 0.14 * np.sin(x * 1.3) + 0.12 * (noise1(x[0] * 1.5, 3)[None, :] - 0.5)
        dy = y - yc
        prof = np.where(dy < 0, np.exp(-(dy / 0.26) ** 2), np.exp(-(dy / 0.05) ** 2))
        streak = fbm(x * 18, y * 0.9, 4, 5)
        return prof * (0.25 + 1.2 * streak ** 2)
    a = lowres(field, W, H, 3)
    col = ramp(Y, [(0, '#2a5cff'), (0.35, '#2fb7ff'), (0.6, '#5effd2')])
    img = img + col * (1 - np.exp(-a * 1.4))[..., None]
    img += (stars(W, H, 1600, 13, 1.1, 0.55, 0.7))[..., None]
    return img


def nebula_violet(W, H):
    X, Y = grid(W, H)
    def neb(x, y):
        wx = fbm(x * 2 + 3, y * 2, 4, 21); wy = fbm(x * 2, y * 2 + 7, 4, 22)
        return fbm(x * 2.2 + wx * 1.6, y * 2.2 + wy * 1.6, 6, 23)
    n = lowres(neb, W, H, 4)
    img = ramp(n, [(0.2, '#030010'), (0.45, '#1c0638'), (0.6, '#5a1a8c'),
                   (0.72, '#c03a9a'), (0.85, '#ffd0f0')])
    img *= (0.35 + 0.65 * smooth(1.0, 0.2, Y))[..., None]
    yc = 0.78 + 0.10 * np.sin(X[0] * 1.6 + 0.5)
    dy = Y - yc[None, :]
    wave = np.where(dy < 0, np.exp(-(dy / 0.18) ** 2), np.exp(-(dy / 0.02) ** 2))
    img += ramp(Y, [(0.4, '#6a2cff'), (0.8, '#ff5ad0')]) * (wave * 0.8)[..., None]
    img += stars(W, H, 2600, 31, 1.2, 1.0, 0.9)[..., None]
    return img


def snow_aurora(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#0a0730'), (0.7, '#20136a'), (1, '#3a2390')])
    d = (Y - (0.5 - 0.35 * X - 0.06 * np.sin(X * 3))) / 0.16
    band = np.exp(-d ** 2) * lowres(lambda x, y: 0.5 + fbm(x * 4 - y * 6, y * 3, 4, 41), W, H, 4)
    img += ramp(d, [(-2, '#5a6cff'), (0, '#b98cff'), (2, '#ff9ad8')]) * band[..., None] * 0.9
    for cnt, sig, br, sd in ((60, 22, 0.25, 1), (180, 9, 0.35, 2), (900, 2.5, 0.6, 3)):
        img += stars(W, H, cnt, sd + 50, sig, 1.0, br)[..., None] * hexrgb('#e8eaff')
    return vignette(img, X, Y, 0.3)


def spiral_galaxy(W, H):
    X, Y = grid(W, H)
    def gal(x, y):
        c, s = np.cos(0.45), np.sin(0.45)
        u = (x * c + (y - 0.5) * s); v = (-x * s + (y - 0.5) * c) / 0.48
        r = np.sqrt(u * u + v * v) + 1e-4
        th = np.arctan2(v, u)
        arm = np.cos(2 * (th - np.log(r) / 0.28)) * 0.5 + 0.5
        dust = fbm(u * 9, v * 9, 5, 61)
        dens = arm ** 3 * np.exp(-r / 0.32) * (0.5 + dust) + 2.2 * np.exp(-(r / 0.06) ** 2)
        lane = smooth(0.55, 0.7, dust) * arm * np.exp(-r / 0.3)
        return np.stack([dens, r, lane], -1)
    g = lowres(gal, W, H, 3)
    dens, r, lane = g[..., 0], g[..., 1], g[..., 2]
    col = ramp(r, [(0, '#fff2d8'), (0.08, '#ffd6a0'), (0.2, '#a7b8ff'), (0.5, '#5a7cff')])
    img = ramp(Y, [(0, '#020109'), (1, '#0a0420')])
    img = img + col * (1 - np.exp(-dens * 1.5))[..., None]
    img *= (1 - 0.5 * lane)[..., None]
    img += stars(W, H, 3000, 71, 1.0, 1.0, 0.8)[..., None]
    return img
