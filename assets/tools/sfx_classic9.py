"""Synthesize the OS Pi "Classic 9" sound pack (original, procedural).

usage: python3 sfx_classic9.py OUTDIR
Original synthesized sounds in the spirit of late-1990s desktop alert and
Platinum interface sounds. Nothing is sampled or transcribed from any
existing recording. Reuses the helpers and level matching from sfx.py.
"""
import json, os, sys
import numpy as np
from scipy import signal
from scipy.io import wavfile
from sfx import (SR, RNG, t_, env, expd, sine, square, bell, noise, bp, lp, hp,
                 at, reverb, stereo, m2n, finish, click, whoosh, sweep)


def saw(f, d, harmonics=24):
    out = np.zeros(int(SR * d))
    for k in range(1, harmonics + 1):
        if f * k < SR / 2.2:
            out += sine(f * k, d) / k
    return out * 0.6


def formant(src, freqs, bws, gains):
    out = np.zeros_like(src)
    for f, bw, g in zip(freqs, bws, gains):
        out += g * bp(src, max(40, f - bw / 2), f + bw / 2, 2)
    return out


def glottal(f, d):
    """Band-rich pulse train with a time-varying pitch array or scalar."""
    f = np.broadcast_to(np.asarray(f, float), t_(d).shape)
    ph = np.cumsum(f) / SR
    return (2 * (ph % 1.0) - 1) ** 3


# --- system --------------------------------------------------------------

def startup_chord():
    # Big open major chord with an organ-like additive tone and slow bloom.
    d = 4.5
    notes = [41, 48, 53, 57, 60, 65]
    x = np.zeros(int(SR * d))
    for i, m in enumerate(notes):
        f = m2n(m)
        v = saw(f, d, 14) + 0.6 * saw(f * 1.004, d, 14) + 0.3 * sine(f * 2, d)
        x += v * (1.0 - i * 0.07)
    x = lp(x, 3200, 2) * env(len(x), 0.02, 0.6, 0.55, 3.2, 0.4)
    return reverb(x, 2.4, 0.38)


def restart_tone():
    seq = [(0.0, 65), (0.1, 72), (0.2, 77)]
    x = at(1.6, *[(s, bell(m2n(m), 1.3, 2.0, 0.9, 0.45)) for s, m in seq])
    return reverb(x, 1.2, 0.3)


# --- alerts ---------------------------------------------------------------

def plain_beep():
    # Short two-stage synth beep: bright attack falling to a softer tone.
    d = 0.32
    f = np.where(t_(d) < 0.05, 1320, 990)
    x = square(f, d, 7) * env(int(SR * d), 0.002, 0.08, 0.5, 0.12, 0.1)
    return stereo(lp(x, 5000) * 0.5)


def tin_chime():
    # Two quick metallic strikes, a fifth apart, hard attack, short ring.
    def hit(f):
        d = 0.7
        n = int(SR * d)
        partials = [(1, 1.0, 0.25), (2.76, 0.5, 0.12), (5.4, 0.3, 0.06), (8.9, 0.15, 0.03)]
        return sum(g * sine(f * r, d) * expd(n, tau) for r, g, tau in partials)
    x = at(0.9, (0, hit(m2n(84))), (0.09, hit(m2n(77)) * 0.85))
    return reverb(x, 0.7, 0.2)


def eep():
    # Squeaky up-and-over squeal, like a small startled creature.
    d = 0.42
    tt = t_(d)
    f = 1700 + 1400 * np.sin(np.pi * np.clip(tt / 0.3, 0, 1)) + 90 * np.sin(2 * np.pi * 28 * tt)
    x = (sine(f, d) + 0.35 * sine(f * 2, d)) * env(len(tt), 0.01, 0.25, 0.4, 0.12, 0.02)
    return reverb(x * 0.7, 0.5, 0.15)


def quack():
    d = 0.3
    tt = t_(d)
    f0 = 290 * np.exp(-tt * 1.6)
    src = glottal(f0, d) + 0.05 * noise(d)
    x = formant(src, [950, 1500, 2700], [260, 300, 500], [1.0, 0.7, 0.3])
    x *= env(len(tt), 0.008, 0.08, 0.7, 0.12, 0.08)
    return reverb(lp(x, 4000), 0.4, 0.12)


