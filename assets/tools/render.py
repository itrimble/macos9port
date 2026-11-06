"""Render the OS Pi wallpaper collection.

usage: python3 render.py OUTDIR [--preview] [--only ID]
Outputs, per wallpaper:
  8k/  7680x4320 master (16:9)      4k/  3840x2160 (from master)
  1080p/ 1920x1080 (from master)    16x10/ 2560x1600 native render
  4x3/ 1024x768 native render (OS Pi window size)
Pixel-pattern wallpapers are PNG and also ship a 1x tile in tiles/.
"""
import json, os, sys, time
import numpy as np
import cv2
from PIL import Image
from core import to_u8
import patterns, walls_a as A, walls_b as B

C = [  # id, title, era, year, kind, fn-or-pattern
    ("01", "Halftone Grey", "1-bit desktop", 1984, "pattern", "halftone"),
    ("02", "Basket Weave", "1-bit desktop", 1986, "pattern", "basket"),
    ("03", "Tidal Tile", "early color", 1988, "pattern", "tidal"),
    ("04", "Periwinkle Speckle", "System 7 color", 1991, "pattern", "periwinkle"),
    ("05", "Platinum Ripple", "Platinum", 1994, "pattern", "ripple"),
    ("06", "Lavender Bubbles", "Platinum", 1997, "pattern", "bubbles"),
    ("07", "Translucent Teal", "translucent hardware", 1998, "art", A.translucent),
    ("08", "Five Flavors", "translucent hardware", 1999, "art", A.five_flavors),
    ("09", "Graphite", "translucent hardware", 2000, "art", A.graphite),
    ("10", "Aqua Swoosh", "early Aqua", 2001, "art", A.aqua_swoosh),
    ("11", "Aqua Pinstripe", "early Aqua", 2002, "art", A.aqua_pinstripe),
    ("12", "Brushed Metal", "brushed metal", 2003, "art", A.brushed_metal),
    ("13", "Aurora Blue", "big-cat Aqua", 2005, "art", A.aurora_blue),
    ("14", "Nebula Violet", "space era", 2007, "art", A.nebula_violet),
    ("15", "Snow Aurora", "space era", 2009, "art", A.snow_aurora),
    ("16", "Spiral Galaxy", "space era", 2011, "art", A.spiral_galaxy),
    ("17", "Big Wave", "California coast", 2013, "art", B.big_wave),
    ("18", "Granite Peaks", "California parks", 2014, "art", B.granite_peaks),
    ("19", "Night Cliff", "California parks", 2015, "art", B.night_cliff),
    ("20", "Blue Ridges", "California parks", 2016, "art", B.blue_ridges),
    ("21", "Alpine Lake", "California parks", 2017, "art", B.alpine_lake),
    ("22", "Dunes Day", "desert", 2018, "art", B.dunes_day),
    ("23", "Dunes Night", "desert", 2018, "art", B.dunes_night),
    ("24", "Island Cliff", "California coast", 2019, "art", B.island_cliff),
    ("25", "Coastal Gradient", "abstract gradient", 2020, "art", B.coastal_gradient),
    ("26", "Silk Folds", "abstract gradient", 2021, "art", B.silk_folds),
    ("27", "Bloom", "abstract gradient", 2022, "art", B.bloom),
    ("28", "Vineyard Hills", "abstract landscape", 2023, "art", B.vineyard_hills),
    ("29", "Glow Waves", "abstract gradient", 2024, "art", B.glow_waves),
    ("30", "Liquid Glass", "glass material", 2025, "art", B.liquid_glass),
]


def slug(e):
    return f"{e[0]}-{e[3]}-{e[1].lower().replace(' ', '-')}"


def render(e, W, H):
    if e[4] == "pattern":
        p = patterns.render_pattern(e[5], W, H)
        return np.clip(p * 255 + 0.5, 0, 255).astype(np.uint8)
    return to_u8(e[5](W, H), seed=int(e[0]))


def save(arr, path, png):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im = Image.fromarray(arr)
    if png:
        im.save(path, optimize=True)
    else:
        im.save(path, quality=93, subsampling=0, optimize=True)


def main():
    out = sys.argv[1]
    preview = "--preview" in sys.argv
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    manifest = []
    for e in C:
        if only and e[0] not in only:
            continue
        t = time.time()
        png = e[4] == "pattern"
        ext = "png" if png else "jpg"
        name = slug(e)
        if preview:
            save(render(e, 960, 540), f"{out}/preview/{name}.{ext}", png)
        else:
            m = render(e, 7680, 4320)
            save(m, f"{out}/8k/{name}.{ext}", png)
            for d, (w, h) in (("4k", (3840, 2160)), ("1080p", (1920, 1080))):
                interp = cv2.INTER_NEAREST if png else cv2.INTER_AREA
                small = patterns_or_resize(e, m, w, h, interp)
                save(small, f"{out}/{d}/{name}.{ext}", png)
            del m
            save(render(e, 2560, 1600), f"{out}/16x10/{name}.{ext}", png)
            save(render(e, 1024, 768), f"{out}/4x3/{name}.{ext}", png)
            if png:
                tile = np.clip(patterns.tile_image(e[5]) * 255 + 0.5, 0, 255).astype(np.uint8)
                save(tile, f"{out}/tiles/{name}.png", True)
        manifest.append({"id": e[0], "title": e[1], "era": e[2], "year": e[3],
                         "kind": e[4], "file": f"{name}.{ext}"})
        print(f"{name}: {time.time() - t:.1f}s", flush=True)
    if not only:
        with open(f"{out}/manifest.json", "w") as f:
            json.dump({"variants": {"8k": [7680, 4320], "4k": [3840, 2160],
                                    "1080p": [1920, 1080], "16x10": [2560, 1600],
                                    "4x3": [1024, 768]},
                       "tiles": "pattern wallpapers: 1x tile, scale by round(H/384)",
                       "wallpapers": manifest}, f, indent=2)


def patterns_or_resize(e, m, w, h, interp):
    if e[4] == "pattern":
        return render(e, w, h)  # exact integer pixel scaling per size
    return cv2.resize(m, (w, h), interpolation=interp)


if __name__ == "__main__":
    main()
