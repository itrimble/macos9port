"""Procedural "fruit era" wallpapers (40s series), inspired by late-1990s
translucent-hardware desktops: soft color hazes with floating glass beads,
perforated bokeh light, pastel daisies, and a pi-themed default desktop.

Every function is a pure function of (W, H) and renders natively at 8K.
"""
import numpy as np
import cv2
from core import F, grid, smooth, ramp, mix, fbm, lowres, hexrgb, vignette


# --- glass beads -------------------------------------------------------------

def bead(img, W, H, x, y, r, col, blur=0.0, glow='#ffffff', alpha=1.0):
    """Composite one glossy glass bead. x, y, r, blur are in units of H."""
    cx, cy, R = x * H + W / 2, y * H, max(r * H, 0.8)
    s = blur * H
    pad = int(R + 3 * s + 3)
    x0, x1 = int(cx) - pad, int(cx) + pad + 1
    y0, y1 = int(cy) - pad, int(cy) + pad + 1
    if x1 <= 0 or y1 <= 0 or x0 >= W or y0 >= H:
        return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(F)
    nx, ny = (xx + 0.5 - cx) / R, (yy + 0.5 - cy) / R
    d2 = nx * nx + ny * ny
    a = np.clip((1 - np.sqrt(d2)) * R / 1.2, 0, 1)
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    base, lite = hexrgb(col), hexrgb(glow)
    diff = np.clip(-0.45 * nx - 0.6 * ny + 0.65 * nz, 0, 1)
    sss = np.clip(0.4 * nx + 0.7 * ny, 0, 1) ** 2
    spec = np.exp(-((nx + 0.33) ** 2 + (ny + 0.42) ** 2) / 0.018)
    spec2 = np.exp(-((nx - 0.28) ** 2 + (ny - 0.5) ** 2) / 0.05) * 0.25
    rim = (1 - nz) ** 3
    c = base * (0.35 + 0.75 * diff)[..., None] + (base * 0.5 + lite * 0.5) * (0.8 * sss)[..., None]
    c = c + lite * (rim * 0.25 + spec2)[..., None] + (spec * 1.1)[..., None]
    a = a * (0.82 + 0.18 * rim) * F(alpha)
    if s > 0.4:
        pre = c * a[..., None]
        pre = cv2.GaussianBlur(pre, (0, 0), s)
        a = cv2.GaussianBlur(a, (0, 0), s)
        c = pre / np.maximum(a, 1e-4)[..., None]
    sx0, sy0 = max(0, -x0), max(0, -y0)
    sx1, sy1 = (x1 - x0) - max(0, x1 - W), (y1 - y0) - max(0, y1 - H)
    dst = img[y0 + sy0:y0 + sy1, x0 + sx0:x0 + sx1]
    aa = a[sy0:sy1, sx0:sx1, None]
    dst[:] = dst * (1 - aa) + c[sy0:sy1, sx0:sx1] * aa


def glow_field(W, H, blobs, d=4):
    """Sum of soft gaussian glows [(x, y, radius, strength)] at low res."""
    def fn(X, Y):
        g = np.zeros_like(X)
        for bx, by, br, bs in blobs:
            g += F(bs) * np.exp(-((X - bx) ** 2 + (Y - by) ** 2) / F(br * br))
        return g
    return lowres(fn, W, H, d)


def drift(W, H, stops, glow_col, bead_cols, blobs, band, seed, n=130, big=6):
    X, Y = grid(W, H)
    img = ramp(Y + 0.15 * X, stops)
    haze = lowres(lambda a, b: fbm(a * 1.3 + 3, b * 1.6, 4, seed), W, H, 6)
    img *= (0.88 + 0.24 * haze)[..., None]
    g = glow_field(W, H, blobs)
    img = mix(img, hexrgb(glow_col), np.clip(g, 0, 1) * 0.85)
    img = vignette(img, X, Y, 0.3)
    rng = np.random.default_rng(seed)
    asp = W / H
    a0, a1, amp, freq, width = band
    beads_ = []
    for _ in range(n):
        x = rng.uniform(-asp / 2 - 0.05, asp / 2 + 0.05)
        y = a0 + a1 * x + amp * np.sin(freq * x + seed) + rng.normal(0, width)
        r = np.exp(rng.uniform(np.log(0.003), np.log(0.012)))
        beads_.append((x, y, r, rng.choice(bead_cols), rng.uniform(0, 0.0012)))
    for _ in range(big):
        x = rng.uniform(-asp / 2, asp / 2)
        y = rng.uniform(0.1, 0.9)
        beads_.append((x, y, rng.uniform(0.016, 0.03), rng.choice(bead_cols), rng.uniform(0.002, 0.006)))
    beads_.sort(key=lambda b: b[2])
    for x, y, r, c, bl in beads_:
        bead(img, W, H, x, y, r, c, bl, glow_col)
    return img


