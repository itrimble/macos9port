"""Synthesize the OS Pi interface sound set (original, procedural).

usage: python3 sfx.py OUTDIR
Writes 48 kHz 16-bit stereo WAV files plus 22.05 kHz 8-bit mono copies
in classic/ for period-accurate playback paths. Deterministic output.
"""
import json, os, sys
import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
RNG = np.random.default_rng(2026)


def t_(d):
    return np.arange(int(SR * d)) / SR


def env(n, a=0.005, d=0.2, s=0.0, r=0.1, hold=0.0):
    a_n, d_n, h_n, r_n = (int(SR * x) for x in (a, d, hold, r))
    e = np.concatenate([np.linspace(0, 1, max(1, a_n)), np.linspace(1, s, max(1, d_n)),
                        np.full(h_n, s), np.linspace(s, 0, max(1, r_n))])
    return np.pad(e, (0, max(0, n - len(e))))[:n]


def expd(n, tau):
    return np.exp(-np.arange(n) / (SR * tau))


def sine(f, d, ph=0.0):
    f = np.broadcast_to(f, t_(d).shape)
    return np.sin(2 * np.pi * np.cumsum(f) / SR + ph)


def square(f, d, harmonics=15):
    out = np.zeros(int(SR * d))
    for k in range(1, harmonics * 2, 2):
        if np.max(np.broadcast_to(f, out.shape)) * k < SR / 2.2:
            out += sine(np.asarray(f) * k, d) / k
    return out * 0.8


def bell(f, d, ratio=3.5, index=2.0, tau=0.6):
    n = int(SR * d)
    e = expd(n, tau)
    mod = index * e * sine(f * ratio, d)
    return np.sin(2 * np.pi * f * t_(d) + mod) * e


def noise(d):
    return RNG.standard_normal(int(SR * d))


def bp(x, lo, hi, order=2):
    sos = signal.butter(order, [lo, hi], 'bandpass', fs=SR, output='sos')
    return signal.sosfilt(sos, x)


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, 'lowpass', fs=SR, output='sos'), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, 'highpass', fs=SR, output='sos'), x)


def pluck(f, d, damp=0.996):
    n = int(SR * d); p = int(SR / f)
    buf = RNG.uniform(-1, 1, p); out = np.zeros(n)
    for i in range(n):
        out[i] = buf[i % p]
        buf[i % p] = damp * 0.5 * (buf[i % p] + buf[(i + 1) % p])
    return out


def at(total, *parts):
    """Place (start_seconds, mono) parts on a timeline."""
    out = np.zeros(int(SR * total))
    for st, x in parts:
        i = int(SR * st); j = min(len(out), i + len(x))
        out[i:j] += x[:j - i]
    return out


def reverb(x, size=1.2, mix=0.25, seed=3):
    r = np.random.default_rng(seed)
    n = int(SR * size)
    ir = r.standard_normal((2, n)) * np.exp(-np.arange(n) / (SR * size / 6))
    ir = lp(ir, 7000)
    wet = np.stack([signal.fftconvolve(x, ir[c])[:len(x) + n] for c in range(2)])
    wet = np.pad(wet, ((0, 0), (0, len(x) + n - wet.shape[1])))
    dry = np.stack([np.pad(x, (0, n))] * 2)
    wet /= np.max(np.abs(wet)) + 1e-9
    return dry * (1 - mix) + wet * mix * np.max(np.abs(x))


def stereo(x, pan=0.0):
    l, r = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    return np.stack([x * l * 1.414, x * r * 1.414])


