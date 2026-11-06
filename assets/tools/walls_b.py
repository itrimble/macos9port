"""Wallpapers inspired by the landscape and abstract-gradient eras."""
import numpy as np
from core import (F, grid, axes, smooth, ramp, mix, fbm, noise1, lowres,
                  stars, hexrgb, vignette)


def _ridge(x, base, amp, freq, seed, oct=4, sharp=0.0):
    n = noise1(x * freq, seed, oct)
    if sharp:
        n = (1 - np.abs(n * 2 - 1)) ** (1 + sharp)
    return base - amp * n


def _layers(img, X, Y, layers, haze_col, fog=0.0):
    """layers: list of (ridge_y(1D), color_hex, haze 0..1) back to front."""
    x = X[0]
    for ry, col, hz in layers:
        mask = smooth(-0.0012, 0.0012, Y - ry[None, :])
        c = mix(hexrgb(col), hexrgb(haze_col), F(hz))
        depth = np.clip((Y - F(ry.min())) * 4, 0, 1)
        shade = c[None, None, :] * (1 - 0.25 * depth)[..., None]
        if fog:
            shade = mix(shade, hexrgb(haze_col), fog * np.exp(-(Y - F(ry.min())).clip(0) * 9))
        img = mix(img, shade, mask)
    return img


def big_wave(W, H):
    X, Y = grid(W, H)
    sky = ramp(Y, [(0, '#0d2a4a'), (0.5, '#3f7ea6'), (1, '#9fd2e6')])
    x = X[0]
    crest = 0.62 - 0.36 * np.exp(-((x + 0.15) / 0.45) ** 2) - 0.04 * np.sin(x * 2.3)
    face = Y - crest[None, :]
    sea = ramp(face, [(0, '#e6fbff'), (0.03, '#5fc4d8'), (0.25, '#0f5c8a'), (0.7, '#031d3a')])
    tex = lowres(lambda a, b: fbm(a * 3 + b * 9, b * 22, 5, 81), W, H, 3)
    sea *= (0.85 + 0.3 * tex)[..., None]
    foam = np.exp(-(face / 0.012) ** 2) * (0.6 + 0.6 * tex)
    img = mix(sky, sea, smooth(-0.002, 0.002, face))
    img = img + foam[..., None] * 0.9
    lip = np.exp(-((Y - crest[None, :] + 0.02) / 0.03) ** 2) * smooth(0.0, 0.6, x)[None, :]
    img += (lip * tex * 0.6)[..., None]
    return vignette(img, X, Y, 0.35)


