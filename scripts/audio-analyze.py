#!/usr/bin/env python3
"""Describe a probe WAV: level timeline, silent gaps, clicks, and a spectrogram.

Usage: python3 -I scripts/audio-analyze.py capture.wav [--png out.png] [--from S --to S]

A click is a sample-to-sample step far larger than the surrounding signal.
A gap is a run of near-silence inside an otherwise active stretch. Both are
printed with timestamps so a listening report can be matched to the PCM.
"""
import argparse
import struct
import sys
import wave

import numpy as np


def load(path):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        channels = w.getnchannels()
        width = w.getsampwidth()
        frames = w.readframes(w.getnframes())
    if width != 2:
        raise SystemExit("expected 16-bit PCM")
    data = np.frombuffer(frames, dtype="<i2").astype(np.float32)
    if channels > 1:
        data = data.reshape(-1, channels).mean(axis=1)
    return rate, data


def timeline(rate, x, window=0.1):
    n = int(rate * window)
    count = len(x) // n
    blocks = x[: count * n].reshape(count, n)
    rms = np.sqrt((blocks ** 2).mean(axis=1))
    peak = np.abs(blocks).max(axis=1)
    return rms, peak, window


def find_gaps(rate, x, rms, window, floor=40.0, min_ms=4.0):
    """Runs of |x| < floor longer than min_ms while the 100 ms RMS around them is active."""
    quiet = np.abs(x) < floor
    gaps = []
    start = None
    for i, q in enumerate(quiet):
        if q and start is None:
            start = i
        elif not q and start is not None:
            length_ms = (i - start) * 1000.0 / rate
            if length_ms >= min_ms:
                block = min(start // int(rate * window), len(rms) - 1)
                before = rms[max(0, block - 2): block + 1].max()
                after = rms[block: min(len(rms), block + 3)].max()
                if before > 300 and after > 300:
                    gaps.append((start / rate, length_ms))
            start = None
    return gaps


def find_clicks(rate, x, ratio=6.0, absolute=6000.0):
    """Steps that exceed both an absolute size and a multiple of the local step size."""
    d = np.abs(np.diff(x))
    n = int(rate * 0.02)
    count = len(d) // n
    local = d[: count * n].reshape(count, n).mean(axis=1)
    clicks = []
    for i in range(count):
        block = d[i * n:(i + 1) * n]
        j = int(block.argmax())
        step = block[j]
        if step > absolute and step > ratio * max(local[i], 1.0):
            clicks.append(((i * n + j) / rate, float(step), float(local[i])))
    return clicks


def spectrogram(rate, x, path, seconds_from, seconds_to):
    from PIL import Image
    a = int(seconds_from * rate)
    b = int(seconds_to * rate) if seconds_to else len(x)
    seg = x[a:b]
    n = 1024
    hop = 256
    count = max(1, (len(seg) - n) // hop)
    win = np.hanning(n)
    rows = []
    for i in range(count):
        frame = seg[i * hop: i * hop + n] * win
        mag = np.abs(np.fft.rfft(frame))[: n // 4]
        rows.append(20 * np.log10(mag + 1.0))
    s = np.array(rows).T[::-1]
    s = (s - s.min()) / max(1e-6, s.max() - s.min())
    img = Image.fromarray((s * 255).astype(np.uint8))
    img = img.resize((min(4096, img.width), 256))
    img.save(path)
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("wav")
    p.add_argument("--png")
    p.add_argument("--from", dest="start", type=float, default=0.0)
    p.add_argument("--to", dest="end", type=float, default=0.0)
    args = p.parse_args()
    rate, x = load(args.wav)
    print(f"rate={rate} seconds={len(x) / rate:.2f} peak={np.abs(x).max():.0f} rms={np.sqrt((x ** 2).mean()):.0f}")
    rms, peak, window = timeline(rate, x)
    active = rms > 300
    if active.any():
        first = int(np.argmax(active)) * window
        last = (len(active) - int(np.argmax(active[::-1]))) * window
        print(f"active from {first:.1f}s to {last:.1f}s ({active.sum() * window:.1f}s active of {len(active) * window:.1f}s)")
    else:
        print("no active audio (all blocks below RMS 300)")
    step = max(1, int(1.0 / window))
    print("per-second rms:", " ".join(f"{rms[i:i + step].mean():.0f}" for i in range(0, len(rms), step)))
    gaps = find_gaps(rate, x, rms, window)
    print(f"gaps>=4ms inside active audio: {len(gaps)}")
    for t, ms in gaps[:40]:
        print(f"  gap at {t:8.3f}s length {ms:6.1f} ms")
    clicks = find_clicks(rate, x)
    print(f"clicks: {len(clicks)}")
    for t, s, l in clicks[:60]:
        print(f"  click at {t:8.3f}s step {s:6.0f} local-mean-step {l:5.0f}")
    if len(clicks) > 1:
        gaps_between = np.diff([c[0] for c in clicks])
        print(f"click spacing median {np.median(gaps_between) * 1000:.1f} ms, min {gaps_between.min() * 1000:.1f} ms")
    if args.png:
        print("spectrogram:", spectrogram(rate, x, args.png, args.start, args.end))


if __name__ == "__main__":
    main()