def teal_drift(W, H):
    return drift(W, H, [(0, '#0b6f7a'), (0.45, '#1aa3a8'), (0.8, '#0e6f8c'), (1.2, '#063a5c')],
                 '#bff6f0', ['#0b4f7a', '#0a6a8a', '#123c6e'],
                 [(0.25, 0.45, 0.35, 0.7), (-0.5, 0.2, 0.3, 0.25)], (0.55, -0.12, 0.06, 2.5, 0.07), 41)


def violet_drift(W, H):
    return drift(W, H, [(0, '#5a12b8'), (0.5, '#7b2fe0'), (0.9, '#6a1fd0'), (1.2, '#3c0a8c')],
                 '#e6c8ff', ['#6a1ad8', '#8a3cf0', '#4a10a8'],
                 [(0.15, 0.75, 0.3, 0.9), (-0.3, 0.35, 0.25, 0.3)], (0.4, 0.15, 0.08, 3.0, 0.08), 46)


def lime_drift(W, H):
    return drift(W, H, [(0, '#2fae1e'), (0.45, '#52d22c'), (0.85, '#2a9f18'), (1.2, '#14700c')],
                 '#e9ffd2', ['#2a9a14', '#1f7f10', '#3fbf22'],
                 [(0.1, 0.5, 0.45, 0.8)], (0.45, 0.05, 0.05, 2.0, 0.09), 49)


def berry_drift(W, H):
    return drift(W, H, [(0, '#c21a5c'), (0.45, '#e2508a'), (0.85, '#c41e60'), (1.2, '#8a0c3c')],
                 '#ffe3f0', ['#c4123a', '#e02a4e', '#a00c30'],
                 [(-0.2, 0.42, 0.42, 0.9), (0.6, 0.7, 0.25, 0.3)], (0.5, -0.18, 0.05, 2.2, 0.1), 54, 110)


def tangerine_drift(W, H):
    return drift(W, H, [(0, '#e8780c'), (0.5, '#f5a030'), (0.9, '#e06a08'), (1.2, '#b04a04')],
                 '#ffe6b8', ['#d4600a', '#c05008', '#e87a14'],
                 [(0.3, 0.6, 0.4, 0.6)], (0.35, 0.1, 0.07, 2.6, 0.11), 56, 90, 4)


# --- perforated bokeh --------------------------------------------------------

def aqua_bokeh(W, H):
    """A tilted perforated plane of light, focused in the middle distance."""
    X, Y = grid(W, H)
    z = 1.0 / (Y * 1.25 + 0.12)            # depth: near at the bottom
    u = (X + 0.15) * z * 2.2
    v = z * 2.2 + 0.3 * X
    fu, fv = u - np.floor(u), v - np.floor(v)
    dist = np.sqrt((fu - 0.5) ** 2 + (fv - 0.5) ** 2)
    focus = np.abs(np.log(z / 2.2))
    soft = 0.02 + 0.22 * focus             # defocus widens the edge
    dot = 1 - smooth(0.33 - soft, 0.33 + soft * 0.4, dist)
    # pixel-footprint antialias for far rows
    fw = np.clip(z * 2.2 / H * 3, 0, 0.5)
    tw = np.clip(fw / 0.25, 0, 1)
    dot = dot + (F(0.33 ** 2 * np.pi) - dot) * tw
    base = ramp(Y, [(0, '#0d5c86'), (0.5, '#1b8fb8'), (1, '#64c8e0')])
    lit = ramp(focus, [(0, '#ffffff'), (0.6, '#cdf4ff'), (1.4, '#7fd6ef')])
    img = mix(base, lit, dot * (0.55 + 0.45 * np.exp(-focus * 1.5)))
    g = glow_field(W, H, [(-0.4, 0.35, 0.5, 0.35)])
    img = mix(img, hexrgb('#ffffff'), np.clip(g, 0, 1) * 0.5)
    return vignette(img, X, Y, 0.25)


# --- pastel daisies ----------------------------------------------------------