def m2n(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# --- sound designs -------------------------------------------------------

def startup():
    notes = [49, 56, 61, 65, 68, 72]  # D-flat major add9 voicing, original chord
    d = 4.0
    x = sum(bell(m2n(m), d, ratio=2.0, index=1.2, tau=1.6) * (0.9 - i * 0.08)
            for i, m in enumerate(notes))
    pad = sum(sine(m2n(m) * k, d) for m in notes[:3] for k in (1, 1.003))
    x = x + 0.25 * pad * env(len(pad), 0.4, 1.5, 0.4, 2.0)
    shimmer = bell(m2n(85), d, 1.5, 0.6, 0.8) * 0.15
    return reverb(x + at(d, (0.25, shimmer)), 2.5, 0.35)


def shutdown():
    seq = [(0.0, 72), (0.12, 68), (0.24, 65), (0.36, 61)]
    x = at(2.2, *[(s, bell(m2n(m), 1.6, 2.0, 0.8, 0.7)) for s, m in seq])
    x *= np.linspace(1, 0.6, len(x))
    return reverb(lp(x, 5000), 1.8, 0.35)


def sweep(f0, f1, d, tau):
    f = np.geomspace(f0, f1, int(SR * d))
    return sine(f, d) * expd(len(f), tau) * env(len(f), 0.02, d, 1, 0.05)


def sleep():
    return reverb(sweep(880, 220, 0.9, 0.5) + 0.4 * sweep(1320, 330, 0.9, 0.4), 1.4, 0.4)


def wake():
    return reverb(sweep(220, 880, 0.7, 0.6) + 0.4 * sweep(330, 1320, 0.7, 0.5), 1.4, 0.4)


def alert_beep():
    x = square(1046.5, 0.16, 9) * env(int(SR * 0.16), 0.003, 0.02, 0.8, 0.03, 0.1)
    return stereo(lp(x, 6000) * 0.5)


def alert_droplet():
    d = 0.35
    f = 600 * np.exp(np.linspace(0, 1.4, int(SR * d)))
    x = sine(f, d) * expd(len(f), 0.07)
    return reverb(x, 0.6, 0.3)


def alert_glass():
    x = bell(2093, 1.4, 2.76, 1.5, 0.45) + 0.5 * bell(3136, 1.4, 2.76, 1.0, 0.3)
    return reverb(x, 1.2, 0.3)


def alert_boing():
    d = 0.6; tt = t_(d)
    f = 180 + 120 * np.exp(-tt * 6) * np.sin(2 * np.pi * 14 * tt) + 80 * np.exp(-tt * 3)
    return stereo(sine(f, d) * expd(len(tt), 0.25) * 0.9)


def alert_pluck():
    x = at(0.9, (0, pluck(m2n(64), 0.8)), (0.09, pluck(m2n(71), 0.8) * 0.8))
    return reverb(x, 0.8, 0.25)


def alert_marimba():
    def mar(m, d=0.5):
        f = m2n(m)
        return (sine(f, d) + 0.25 * sine(f * 4, d) * expd(int(SR * d), 0.03)) * expd(int(SR * d), 0.18)
    return reverb(at(0.8, (0, mar(72)), (0.11, mar(79))), 0.8, 0.25)


def alert_whistle():
    d = 0.55; tt = t_(d)
    f = 900 + 900 * tt / d + 30 * np.sin(2 * np.pi * 7 * tt)
    x = sine(f, d) * env(len(tt), 0.04, 0.3, 0.7, 0.15, 0.1) + 0.03 * bp(noise(d), 1500, 4000)
    return reverb(x, 0.6, 0.2)


def alert_bloop():
    d = 0.4
    f = np.geomspace(140, 520, int(SR * d))
    return reverb(sine(f, d) * env(len(f), 0.01, 0.35, 0, 0.02), 0.5, 0.2)


def error_buzz():
    d = 0.32
    x = (square(110, d, 20) + square(116.5, d, 20)) * env(int(SR * d), 0.005, 0.05, 0.7, 0.08, 0.18)
    return stereo(lp(x, 2500) * 0.4)


def notification():
    x = at(1.4, (0, bell(m2n(76), 1.1, 2.0, 1.0, 0.5)), (0.14, bell(m2n(83), 1.1, 2.0, 1.0, 0.5)))
    return reverb(x * 0.8, 1.0, 0.3)


def click(f=4000, d=0.02, tau=0.002, body=0.0):
    x = bp(noise(d), f * 0.6, min(f * 1.6, SR / 2.2)) * expd(int(SR * d), tau)
    if body:
        x += body * sine(f / 8, d) * expd(int(SR * d), tau * 2)
    return x


def menu_tick():
    return stereo(click(5000, 0.03, 0.0015) * 0.6)


def button_click():
    return stereo(at(0.08, (0, click(3000, 0.04, 0.002, 0.5)), (0.035, click(2200, 0.04, 0.0025) * 0.6)))


def key_click():
    return stereo(click(2600, 0.05, 0.004, 0.8) * 0.7, 0.1)


def whoosh(d, up=True):
    n = noise(d)
    f = np.geomspace(300, 4000, 8) if up else np.geomspace(4000, 300, 8)
    seg = np.array_split(np.arange(len(n)), 8)
    out = np.zeros_like(n)
    for k, idx in enumerate(seg):
        out[idx] = bp(n, f[k] * 0.7, min(f[k] * 1.4, SR / 2.2))[idx]
    return out * np.sin(np.linspace(0, np.pi, len(n))) ** 2


def window_open():
    x = whoosh(0.28, True) * 0.4 + 0.25 * sweep(500, 1100, 0.28, 0.2)
    return reverb(x, 0.5, 0.2)


def window_close():
    x = whoosh(0.24, False) * 0.4 + 0.25 * sweep(1000, 450, 0.24, 0.15)
    return reverb(x, 0.5, 0.2)


def windowshade_roll():
    ticks = at(0.34, *[(i * 0.03, click(3500 - i * 150, 0.02, 0.0015) * 0.5) for i in range(10)])
    return stereo(ticks + 0.2 * whoosh(0.34, False))


def drag_pickup():
    d = 0.09
    return stereo(sine(np.geomspace(300, 900, int(SR * d)), d) * env(int(SR * d), 0.004, 0.08, 0, 0.005) * 0.7)


def drop_thunk():
    d = 0.2
    x = sine(np.geomspace(160, 60, int(SR * d)), d) * expd(int(SR * d), 0.05)
    return stereo(x + click(1200, d, 0.004) * 0.4)


def rustle(d, density=60, seed=0):
    r = np.random.default_rng(seed)
    x = np.zeros(int(SR * d))
    for _ in range(int(density * d)):
        i = r.integers(0, len(x) - 2000)
        b = bp(r.standard_normal(2000), 1500, 9000) * expd(2000, r.uniform(0.002, 0.008))
        x[i:i + 2000] += b * r.uniform(0.3, 1)
    return x


def trash_drop():
    return reverb(at(0.35, (0, rustle(0.35, 80, 5) * 0.6), (0, 0.4 * drop_thunk()[0])), 0.5, 0.2)


def trash_empty():
    x = rustle(0.9, 140, 9) * np.linspace(1, 0.2, int(SR * 0.9)) * 0.6
    return reverb(x + 0.5 * at(0.9, (0.4, whoosh(0.5, False))), 0.8, 0.25)


def folder_open():
    pop = sine(np.geomspace(500, 1200, int(SR * 0.05)), 0.05) * expd(int(SR * 0.05), 0.012)
    return reverb(at(0.4, (0, pop), (0.04, bell(m2n(84), 0.35, 1.0, 0.5, 0.12) * 0.4)), 0.5, 0.2)


def copy_complete():
    x = at(1.0, *[(i * 0.08, bell(m2n(m), 0.7, 1.0, 0.6, 0.25)) for i, m in enumerate((72, 76, 79, 84))])
    return reverb(x * 0.6, 1.0, 0.3)


def download_complete():
    x = at(1.3, *[(i * 0.13, bell(m2n(m), 1.0, 3.0, 1.2, 0.4)) for i, m in enumerate((79, 74, 86))])
    return reverb(x * 0.6, 1.2, 0.3)


def motor(d, f0, f1):
    f = np.linspace(f0, f1, int(SR * d))
    return (square(f, d, 6) * 0.2 + 0.3 * bp(noise(d), 200, 1200)) * np.sin(np.linspace(0, np.pi, len(f)))


def disk_insert():
    clunk = sine(np.geomspace(220, 80, int(SR * 0.12)), 0.12) * expd(int(SR * 0.12), 0.03)
    return stereo(at(1.0, (0, click(1500, 0.05, 0.005, 1.0)), (0.1, clunk), (0.25, motor(0.7, 60, 90) * 0.5)))


def disk_eject():
    clunk = sine(np.geomspace(260, 90, int(SR * 0.12)), 0.12) * expd(int(SR * 0.12), 0.03)
    return stereo(at(0.9, (0, motor(0.55, 90, 50) * 0.5), (0.55, clunk), (0.6, click(1800, 0.05, 0.005, 1.0))))


def screenshot_shutter():
    x = at(0.25, (0, click(2500, 0.06, 0.006, 0.6) + 0.3 * bp(noise(0.06), 3000, 9000) * expd(int(SR * 0.06), 0.01)),
           (0.09, click(1800, 0.07, 0.008, 0.8)))
    return reverb(x, 0.3, 0.15)


SOUNDS = [
    ("startup_chime", "system", startup), ("shutdown", "system", shutdown),
    ("sleep", "system", sleep), ("wake", "system", wake),
    ("alert_beep", "alert", alert_beep), ("alert_droplet", "alert", alert_droplet),
    ("alert_glass", "alert", alert_glass), ("alert_boing", "alert", alert_boing),
    ("alert_pluck", "alert", alert_pluck), ("alert_marimba", "alert", alert_marimba),
    ("alert_whistle", "alert", alert_whistle), ("alert_bloop", "alert", alert_bloop),
    ("error_buzz", "alert", error_buzz), ("notification", "alert", notification),
    ("menu_tick", "ui", menu_tick), ("button_click", "ui", button_click),
    ("key_click", "ui", key_click), ("window_open", "ui", window_open),
    ("window_close", "ui", window_close), ("windowshade_roll", "ui", windowshade_roll),
    ("drag_pickup", "ui", drag_pickup), ("drop_thunk", "ui", drop_thunk),
    ("trash_drop", "finder", trash_drop), ("trash_empty", "finder", trash_empty),
    ("folder_open", "finder", folder_open), ("copy_complete", "finder", copy_complete),
    ("download_complete", "finder", download_complete), ("disk_insert", "finder", disk_insert),
    ("disk_eject", "finder", disk_eject), ("screenshot_shutter", "finder", screenshot_shutter),
]


def finish(x):
    if x.ndim == 1:
        x = stereo(x)
    x = x - x.mean(axis=1, keepdims=True)
    fade = min(int(SR * 0.01), x.shape[1] // 4)
    x[:, -fade:] *= np.linspace(1, 0, fade)
    nz = np.where(np.max(np.abs(x), 0) > 1e-4)[0]
    x = x[:, : nz[-1] + 1 + int(SR * 0.02)] if len(nz) else x
    peak = np.max(np.abs(x)) + 1e-9
    rms = np.sqrt(np.mean(x ** 2)) + 1e-9
    return x * min(0.89 / peak, 0.16 / rms)  # level-matched, -1 dBFS ceiling


def main():
    out = sys.argv[1]
    os.makedirs(f"{out}/classic", exist_ok=True)
    meta = []
    for i, (name, cat, fn) in enumerate(SOUNDS, 1):
        x = finish(fn())
        fname = f"{i:02d}-{name}.wav"
        wavfile.write(f"{out}/{fname}", SR, (x.T * 32767).astype(np.int16))
        mono = signal.resample_poly(x.mean(0), 147, 320)
        u8 = np.clip(mono / (np.max(np.abs(mono)) + 1e-9) * 127 + 128, 0, 255).astype(np.uint8)
        wavfile.write(f"{out}/classic/{fname}", 22050, u8)
        meta.append({"id": f"{i:02d}", "name": name, "category": cat, "file": fname,
                     "seconds": round(x.shape[1] / SR, 3)})
        print(fname, meta[-1]["seconds"], flush=True)
    with open(f"{out}/manifest.json", "w") as f:
        json.dump({"sample_rate": SR, "format": "PCM16 stereo",
                   "classic": "classic/: 22050 Hz 8-bit unsigned mono", "sounds": meta}, f, indent=2)


if __name__ == "__main__":
    main()
