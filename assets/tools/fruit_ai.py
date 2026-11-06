"""Finish the AI-made fruit-era wallpapers: denoise, 4x upscale, crop, export sizes.

usage: python3 fruit_ai.py SRC_DIR OUT_DIR WEIGHTS
The source images are AI-generated (not procedural), so render.py cannot
rebuild them; this script only reproduces the upscale and crop steps.
"""
import json, os, subprocess, sys
from PIL import Image, ImageFilter

Image.MAX_IMAGE_PIXELS = None
HERE = os.path.dirname(os.path.abspath(__file__))

# id, source file, title, year, horizontal anchor (0 = keep left edge, 1 = keep right)
FRUIT_AI = [
    ("42", "teal_bloom", "Teal Bloom", 1999, 0.2),
    ("43", "glass_shelf", "Glass Shelf", 2000, 0.7),
    ("45", "amber_cosmos", "Amber Cosmos", 2000, 0.3),
    ("47", "violet_colony", "Violet Colony", 1999, 0.5),
    ("48", "lime_spiral", "Lime Spiral", 1999, 0.5),
    ("51", "silk_curves", "Silk Curves", 2001, 0.8),
    ("52", "blue_shutters", "Terracotta Shutters", 2000, 0.5),
    ("53", "berry_cells", "Berry Cells", 1999, 0.5),
    ("55", "tangerine_coral", "Tangerine Coral", 1999, 0.7),
    ("57", "star_field_orange", "Tangerine Stars", 2000, 0.5),
    ("58", "saucer_lime", "Lime Saucers", 1999, 0.5),
    ("59", "saucer_gold", "Golden Lenses", 2000, 0.6),
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
    for cid, slug, title, year, ax in FRUIT_AI:
        name = f"{cid}-{year}-{title.lower().replace(' ', '-')}"
        big = f"/tmp/fruit_{slug}_x4.png"
        if not os.path.exists(big):
            pre = f"/tmp/fruit_{slug}_pre.png"
            Image.open(f"{src}/{slug}.png").convert("RGB").filter(
                ImageFilter.GaussianBlur(0.6)).save(pre)
            subprocess.run([sys.executable, f"{HERE}/upscale.py", weights, pre, big], check=True)
        im = Image.open(big).convert("RGB")
        for d, (w, h) in SIZES.items():
            os.makedirs(f"{out}/{d}", exist_ok=True)
            cover(im, w, h, ax).save(f"{out}/{d}/{name}.jpg", quality=93, subsampling=0)
        meta.append({"id": cid, "title": title, "era": "fruit era", "year": year,
                     "kind": "photo-ai", "file": f"{name}.jpg"})
        print(name, flush=True)
    with open(f"{out}/fruit_ai.json", "w") as f:
        json.dump(meta, f, indent=2)


if __name__ == "__main__":
    main()
