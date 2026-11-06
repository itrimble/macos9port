"""Render the procedural fruit-era wallpapers (fruit.py) at every size.

usage: python3 fruit_render.py OUT_DIR [--preview]
"""
import json, sys, time
import cv2
from core import to_u8
from fruit import FRUIT
from render import save


def name_of(cid, title, year):
    return f"{cid}-{year}-{title.lower().replace(' ', '-')}"


def main():
    out = sys.argv[1]
    meta = []
    for cid, title, year, fn in FRUIT:
        t = time.time()
        name = name_of(cid, title, year)
        if "--preview" in sys.argv:
            save(to_u8(fn(960, 540), seed=int(cid)), f"{out}/preview/{name}.jpg", False)
        else:
            m = to_u8(fn(7680, 4320), seed=int(cid))
            save(m, f"{out}/8k/{name}.jpg", False)
            for d, (w, h) in (("4k", (3840, 2160)), ("1080p", (1920, 1080))):
                save(cv2.resize(m, (w, h), interpolation=cv2.INTER_AREA),
                     f"{out}/{d}/{name}.jpg", False)
            del m
            for d, (w, h) in (("16x10", (2560, 1600)), ("4x3", (1024, 768))):
                save(to_u8(fn(w, h), seed=int(cid)), f"{out}/{d}/{name}.jpg", False)
        meta.append({"id": cid, "title": title, "era": "fruit era", "year": year,
                     "kind": "art", "file": f"{name}.jpg"})
        print(f"{name}: {time.time() - t:.1f}s", flush=True)
    with open(f"{out}/fruit.json", "w") as f:
        json.dump(meta, f, indent=2)


if __name__ == "__main__":
    main()
