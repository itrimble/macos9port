"""Finish the big-cat wallpapers: denoise, 4x upscale, crop, export sizes.

usage: python3 cats.py SRC_DIR OUT_DIR WEIGHTS
The source images are AI-generated (not procedural), so render.py cannot
rebuild them; this script only reproduces the upscale and crop steps.
"""
import json, os, subprocess, sys
from PIL import Image, ImageFilter

Image.MAX_IMAGE_PIXELS = None
HERE = os.path.dirname(os.path.abspath(__file__))

# id, slug, title, year, horizontal anchor (0 = keep left edge, 1 = keep right)
CATS = [
    ("31", "cheetah", "Cheetah Dawn", 2001, 0.8),
    ("32", "puma", "Puma Blue Hour", 2001, 0.2),
    ("33", "jaguar", "Jaguar River", 2002, 0.85),
    ("34", "panther", "Black Panther", 2003, 0.6),
    ("35", "tiger", "Tiger in the Mist", 2005, 0.2),
    ("36", "leopard", "Leopard Nebula", 2007, 0.9),
    ("37", "snow-leopard", "Snow Leopard Aurora", 2009, 0.8),
    ("38", "lion", "Lion Under the Galaxy", 2011, 0.6),
    ("39", "mountain-lion", "Mountain Lion Alpenglow", 2012, 0.25),
]
SIZES = {"8k": (7680, 4320), "4k": (3840, 2160), "1080p": (1920, 1080),
         "16x10": (2560, 1600), "4x3": (1024, 768)}


def cover(im, w, h, ax):
    W, H = im.size
    s = max(w / W, h / H)
    cw, ch = round(w / s), round(h / s)
    x0 = round((W - cw) * ax)
    y0 = (H - ch) // 2
    return im.crop((x0, y0, x0 + cw, y0 + ch)).resize((w, h), Image.LANCZOS)


def main():
    src, out, weights = sys.argv[1:4]
    meta = []
    for cid, slug, title, year, ax in CATS:
        name = f"{cid}-{year}-{slug}"
        big = f"/tmp/{slug}_x4.png"
        if not os.path.exists(big):
            pre = f"/tmp/{slug}_pre.png"
            Image.open(f"{src}/{slug}.png").convert("RGB").filter(
                ImageFilter.GaussianBlur(0.6)).save(pre)
            subprocess.run([sys.executable, f"{HERE}/upscale.py", weights, pre, big], check=True)
        im = Image.open(big).convert("RGB")
        for d, (w, h) in SIZES.items():
            os.makedirs(f"{out}/{d}", exist_ok=True)
            cover(im, w, h, ax).save(f"{out}/{d}/{name}.jpg", quality=93, subsampling=0)
        meta.append({"id": cid, "title": title, "era": "big-cat era", "year": year,
                     "kind": "photo-ai", "file": f"{name}.jpg"})
        print(name, flush=True)
    with open(f"{out}/cats.json", "w") as f:
        json.dump(meta, f, indent=2)


if __name__ == "__main__":
    main()
