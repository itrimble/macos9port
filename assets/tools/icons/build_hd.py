"""Export high-resolution (HiDPI) OS Pi icons.

usage: python3 build_hd.py OUTDIR      (OUTDIR is the icons folder)
Writes hd/<size>/<name>.png (64..1024), ospi-icons@2x.rgba and
ospi-icons@4x.rgba (64 and 128 px RGBA sets), cursors/hd/, a HiDPI
contact sheet, and rebuilds app/OSPi.icns from the smooth art.
Run build.py first; it owns the 1x pixel set.
"""
import io, os, struct, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, binary_fill_holes
import set_a, set_b, set_c, cursors
from core import Icon
from hires import render_hd, downsample, scale2x

REG = {**set_a.REG, **set_b.REG, **set_c.REG}
SIZES = [64, 128, 256, 512, 1024]
SET_SIZES = {'@2x': 64, '@4x': 128}


def render_one(name):
    big = render_hd(REG[name][1], k=64)  # 2048 px master
    return name, {s: downsample(big, s) for s in SIZES}


def png_bytes(im):
    b = io.BytesIO()
    im.save(b, 'PNG', optimize=True)
    return b.getvalue()


def write_rgba_set(path, imgs, size):
    """OSPIHD01: magic, count u32, size u32, then name[24] + size*size RGBA."""
    blob = bytearray(b'OSPIHD01' + struct.pack('>II', len(imgs), size))
    for name, ims in imgs:
        blob += name.encode().ljust(24, b'\0') + np.array(ims[size], np.uint8).tobytes()
    with open(path, 'wb') as f:
        f.write(blob)


def write_icns(path, px32, hd):
    types = [(b'ic11', None), (b'ic12', 64), (b'ic07', 128), (b'ic13', 256), (b'ic08', 256),
             (b'ic14', 512), (b'ic09', 512), (b'ic10', 1024)]
    chunks = b''
    for t, s in types:
        data = png_bytes(px32 if s is None else hd[s])
        chunks += t + struct.pack('>I', len(data) + 8) + data
    with open(path, 'wb') as f:
        f.write(b'icns' + struct.pack('>I', len(chunks) + 8) + chunks)


def hd_cursors(out):
    cdir = os.path.join(out, 'cursors', 'hd')
    os.makedirs(cdir, exist_ok=True)
    for name, (_, rows) in cursors.CURSORS.items():
        black = np.array([[ch == '#' for ch in r] for r in rows])
        drawn = binary_fill_holes(np.array([[ch in '#W' for ch in r] for r in rows]))
        lab = np.where(black, 2, np.where(drawn, 1, 0)).astype(np.int8)
        for f in (2, 4):
            n = f * 4  # supersample, then downsample
            big = lab
            while big.shape[0] < 16 * n:
                big = scale2x(big)
            k = gaussian_filter((big == 2).astype(np.float32), 0.1 * n)
            w = gaussian_filter((big == 1).astype(np.float32), 0.1 * n)
            a = np.clip(k + w, 0, 1)
            v = 255 * w / np.maximum(k + w, 1e-6)
            rgba = np.dstack([v, v, v, 255 * a]).astype(np.uint8)
            downsample(Image.fromarray(rgba, 'RGBA'), 16 * f).save(
                os.path.join(cdir, f'{name}@{f}x.png'))


def contact(out, imgs):
    cols, S, pad = 10, 128, 24
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * (S + pad) + pad, rows * (S + pad) + pad), (91, 113, 162, 255))
    for i, (_, ims) in enumerate(imgs):
        sheet.alpha_composite(ims[S], (pad + (i % cols) * (S + pad), pad + (i // cols) * (S + pad)))
    sheet.convert('RGB').save(os.path.join(out, 'contact-sheet-hd.png'))


def main(out):
    names = list(REG)
    with Pool() as p:
        imgs = p.map(render_one, names)
    for name, ims in imgs:
        for s, im in ims.items():
            d = os.path.join(out, 'hd', str(s))
            os.makedirs(d, exist_ok=True)
            im.save(os.path.join(d, name + '.png'), optimize=True)
    for tag, s in SET_SIZES.items():
        write_rgba_set(os.path.join(out, f'ospi-icons{tag}.rgba'), imgs, s)
    ic = Icon()
    REG['pi_logo'][1](ic)
    os.makedirs(os.path.join(out, 'app'), exist_ok=True)
    write_icns(os.path.join(out, 'app', 'OSPi.icns'), ic.im, dict(imgs)['pi_logo'])
    hd_cursors(out)
    contact(out, imgs)
    print(len(imgs), 'icons at', SIZES)


if __name__ == '__main__':
    main(sys.argv[1])