def granite_peaks(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#2b3f7a'), (0.35, '#d86a5a'), (0.55, '#ffc58a'), (1, '#ffe7c4')])
    x = X[0]
    L = [(_ridge(x, 0.56, 0.34, 0.9, 1, sharp=0.3), '#7a5c78', 0.55),
         (_ridge(x, 0.64, 0.20, 1.6, 2, sharp=0.4), '#5a4060', 0.35),
         (_ridge(x, 0.76, 0.16, 2.1, 3, sharp=0.5), '#3a2840', 0.18),
         (_ridge(x, 0.90, 0.12, 2.8, 4), '#1a1020', 0.0)]
    return _layers(img, X, Y, L, '#ffb98a', fog=0.4)


def night_cliff(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#060b24'), (0.55, '#24306a'), (0.85, '#b8628a'), (1, '#f0a07a')])
    img += stars(W, H, 2200, 91, 1.0, 0.6, 0.8)[..., None]
    x = X[0]
    a = W / H
    cliff = 0.12 + 0.55 * smooth(-0.05, 0.55, x + 0.1) + 0.03 * noise1(x * 8, 9)
    cliff = np.where(x < -0.05, 0.12 + 0.04 * noise1(x * 6, 7) + 0.02 * (x + a / 2), cliff)
    L = [(_ridge(x, 0.86, 0.06, 2.0, 12), '#3a3460', 0.3),
         (cliff, '#15142c', 0.0),
         (_ridge(x, 0.96, 0.04, 6.0, 13), '#05050f', 0.0)]
    img = _layers(img, X, Y, L, '#6a5a9a')
    rim = np.exp(-((Y - cliff[None, :]) / 0.004) ** 2) * smooth(0.6, 0.9, Y)
    return img + (rim * 0.25)[..., None] * hexrgb('#ffb08a')


def blue_ridges(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#8fb8e6'), (0.45, '#d8e8f6'), (1, '#e8f0f8')])
    x = X[0]
    pal = ['#a8c4e2', '#86a8d4', '#6488c0', '#4668a8', '#2c4a88', '#1a3268', '#0e1e48']
    L = [(_ridge(x, 0.40 + i * 0.09, 0.10, 1.2 + i * 0.5, 20 + i), c, 0.0)
         for i, c in enumerate(pal)]
    return _layers(img, X, Y, L, '#e8f0f8', fog=0.7)


def alpine_lake(W, H):
    X, Y = grid(W, H)
    hz = 0.58
    x = X[0]
    sky = ramp(Y, [(0, '#3a6ea8'), (0.5, '#c8d8e8')])
    L = [(_ridge(x, 0.48, 0.30, 1.0, 31, sharp=0.5), '#8a9ab8', 0.3),
         (_ridge(x, 0.55, 0.16, 1.8, 32, sharp=0.3), '#4a5a70', 0.1),
         (_ridge(x, 0.585, 0.03, 9.0, 33), '#c46a2a', 0.0)]
    top = _layers(sky, X, Y, L, '#c8d8e8', fog=0.3)
    # reflection: mirror rows above the horizon, add ripples
    ry = np.clip((2 * hz - Y[:, 0]) * H, 0, H - 1).astype(int)
    rip = lowres(lambda a, b: fbm(a * 4, b * 60, 3, 34), W, H, 2)
    shift = ((rip - 0.5) * 0.004 * H).astype(int)
    rows = np.clip(ry[:, None] + shift, 0, H - 1)
    cols = np.broadcast_to(np.arange(W), (H, W))
    refl = top[rows, cols] * F(0.78) + hexrgb('#0a2440') * F(0.15)
    img = np.where((Y > hz)[..., None], refl, top)
    return vignette(img, X, Y, 0.25)


def _dunes(W, H, night):
    X, Y = grid(W, H)
    if night:
        img = ramp(Y, [(0, '#050a1e'), (0.5, '#1a2650'), (0.7, '#3a3a6a')])
        img += stars(W, H, 2400, 101, 1.0, 0.6, 0.8)[..., None]
        lit, dark = '#4a4a7a', '#141530'
    else:
        img = ramp(Y, [(0, '#5a9ad0'), (0.55, '#f2d2a8'), (0.7, '#ffe2b8')])
        lit, dark = '#f0b070', '#8a4a2a'
    x = X[0]
    for i in range(5):
        yb = 0.56 + i * 0.1
        crest = yb - 0.08 * (1 - np.abs(np.sin(x * (1.0 + i * 0.4) + i * 1.7)) ** 0.6) \
            - 0.02 * noise1(x * 3, 110 + i)
        d = Y - crest[None, :]
        mask = smooth(-0.001, 0.001, d)
        side = np.sin(x * (1.0 + i * 0.4) + i * 1.7)
        lightness = smooth(-0.2, 0.4, side)[None, :] * np.exp(-d.clip(0) * 6)
        col = mix(ramp(d, [(0, dark), (0.2, dark)]), ramp(d, [(0, lit), (0.3, dark)]), lightness)
        col *= F(0.75 + 0.25 * i / 4)
        img = mix(img, col, mask)
    return vignette(img, X, Y, 0.25)


def dunes_day(W, H):
    return _dunes(W, H, False)


def dunes_night(W, H):
    return _dunes(W, H, True)


def island_cliff(W, H):
    X, Y = grid(W, H)
    hz = 0.62
    sky = ramp(Y, [(0, '#1e3a6e'), (0.4, '#7a8ac0'), (0.6, '#f4b08a')])
    cl = lowres(lambda a, b: fbm(a * 2.5, b * 7, 5, 120), W, H, 4)
    sky = mix(sky, hexrgb('#ffd8c0'), smooth(0.55, 0.8, cl) * smooth(0.6, 0.1, Y) * 0.6)
    sea = ramp(Y, [(hz, '#5a7ab0'), (1, '#0a2448')])
    sea *= (0.9 + 0.2 * lowres(lambda a, b: fbm(a * 3, b * 80, 3, 121), W, H, 2))[..., None]
    img = np.where((Y > hz)[..., None], sea, sky)
    x = X[0]
    isl = hz - 0.30 * np.exp(-((x - 0.15) / 0.35) ** 2) * (0.8 + 0.2 * noise1(x * 6, 122)) \
        - 0.08 * np.exp(-((x + 0.5) / 0.3) ** 2)
    m = smooth(-0.001, 0.001, Y - isl[None, :]) * (Y < hz + 0.002)
    img = mix(img, ramp(Y - isl[None, :], [(0, '#3a3a5a'), (0.2, '#1a1a30')]), m)
    glow = np.exp(-((Y - hz) / 0.004) ** 2) * 0.4
    return vignette(img + glow[..., None] * hexrgb('#ffd0a0'), X, Y, 0.25)


def _bands(W, H, pal, freq, amp, seed, bg, tilt=0.0):
    X, Y = grid(W, H)
    img = ramp(Y, bg)
    x = X[0]
    n = len(pal)
    for i, c in enumerate(pal):
        yb = 0.25 + 0.75 * (i + 1) / (n + 1)
        edge = yb + amp * np.sin(x * freq + i * 0.9 + seed) + 0.04 * np.sin(x * freq * 2.3 + i) \
            + tilt * x
        d = Y - edge[None, :]
        m = smooth(-0.0015, 0.0015, d)
        col = ramp(d, [(0, c[0]), (0.35, c[1])])
        img *= (1 - 0.35 * np.exp(-((-d).clip(0)) / 0.025) * (d < 0))[..., None]
        img = mix(img, col, m)
    return vignette(img, X, Y, 0.2)


def coastal_gradient(W, H):
    pal = [('#ff9a6a', '#ff6a5a'), ('#ff5a8a', '#c23a8a'), ('#7a4ad0', '#4a2aa0'),
           ('#2a5ad0', '#1a3aa0'), ('#123a8a', '#0a1a5a')]
    return _bands(W, H, pal, 1.6, 0.09, 0.3, [(0, '#5ab0ff'), (0.5, '#ffc4a0')])


def silk_folds(W, H):
    pal = [('#ffa8d8', '#ff6ab0'), ('#d06aff', '#9a3ad8'), ('#7a6aff', '#4a3ad0'),
           ('#4a8aff', '#2a5ad8'), ('#2ab0ff', '#1a6ad0'), ('#1a3a9a', '#0a1a5a')]
    return _bands(W, H, pal, 2.2, 0.13, 1.4, [(0, '#2a1a6a'), (1, '#ff8ad0')], tilt=-0.12)


def bloom(W, H):
    X, Y = grid(W, H)
    cx, cy = 0.0, 0.62
    r = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2) + 1e-5
    th = np.arctan2(Y - cy, X - cx)
    img = ramp(r, [(0, '#ffd27a'), (0.8, '#3a0a1a')])
    for k, (n, rr, c0, c1, ph) in enumerate([(7, 0.95, '#ff4a1a', '#7a0a10', 0.0),
                                            (7, 0.70, '#ff7a2a', '#b01a10', 0.45),
                                            (7, 0.45, '#ffaa4a', '#e04a1a', 0.0),
                                            (7, 0.24, '#ffd88a', '#ff7a2a', 0.45)]):
        petal = rr * (0.62 + 0.38 * np.abs(np.cos((th + ph) * n / 2)) ** 0.7)
        d = r - petal
        m = smooth(0.003, -0.003, d)
        col = ramp(r / rr, [(0, c0), (1, c1)])
        img *= (1 - 0.45 * np.exp(-(d.clip(0)) / 0.03))[..., None]
        img = mix(img, col, m)
    return vignette(img, X, Y, 0.35)


def vineyard_hills(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#7ab8e8'), (0.45, '#f4e2b0')])
    x = X[0]
    pal = [('#a8b870', '#7a9a50'), ('#8aa858', '#5a8038'), ('#6a9040', '#3a6a28'),
           ('#4a7a30', '#24501a'), ('#2a5a20', '#123010')]
    for i, (c0, c1) in enumerate(pal):
        edge = 0.42 + i * 0.12 + 0.07 * np.sin(x * (1.1 + i * 0.3) + i * 2.1)
        d = Y - edge[None, :]
        m = smooth(-0.0015, 0.0015, d)
        rows = 0.5 + 0.5 * np.sin((x[None, :] * 0.6 + d * 3) * 120 / (1 + i))
        col = ramp(d, [(0, c0), (0.25, c1)]) * (0.92 + 0.08 * rows)[..., None]
        img = mix(img, col, m)
    sun = np.exp(-((X - 0.6) ** 2 + (Y - 0.18) ** 2) / 0.02)
    return vignette(img + (sun * 0.25)[..., None], X, Y, 0.2)


def glow_waves(W, H):
    X, Y = grid(W, H)
    img = ramp(Y, [(0, '#021a24'), (1, '#063a4a')])
    x = X[0]
    for i, (c, w) in enumerate([('#1ad0a0', 0.12), ('#2ab0ff', 0.09), ('#7aff8a', 0.07),
                                ('#00e0d0', 0.05), ('#5a8aff', 0.10)]):
        yc = 0.5 + 0.18 * np.sin(x * (0.9 + 0.25 * i) + i * 1.3) + 0.05 * np.sin(x * 3 + i)
        d = (Y - yc[None, :]) / w
        a = np.where(d > 0, np.exp(-d ** 2), np.exp(-(d / 2.5) ** 2)) * 0.55
        img = img + hexrgb(c) * a[..., None] * (1 - img)
    return vignette(img, X, Y, 0.3)


def liquid_glass(W, H):
    X, Y = grid(W, H)
    base = lowres(lambda a, b: fbm(a * 0.9 + 3, b * 0.9, 4, 141), W, H, 6)
    img = ramp(base + 0.25 * X, [(0.1, '#2a4aff'), (0.4, '#8a5aff'), (0.6, '#ff6ab0'),
                                 (0.85, '#ffb05a')])
    rng = np.random.default_rng(2025)
    a = W / H
    for _ in range(5):
        cx = float(rng.uniform(-a / 2.3, a / 2.3)); cy = float(rng.uniform(0.15, 0.85))
        r = float(rng.uniform(0.14, 0.34))
        d = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2) / r
        inside = smooth(1.0, 0.97, d)
        lens = np.sqrt(np.clip(1 - d ** 2, 0, 1))
        shifted = ramp(base + 0.25 * X + 0.35 * (1 - lens), [(0.1, '#5a7aff'), (0.4, '#b08aff'),
                                                             (0.6, '#ff9ad0'), (0.85, '#ffd09a')])
        img = mix(img, shifted * 1.05, inside)
        rim = np.exp(-((d - 0.97) / 0.02) ** 2) * 0.3
        spec = np.exp(-(((X - cx + 0.4 * r) ** 2 + (Y - cy + 0.45 * r) ** 2) / (0.06 * r) ** 2))
        img = img + (rim + spec * 0.35 * inside)[..., None]
    return vignette(img, X, Y, 0.2)
