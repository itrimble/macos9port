"""Export the OS Pi icon set.

usage: python3 build.py OUTDIR
Writes ospi-icons.icl8 (binary set), png/<size>/<name>.png, cursors/,
OSPi.icns, manifest.json, and contact-sheet.png. Deterministic output.
"""
import io, json, os, struct, sys
import numpy as np
from PIL import Image
from scipy.ndimage import binary_fill_holes
from core import Icon, PAL, PAL_INDEX
import set_a, set_b, cursors

SIZES = [16, 32, 64, 128, 256, 512, 1024]
REG = {**set_a.REG, **set_b.REG}


def render(name):
    ic = Icon()
    REG[name][1](ic)
    return ic


def small(idx, mask):
    """2x2 reduce to 16x16: coverage mask, darkest-biased mode color, black rim."""
    m = mask.reshape(16, 2, 16, 2).any(axis=(1, 3))
    out = np.zeros((16, 16), np.uint8)
    for y in range(16):
        for x in range(16):
            blk = idx[y * 2:y * 2 + 2, x * 2:x * 2 + 2][mask[y * 2:y * 2 + 2, x * 2:x * 2 + 2]]
            if blk.size:
                v, n = np.unique(blk, return_counts=True)
                best = v[n == n.max()]
                out[y, x] = min(best, key=lambda i: PAL[i].sum())
    inner = m.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        inner &= np.roll(np.roll(np.pad(m, 1), dy, 0), dx, 1)[1:-1, 1:-1]
    out[m & ~inner] = PAL_INDEX[(0, 0, 0)]
    return out, m


def pack_bits(m):
    return np.packbits(m.astype(np.uint8), axis=1).tobytes()


def to_rgba(idx, mask):
    rgb = PAL[idx].astype(np.uint8)
    a = (mask * 255).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, a]), 'RGBA')


def png_bytes(im):
    b = io.BytesIO()
    im.save(b, 'PNG', optimize=True)
    return b.getvalue()


def scaled(im32, im16, size):
    src = im16 if size == 16 else im32
    return src.resize((size, size), Image.NEAREST)


def write_icns(path, im32, im16):
    types = [(b'ic11', 32), (b'ic12', 64), (b'ic07', 128), (b'ic13', 256), (b'ic08', 256),
             (b'ic14', 512), (b'ic09', 512), (b'ic10', 1024)]
    chunks = b''
    for t, s in types:
        data = png_bytes(scaled(im32, im16, s))
        chunks += t + struct.pack('>I', len(data) + 8) + data
    with open(path, 'wb') as f:
        f.write(b'icns' + struct.pack('>I', len(chunks) + 8) + chunks)


def cursor_assets(out):
    cdir = os.path.join(out, 'cursors')
    os.makedirs(cdir, exist_ok=True)
    meta = {}
    for name, ((hx, hy), rows) in cursors.CURSORS.items():
        data = np.array([[ch == '#' for ch in r] for r in rows])
        drawn = np.array([[ch in '#W' for ch in r] for r in rows])
        mask = binary_fill_holes(drawn)
        blob = pack_bits(data) + pack_bits(mask) + struct.pack('>hh', hy, hx)
        assert len(blob) == 68
        with open(os.path.join(cdir, name + '.curs'), 'wb') as f:
            f.write(blob)
        rgba = np.zeros((16, 16, 4), np.uint8)
        rgba[mask] = (255, 255, 255, 255)
        rgba[data] = (0, 0, 0, 255)
        im = Image.fromarray(rgba, 'RGBA')
        for s in (16, 32, 64):
            im.resize((s, s), Image.NEAREST).save(os.path.join(cdir, f'{name}@{s}.png'))
        meta[name] = {'hotspot': [hx, hy]}
    return meta


def contact(out, items):
    cols, S = 12, 3
    cell = 40 * S
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * cell, rows * (cell + 18 * S) + 8), (91, 113, 162, 255))
    from PIL import ImageDraw
    d = ImageDraw.Draw(sheet)
    for i, (name, im32, im16) in enumerate(items):
        x, y = (i % cols) * cell, (i // cols) * (cell + 18 * S)
        sheet.alpha_composite(im32.resize((32 * S, 32 * S), Image.NEAREST), (x + 4 * S, y + 4 * S))
        sheet.alpha_composite(im16.resize((16 * S, 16 * S), Image.NEAREST), (x + 23 * S, y + 37 * S))
        d.text((x + 4, y + 37 * S + 4), name[:16], fill=(255, 255, 255, 255))
    sheet.convert('RGB').save(os.path.join(out, 'contact-sheet.png'))


def main(out):
    os.makedirs(out, exist_ok=True)
    blob = bytearray(b'OSPIICN1' + struct.pack('>I', len(REG)))
    items, manifest = [], {}
    for name in REG:
        ic = render(name)
        idx, m32 = ic.indices(), ic.mask()
        idx16, m16 = small(idx, m32)
        assert len(name) < 24
        blob += name.encode().ljust(24, b'\0') + idx.tobytes() + pack_bits(m32)
        blob += idx16.tobytes() + pack_bits(m16)
        im32, im16 = to_rgba(idx, m32), to_rgba(idx16, m16)
        for s in SIZES:
            d = os.path.join(out, 'png', str(s))
            os.makedirs(d, exist_ok=True)
            scaled(im32, im16, s).save(os.path.join(d, name + '.png'), optimize=True)
        items.append((name, im32, im16))
        manifest[name] = {'category': REG[name][0]}
    with open(os.path.join(out, 'ospi-icons.icl8'), 'wb') as f:
        f.write(blob)
    logo = dict((n, (a, b)) for n, a, b in items)['pi_logo']
    os.makedirs(os.path.join(out, 'app'), exist_ok=True)
    write_icns(os.path.join(out, 'app', 'OSPi.icns'), *logo)
    cur = cursor_assets(out)
    with open(os.path.join(out, 'manifest.json'), 'w') as f:
        json.dump({'format': 'OSPIICN1', 'record_bytes': 24 + 1024 + 128 + 256 + 32,
                   'icons': manifest, 'cursors': cur, 'png_sizes': SIZES}, f, indent=1)
    contact(out, items)
    print(len(REG), 'icons,', len(cur), 'cursors')


if __name__ == '__main__':
    main(sys.argv[1])