def droplet():
    # Rising resonant "plip" with a tiny splash tail.
    d = 0.5
    n = int(SR * d)
    f = 520 * np.exp(np.linspace(0, 1.9, n))
    x = sine(f, d) * expd(n, 0.06)
    tail = bp(noise(d), 3000, 8000) * expd(n, 0.015) * 0.05
    x += at(d, (0.03, tail))
    return reverb(x, 0.9, 0.35)


def indigo():
    # Shimmering rising arpeggio of soft synth bells.
    seq = [(0.0, 72), (0.06, 76), (0.12, 79), (0.18, 84), (0.24, 88)]
    x = at(1.8, *[(s, bell(m2n(m), 1.4, 3.0, 1.4, 0.5) * (0.7 + i * 0.07))
                  for i, (s, m) in enumerate(seq)])
    return reverb(x, 1.6, 0.4)


def temple():
    # Struck singing bowl: low inharmonic partials with slow beating.
    d = 3.2
    n = int(SR * d)
    f = m2n(57)
    parts = [(1.0, 1.0, 1.4), (2.71, 0.5, 0.9), (5.12, 0.25, 0.5), (8.3, 0.12, 0.3)]
    x = sum(g * (sine(f * r, d) + sine(f * r * 1.003, d)) * expd(n, tau) for r, g, tau in parts)
    x += 0.2 * bp(noise(d), 200, 2000) * expd(n, 0.01)
    return reverb(x * 0.5, 2.5, 0.35)


def uh_oh():
    # Two-syllable synthesized vowel with a falling second note.
    def syl(f0, f1, d, vowel):
        tt = t_(d)
        f = np.linspace(f0, f1, len(tt))
        x = formant(glottal(f, d), *vowel)
        return x * env(len(tt), 0.02, 0.05, 0.85, 0.07, d - 0.14)
    uh = ([640, 1190, 2390], [90, 110, 160], [1.0, 0.5, 0.25])
    oh = ([500, 850, 2400], [90, 100, 160], [1.0, 0.6, 0.2])
    x = at(0.7, (0, syl(250, 240, 0.2, uh)), (0.24, syl(205, 170, 0.36, oh)))
    return reverb(lp(x, 3500), 0.5, 0.15)


def voltage():
    d = 0.55
    n = int(SR * d)
    f = np.geomspace(1400, 90, n)
    buzz = square(f, d, 11) * (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 60 * t_(d))))
    crackle = bp(noise(d), 2000, 9000) * (RNG.random(n) > 0.92)
    x = (0.5 * buzz + 0.4 * crackle) * expd(n, 0.18)
    return stereo(lp(x, 7000) * 0.6)


def whit():
    # Quick bright flute chirp.
    d = 0.18
    tt = t_(d)
    f = 1900 + 2400 * (tt / d) ** 0.6
    x = sine(f, d) * env(len(tt), 0.004, 0.12, 0.2, 0.04)
    x += 0.04 * bp(noise(d), 3000, 7000) * expd(len(tt), 0.02)
    return reverb(x, 0.4, 0.2)


def pong():
    def blip(f, d=0.09):
        return square(f, d, 5) * env(int(SR * d), 0.004, 0.01, 1.0, 0.015, d - 0.035)
    return stereo(at(0.3, (0, blip(m2n(76))), (0.12, blip(m2n(69)))) * 0.45)


def squeak_toy():
    def sq(up, d=0.22):
        tt = t_(d)
        f = (1100 + 900 * np.sin(np.pi * tt / d)) * (1.0 if up else 0.85)
        src = glottal(f, d)
        x = formant(src, [2200, 3400], [600, 800], [1.0, 0.6])
        return x * env(len(tt), 0.01, 0.08, 0.8, 0.06, d - 0.15)
    return reverb(at(0.6, (0, sq(True)), (0.26, sq(False) * 0.9)), 0.4, 0.15)


def sad_trombone_lite():
    seq = [(0.0, 55, 0.22), (0.24, 54, 0.22), (0.48, 53, 0.5)]
    parts = []
    for s, m, d in seq:
        tt = t_(d)
        f = m2n(m) * (1 + 0.012 * np.sin(2 * np.pi * 5.5 * tt) * (tt > 0.15))
        brass = formant(glottal(f, d), [600, 1100, 2400], [250, 300, 500], [1.0, 0.8, 0.3])
        parts.append((s, lp(brass, 2400) *
                      env(len(tt), 0.03, 0.05, 0.85, 0.08, d - 0.16)))
    return reverb(at(1.1, *parts) * 0.6, 0.8, 0.2)


