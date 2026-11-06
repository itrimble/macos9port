#!/usr/bin/env python3
"""Summarize OSPI_AUDIO_TRACE / OSPI_PERF_TRACE lines from an ospi-runtime log.

Usage: python3 -I scripts/audio-trace-summary.py runtime.log [--from S] [--to S]

Prints per-interval producer/callback rates, underrun frames, ring minimum,
guest MHz and presenter FPS, then totals. Times are host ms since the runtime's
SDL clock started, which is close to the bridge probe's wall clock.
"""
import argparse
import re
import sys


def fields(line):
    return {k: v for k, v in re.findall(r"([a-z-]+)=([-0-9.]+)", line)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log")
    p.add_argument("--from", dest="start", type=float, default=0.0)
    p.add_argument("--to", dest="end", type=float, default=1e9)
    args = p.parse_args()
    audio, guest, present = [], [], []
    for line in open(args.log, errors="replace"):
        if line.startswith("audio-trace "):
            audio.append(fields(line))
        elif line.startswith("guest-trace "):
            guest.append(fields(line))
        elif line.startswith("perf-trace ") or line.startswith("present-trace "):
            present.append(fields(line))
    if not audio:
        print("no audio-trace lines; was OSPI_AUDIO_TRACE=1 set?")
        return 1
    base = float(audio[0]["ms"])
    print(f"{'t(s)':>6} {'prod/s':>8} {'cb/s':>8} {'underrun':>9} {'ringmin':>8} {'clip':>5} {'MHz':>6}")
    total_underrun = 0
    worst = []
    mhz_by_ms = {g["ms"]: g.get("guest-MHz", "?") for g in guest}
    for a in audio:
        t = (float(a["ms"]) - base) / 1000.0
        if t < args.start or t > args.end:
            continue
        underrun = int(a.get("underrun-frames", 0))
        total_underrun += underrun
        mhz = "?"
        for g in guest:
            if abs(float(g["ms"]) - float(a["ms"])) < 600:
                mhz = g.get("guest-MHz", "?")
                break
        print(f"{t:6.1f} {float(a.get('producer-fps', 0)):8.0f} {float(a.get('callback-fps', 0)):8.0f} {underrun:9d} {a.get('ring-min-frames', '?'):>8} {a.get('clip-frames', '?'):>5} {mhz:>6}")
        if underrun:
            worst.append((t, underrun))
    print(f"intervals={len(audio)} total-underrun-frames={total_underrun} ({total_underrun / 48000.0:.3f} s) intervals-with-underrun={len(worst)}")
    for t, u in sorted(worst, key=lambda w: -w[1])[:10]:
        print(f"  worst: t={t:.1f}s underrun={u} frames ({u / 48.0:.0f} ms)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