def daisy(img, W, H, x, y, R, rot, petals, col, center, alpha):
    cx, cy, Rp = x * H + W / 2, y * H, R * H
    pad = int(Rp + 2)
    x0, y0 = max(0, int(cx) - pad), max(0, int(cy) - pad)
    x1, y1 = min(W, int(cx) + pad + 1), min(H, int(cy) + pad + 1)
    if x1 <= x0 or y1 <= y0:
        return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(F)
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    r = np.sqrt(dx * dx + dy * dy) / Rp
    th = np.arctan2(dy, dx) + rot
    edge = 0.62 + 0.38 * np.abs(np.cos(th * petals / 2)) ** 0.6
    aa = 1.2 / Rp
    m = np.clip((edge - r) / aa, 0, 1)
    ring = np.clip((0.22 - r) / aa, 0, 1)
    c = mix(np.broadcast_to(hexrgb(col), m.shape + (3,)), hexrgb(center), ring)
    shade = 0.85 + 0.15 * np.clip(1 - r / edge, 0, 1)
    c = c * shade[..., None]
    a = (m * F(alpha))[..., None]
    img[y0:y1, x0:x1] = img[y0:y1, x0:x1] * (1 - a) + c * a


def daisy_pop(W, H):
    X, Y = grid(W, H)
    img = ramp(Y - 0.2 * X, [(0, '#fdf3e4'), (0.6, '#f6ead8'), (1.2, '#efe0cc')])
    rng = np.random.default_rng(44)
    asp = W / H
    cols = ['#5aa7e0', '#8cc6f0', '#f08aa0', '#f6b0c0', '#ffffff', '#4a8fd4', '#f7a35c']
    for i in range(70):
        R = np.exp(rng.uniform(np.log(0.05), np.log(0.2)))
        daisy(img, W, H, rng.uniform(-asp / 2 - 0.1, asp / 2 + 0.1), rng.uniform(-0.1, 1.1), R,
              rng.uniform(0, 6.28), int(rng.choice([10, 12, 14, 16, 20])), rng.choice(cols),
              rng.choice(['#ffffff', '#ffe08a', '#f6d0d8']), rng.uniform(0.35, 0.75))
    wash = lowres(lambda a, b: fbm(a * 2, b * 2, 3, 9), W, H, 6)
    img = mix(img, hexrgb('#ffffff'), 0.25 * wash)
    return img


# --- pi desktop --------------------------------------------------------------

def _seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    t = np.clip(((px - ax) * vx + (py - ay) * vy) / np.maximum(vx * vx + vy * vy, F(1e-9)), 0, 1)
    return np.sqrt((px - ax - t * vx) ** 2 + (py - ay - t * vy) ** 2)


def pi_desktop(W, H):
    """Soft periwinkle desktop with a giant outlined pi drifting off the right edge."""
    X, Y = grid(W, H)
    img = ramp(Y + 0.3 * X, [(0, '#c4cdf2'), (0.6, '#d7defa'), (1.3, '#e7ebfd')])
    g = glow_field(W, H, [(-0.5, 0.3, 0.6, 0.35)])
    img = mix(img, hexrgb('#f4f6ff'), np.clip(g, 0, 1))
    s, ox, oy = 1.0, 0.13, 0.06
    px, py = (X - ox) / s, (Y - oy) / s
    # pi built from strokes: curved top bar, a leaning left leg, a right leg with a foot
    t = np.linspace(0, 1, 24)
    bar = [(0.02 + 0.66 * a, 0.2 - 0.05 * np.sin(np.pi * a) - 0.02 * a) for a in t]
    left = [(0.22 - 0.07 * a ** 1.6, 0.19 + 0.6 * a) for a in t]
    right = [(0.5 + 0.02 * a, 0.17 + 0.52 * a) for a in t] + [(0.52 + 0.06 * a, 0.69 + 0.08 * np.sin(np.pi * a * 0.8)) for a in t]
    d = np.full(X.shape, 9.0, F)
    for path in (bar, left, right):
        for (ax, ay), (bx, by) in zip(path[:-1], path[1:]):
            d = np.minimum(d, _seg_dist(px, py, F(ax), F(ay), F(bx), F(by)))
    w, aa = 0.022, 1.5 / (H * s)
    stroke = 1 - smooth(w - aa, w + aa, d)
    halo = np.exp(-(d / 0.07) ** 2) * 0.25
    img = mix(img, hexrgb('#a9b6ec'), stroke * 0.75)
    img = mix(img, hexrgb('#ffffff'), halo * (1 - stroke))
    return vignette(img, X, Y, 0.12)


FRUIT = [  # id, title, year, fn
    ("40", "Aqua Bokeh", 1999, aqua_bokeh),
    ("41", "Teal Drift", 1999, teal_drift),
    ("44", "Daisy Pop", 2000, daisy_pop),
    ("46", "Violet Drift", 1999, violet_drift),
    ("49", "Lime Drift", 1999, lime_drift),
    ("50", "Pi Desktop", 1999, pi_desktop),
    ("54", "Berry Drift", 1999, berry_drift),
    ("56", "Tangerine Drift", 1999, tangerine_drift),
]