# --- Platinum-style interface ----------------------------------------------

def soft_swish(d, f0, f1, gain=0.3):
    n = noise(d)
    f = np.geomspace(f0, f1, 6)
    seg = np.array_split(np.arange(len(n)), 6)
    out = np.zeros_like(n)
    for k, idx in enumerate(seg):
        out[idx] = bp(n, f[k] * 0.7, min(f[k] * 1.5, SR / 2.2))[idx]
    return out * np.sin(np.linspace(0, np.pi, len(n))) ** 3 * gain


def menu_open():
    return stereo(at(0.12, (0, click(4200, 0.02, 0.0012) * 0.5), (0.005, soft_swish(0.1, 1500, 5000))))


def menu_select():
    x = at(0.12, (0, click(5500, 0.015, 0.001) * 0.5), (0.035, click(5500, 0.015, 0.001) * 0.35))
    return stereo(x + 0.08 * at(0.12, (0, sine(2800, 0.06) * expd(int(SR * 0.06), 0.015))))


def menu_close():
    return stereo(soft_swish(0.09, 5000, 1800, 0.25))


def window_open_zoom():
    x = soft_swish(0.3, 500, 6000, 0.35) + 0.15 * sweep(700, 1400, 0.3, 0.25)
    return reverb(x, 0.4, 0.15)


def window_close_zoom():
    x = soft_swish(0.24, 6000, 500, 0.35) + 0.15 * sweep(1300, 650, 0.24, 0.2)
    return reverb(x, 0.4, 0.15)


def collapse_up():
    x = soft_swish(0.18, 900, 4500, 0.35)
    return stereo(at(0.25, (0, x), (0.17, click(3800, 0.03, 0.002) * 0.6)))


def expand_down():
    x = soft_swish(0.18, 4500, 900, 0.35)
    return stereo(at(0.25, (0, x), (0.17, click(2600, 0.03, 0.0025, 0.4) * 0.6)))


def zoom_box():
    ticks = at(0.22, *[(i * 0.03, click(2500 + i * 400, 0.015, 0.001) * 0.35) for i in range(6)])
    return stereo(ticks + soft_swish(0.22, 1200, 4000, 0.15))


def drag_begin():
    return stereo(at(0.1, (0, click(1800, 0.05, 0.004, 0.5) * 0.6),
                     (0.01, soft_swish(0.08, 2000, 3000, 0.12))))


def drag_end():
    return stereo(click(1200, 0.07, 0.006, 0.9) * 0.7)


def button_press():
    return stereo(click(2400, 0.03, 0.0018, 0.6) * 0.7)


def button_release():
    return stereo(click(3400, 0.025, 0.0014, 0.3) * 0.5)


def checkbox():
    return stereo(at(0.06, (0, click(4500, 0.015, 0.001) * 0.5), (0.02, click(3200, 0.02, 0.0015) * 0.5)))


def scroll_tick():
    return stereo(click(6000, 0.012, 0.0008) * 0.45)


def resize_grow():
    ticks = at(0.3, *[(i * 0.025, click(2000 + (i % 3) * 300, 0.012, 0.001) * 0.25) for i in range(12)])
    return stereo(ticks)


def spring_open():
    d = 0.3
    tt = t_(d)
    f = 400 + 500 * (1 - np.exp(-tt * 18)) + 120 * np.exp(-tt * 10) * np.sin(2 * np.pi * 22 * tt)
    x = at(d, (0, sine(f, d) * expd(len(tt), 0.08) * 0.7), (0, 0.2 * click(3000, 0.02, 0.0015)))
    return reverb(x, 0.4, 0.15)


def trash_crumple():
    d = 0.9
    out = np.zeros(int(SR * d))
    for i in range(70):
        s = RNG.uniform(0, 0.75) ** 1.3
        g = RNG.uniform(0.2, 1.0) * (1 - s / 0.9)
        out += at(d, (s, click(RNG.uniform(1500, 7000), 0.02, RNG.uniform(0.0008, 0.003)) * g))
    clank = bell(m2n(70), 0.6, 2.4, 1.8, 0.12) * 0.35
    return reverb(at(1.1, (0, out * 0.5), (0.78, clank)), 0.6, 0.2)


def lid_clank():
    d = 0.45
    x = bell(m2n(67), d, 2.37, 2.5, 0.1) + 0.6 * bell(m2n(74), d, 3.1, 2.0, 0.07)
    x = at(d, (0, x * 0.5), (0, 0.3 * click(2500, 0.03, 0.002, 0.8)))
    return reverb(x, 0.5, 0.2)


def alert_appear():
    x = at(0.5, (0, soft_swish(0.08, 1200, 3500, 0.2)), (0.05, bell(m2n(81), 0.4, 2.0, 0.6, 0.12) * 0.5))
    return reverb(x, 0.5, 0.2)


SOUNDS = [
    ("startup_chord", "system", startup_chord, "big warm startup chord"),
    ("restart_tone", "system", restart_tone, "short rising restart bells"),
    ("plain_beep", "alert", plain_beep, "the plain everyday beep"),
    ("tin_chime", "alert", tin_chime, "two quick metallic strikes"),
    ("eep", "alert", eep, "squeaky startled squeal"),
    ("quack", "alert", quack, "synthesized duck"),
    ("droplet", "alert", droplet, "water drop plip"),
    ("indigo", "alert", indigo, "shimmering bell arpeggio"),
    ("temple", "alert", temple, "struck singing bowl"),
    ("uh_oh", "alert", uh_oh, "two-syllable vocal oops"),
    ("voltage", "alert", voltage, "electric zap"),
    ("whit", "alert", whit, "quick flute chirp"),
    ("pong", "alert", pong, "8-bit paddle blips"),
    ("squeak_toy", "alert", squeak_toy, "rubber toy squeeze"),
    ("wah_wah", "alert", sad_trombone_lite, "short sad brass fall"),
    ("menu_open", "platinum", menu_open, "menu drops down"),
    ("menu_select", "platinum", menu_select, "menu item chosen (flash)"),
    ("menu_close", "platinum", menu_close, "menu dismissed"),
    ("window_open_zoom", "platinum", window_open_zoom, "window zooms open"),
    ("window_close_zoom", "platinum", window_close_zoom, "window zooms closed"),
    ("collapse_up", "platinum", collapse_up, "window shade rolls up"),
    ("expand_down", "platinum", expand_down, "window shade rolls down"),
    ("zoom_box", "platinum", zoom_box, "zoom box toggled"),
    ("drag_begin", "platinum", drag_begin, "drag starts"),
    ("drag_end", "platinum", drag_end, "drag lands"),
    ("button_press", "platinum", button_press, "push button down"),
    ("button_release", "platinum", button_release, "push button up"),
    ("checkbox", "platinum", checkbox, "checkbox or radio toggled"),
    ("scroll_tick", "platinum", scroll_tick, "scroll arrow step"),
    ("resize_grow", "platinum", resize_grow, "grow box drag"),
    ("spring_open", "finder", spring_open, "spring-loaded folder pops open"),
    ("trash_crumple", "finder", trash_crumple, "Empty Trash"),
    ("lid_clank", "finder", lid_clank, "item dropped on Trash"),
    ("alert_appear", "finder", alert_appear, "alert window appears"),
]


def main():
    out = sys.argv[1]
    os.makedirs(f"{out}/classic", exist_ok=True)
    meta = []
    for i, (name, cat, fn, desc) in enumerate(SOUNDS, 1):
        x = finish(fn())
        fname = f"{i:02d}-{name}.wav"
        wavfile.write(f"{out}/{fname}", SR, (x.T * 32767).astype(np.int16))
        mono = signal.resample_poly(x.mean(0), 147, 320)
        u8 = np.clip(mono / (np.max(np.abs(mono)) + 1e-9) * 127 + 128, 0, 255).astype(np.uint8)
        wavfile.write(f"{out}/classic/{fname}", 22050, u8)
        meta.append({"id": f"{i:02d}", "name": name, "category": cat, "file": fname,
                     "use": desc, "seconds": round(x.shape[1] / SR, 3)})
        print(fname, meta[-1]["seconds"], flush=True)
    with open(f"{out}/manifest.json", "w") as f:
        json.dump({"pack": "Classic 9", "sample_rate": SR, "format": "PCM16 stereo",
                   "classic": "classic/: 22050 Hz 8-bit unsigned mono",
                   "note": "Original synthesis; no sampled or transcribed audio.",
                   "sounds": meta}, f, indent=2)


if __name__ == "__main__":
    main()
