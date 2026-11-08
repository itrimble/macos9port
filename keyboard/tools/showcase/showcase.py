#!/usr/bin/env python3
"""OS999 on foldables: a directed showcase video and stills.

Drives tools/showcase/stage (the real OS999 desktop plus the real keyboard)
through a timeline of finger events, composes phone bodies around the
screens, and muxes the synthesized switch sounds into the video.

    python3 showcase.py STAGE_BIN FONT OUTDIR
Writes OUTDIR/os999-foldables.mp4 and OUTDIR/still-*.png. Needs Pillow,
numpy and ffmpeg.
"""
import math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

STAGE, FONT, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
W, H, FPS = 1920, 1080, 60
COVER, OPEN, FLEX = 0, 1, 2

# Screen specs. Fold8 and Pixel from published specs; the foldable iPhone
# is unannounced, so it uses the widely reported leaked panel resolutions.
DEV = {
    'fold8': dict(name='Galaxy Z Fold8', cover=(1248, 1972, 428), inner=(2448, 1848, 403),
                  body=(31, 33, 36), edge=(70, 74, 80), wall=7, skin=4, crease=0.05, hole='center'),
    'iphone': dict(name='Foldable iPhone (rumored specs)', cover=(1422, 2088, 460), inner=(2713, 1920, 428),
                   body=(44, 43, 47), edge=(150, 150, 156), wall=13, skin=0, crease=0.0, hole='center'),
    'pixel': dict(name='Pixel 11 Pro Fold', cover=(1080, 2342, 399), inner=(2076, 2152, 372),
                  body=(30, 34, 38), edge=(84, 90, 96), wall=5, skin=3, crease=0.07, hole='center'),
}
SKIN_NAMES = ['Platinum', 'Desk Beige', 'Original 128', 'Portable Black', 'Teal Translucent',
              'Graphite', 'Five Flavors', 'Midnight']

def font(px):
    return ImageFont.truetype(FONT, px)

class Stage:
    def __init__(self, audio):
        env = dict(os.environ, HOME='/tmp/os999-showcase-home', TZ='America/Chicago')
        self.p = subprocess.Popen([STAGE, FONT], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)
        self.cmd(f'audio {audio}')
        self.t = 0.0          # seconds of stage time sent

    def cmd(self, c):
        self.p.stdin.write((c + '\n').encode())
        self.p.stdin.flush()
        if c.startswith('render'):
            h = self.p.stdout.readline().decode().split()
            w, hh = int(h[1]), int(h[2])
            img = Image.frombuffer('RGBA', (w, hh), self.p.stdout.read(w * hh * 4), 'raw', 'BGRA', 0, 1)
            return img.convert('RGB'), h[3]
        self.p.stdout.readline()

    def advance_to(self, t):
        ms = int(round(t * 1000)) - int(round(self.t * 1000))
        if ms > 0:
            self.cmd(f'step {ms}')
        self.t = t

    def close(self):
        self.cmd('quit')
        self.p.wait()

# ---- phone bodies ---------------------------------------------------------
def rounded_mask(size, r):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), r, fill=255)
    return m

def body(screen, d, pxin, cover, crease=True):
    """Phone front around a screen image. pxin = output pixels per inch."""
    bez = max(6, int(0.085 * pxin))
    sw, sh = screen.size
    r_out = int((0.36 if cover else 0.24) * pxin)
    r_in = max(4, r_out - bez)
    img = Image.new('RGBA', (sw + 2 * bez, sh + 2 * bez), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    dr.rounded_rectangle((0, 0, img.width - 1, img.height - 1), r_out, fill=d['body'] + (255,),
                         outline=d['edge'] + (255,), width=max(2, bez // 5))
    img.paste(screen, (bez, bez), rounded_mask(screen.size, r_in))
    if cover:
        cx, cy, rr = bez + sw // 2, bez + int(0.16 * pxin), max(4, int(0.045 * pxin))
        dr.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=(6, 6, 8, 255))
    elif crease and d['crease'] > 0:
        ov = Image.new('RGBA', img.size, (0, 0, 0, 0))
        od = ImageDraw.Draw(ov)
        cx = img.width // 2
        for i, a in enumerate((0.5, 1.0, 0.5)):
            od.line((cx - 1 + i, bez, cx - 1 + i, bez + sh), fill=(255, 255, 255, int(255 * d['crease'] * a)))
        img = Image.alpha_composite(img, ov)
    return img

def coeffs(dst, src):
    m = []
    for (x, y), (u, v) in zip(dst, src):
        m.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        m.append([0, 0, 0, x, y, 1, -v * x, -v * y])
    a = np.array(m, dtype=float)
    b = np.array(src, dtype=float).reshape(8)
    return np.linalg.solve(a, b).tolist()

def flex_body(screen, d, pxin, bend):
    """Screen turned on its side and bent like a laptop: top half upright,
    bottom half lying back in perspective. bend 0..1."""
    sw, sh = screen.size
    top, bot = screen.crop((0, 0, sw, sh // 2)), screen.crop((0, sh // 2, sw, sh))
    upright = body(top, d, pxin, False, crease=False)
    base = body(bot, d, pxin, False, crease=False)
    fw, fh = base.size
    squash, widen = 1 - 0.36 * bend, 1 + 0.14 * bend
    oh, ow = int(fh * squash), int(fw * widen)
    dst = [((ow - fw) / 2, 0), ((ow + fw) / 2, 0), (ow, oh), (0, oh)]
    src = [(0, 0), (fw, 0), (fw, fh), (0, fh)]
    warped = base.transform((ow, oh), Image.PERSPECTIVE, coeffs(dst, src), Image.BICUBIC)
    shade = Image.new('RGBA', warped.size, (0, 0, 0, int(40 * bend)))
    warped = Image.alpha_composite(warped, Image.composite(shade, Image.new('RGBA', warped.size), warped.split()[3]))
    hinge = max(6, int(0.07 * pxin))
    out = Image.new('RGBA', (ow, upright.height + hinge + oh), (0, 0, 0, 0))
    out.paste(upright, ((ow - upright.width) // 2, 0), upright)
    dr = ImageDraw.Draw(out)
    hx0, hx1 = (ow - fw) // 2 + 6, (ow + fw) // 2 - 6
    dr.rounded_rectangle((hx0, upright.height - 4, hx1, upright.height + hinge + 4), hinge // 2,
                         fill=tuple(int(c * 0.7) for c in d['body']) + (255,))
    out.paste(warped, (0, upright.height + hinge), warped)
    return out

# ---- frame composition ----------------------------------------------------
BG = None
def background():
    global BG
    if BG is None:
        y = np.linspace(0, 1, H)[:, None]
        x = np.linspace(-1, 1, W)[None, :]
        v = 1 - 0.35 * (x ** 2) - 0.25 * (y - 0.4) ** 2
        r = (38 * v + 4 * y).clip(0, 255); g = (40 * v + 4 * y).clip(0, 255); b = (48 * v + 6 * y).clip(0, 255)
        BG = Image.fromarray(np.dstack([r, g, b]).astype('uint8'), 'RGB').convert('RGBA')
    return BG.copy()

def shadow_paste(canvas, img, xy, blur=24, alpha=150):
    sh = Image.new('RGBA', (img.width + blur * 4, img.height + blur * 4), (0, 0, 0, 0))
    sh.paste((0, 0, 0, alpha), (blur * 2, blur * 2), img.split()[3])
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    canvas.alpha_composite(sh, (int(xy[0] - blur * 2), int(xy[1] - blur * 2 + blur // 2)))
    canvas.alpha_composite(img, (int(xy[0]), int(xy[1])))

def caption(canvas, title, sub, a=1.0, y=990):
    dr = ImageDraw.Draw(canvas)
    f1, f2 = font(34), font(20)
    tw = dr.textlength(title, font=f1)
    dr.text(((W - tw) / 2, y), title, font=f1, fill=(245, 245, 248, int(255 * a)))
    sw = dr.textlength(sub, font=f2)
    dr.text(((W - sw) / 2, y + 44), sub, font=f2, fill=(170, 172, 182, int(255 * a)))

def pill(canvas, label, x, y):
    if label == '-':
        return
    dr = ImageDraw.Draw(canvas)
    f = font(20)
    text = 'haptic: ' + label
    tw = dr.textlength(text, font=f)
    dr.rounded_rectangle((x, y, x + tw + 44, y + 36), 18, fill=(92, 92, 190, 230))
    dr.ellipse((x + 12, y + 12, x + 24, y + 24), fill=(255, 255, 255, 255))
    dr.text((x + 32, y + 6), text, font=f, fill=(255, 255, 255, 255))

def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def spring(t, k=11.0, z=0.55):
    """Underdamped step response 0->1 for transition curves."""
    if t <= 0:
        return 0.0
    wd = k * math.sqrt(1 - z * z)
    return 1 - math.exp(-z * k * t) * (math.cos(wd * t) + z / math.sqrt(1 - z * z) * math.sin(wd * t))

# ---- the film ----------------------------------------------------------------
class Film:
    def __init__(self):
        os.makedirs(OUT, exist_ok=True)
        self.audio = os.path.join(OUT, 'switches.raw')
        self.stage = Stage(self.audio)
        self.video = os.path.join(OUT, 'video-only.mp4')
        self.ff = subprocess.Popen(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                                    '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-', '-c:v', 'libx264',
                                    '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p', self.video],
                                   stdin=subprocess.PIPE)
        self.t = 0.0
        self.events = []           # (time, command)

    def at(self, t, c):
        self.events.append((t, c))

    def type(self, t, slot, text, fid=1, gap=0.13, hold=0.07, thumbs=False):
        for ch in text:
            name = {' ': 'space', '\n': 'return'}.get(ch, ch)
            f = fid + (1 if thumbs and ch in 'yuiophjklnm' else 0)
            if ch.isupper():
                self.at(t, f'down {slot} 9 shift'); self.at(t + 0.06, f'up {slot} 9'); t += 0.12
                name = ch.lower()
            self.at(t, f'down {slot} {f} {name}')
            self.at(t + hold, f'up {slot} {f}')
            t += gap
        return t

    def run_events(self, until):
        self.events.sort(key=lambda e: e[0])
        while self.events and self.events[0][0] <= until:
            t, c = self.events.pop(0)
            self.stage.advance_to(max(t, self.stage.t))
            self.stage.cmd(c)
        self.stage.advance_to(until)

    def emit(self, canvas):
        self.ff.stdin.write(canvas.convert('RGB').tobytes())

    def frames(self, t0, t1, draw):
        print(f'{draw.__name__:14s} {t0:6.2f} -> {t1:6.2f}', file=sys.stderr)
        n = int(round((t1 - t0) * FPS))
        for i in range(n):
            t = t0 + i / FPS
            self.run_events(t)
            self.emit(draw(t, (t - t0) / max(1e-6, t1 - t0)))
        self.t = t1

    def finish(self):
        self.stage.close()
        self.ff.stdin.close()
        self.ff.wait()
        out = os.path.join(OUT, 'os999-foldables.mp4')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', self.video, '-f', 's16le', '-ar', '48000',
                        '-ac', '2', '-i', self.audio, '-c:v', 'copy', '-c:a', 'aac', '-b:a', '192k',
                        '-shortest', out], check=True)
        os.remove(self.video)
        os.remove(self.audio)
        return out

def screen_slot(stage_cmd, slot, dev, which, posture, pxin, split=1, rotated=False):
    w, h, ppi = DEV[dev][which]
    if rotated:
        w, h = h, w
    scale = pxin / ppi
    stage_cmd(f'slot {slot} {w} {h} {ppi / 160:.4f} {scale:.5f} {posture} {split} {DEV[dev]["skin"]} {DEV[dev]["wall"]}')

def main():
    F = Film()
    S = F.stage
    PX_COVER, PX_OPEN = 176.0, 172.0

    # Scene 1: title, then the Galaxy Z Fold8 cover screen.
    screen_slot(S.cmd, 0, 'fold8', 'cover', COVER, PX_COVER)
    S.cmd('text 0 Dear OS999,\\n')
    S.cmd('show 0 0')
    F.at(1.5, 'show 0 1')
    t = F.type(2.2, 0, 'Typing at the caf')
    F.at(t + 0.1, 'down 0 1 e')                       # long-press: accent menu
    F.at(t + 0.75, 'alt 0 1 1')
    F.at(t + 0.95, 'alt 0 1 2')
    F.at(t + 1.15, 'alt 0 1 1')
    F.at(t + 1.4, 'up 0 1')
    t = F.type(t + 1.7, 0, ' on the cover')
    F.at(t, 'down 0 1 123'); F.at(t + 0.07, 'up 0 1')  # layer flip
    t = F.type(t + 0.5, 0, '!', gap=0.3)
    F.at(t + 0.3, 'down 0 1 ABC'); F.at(t + 0.37, 'up 0 1')
    t += 1.0
    F.at(t, 'down 0 1 space')                          # space-bar trackpad
    for i in range(1, 36):
        F.at(t + 0.45 + i * 0.016, f'nudge 0 1 {9 if i < 20 else -4} {-6 if i < 20 else -3}')
    F.at(t + 1.05, 'up 0 1')
    cover_end = t + 1.4

    def cover_frame(tt, p):
        c = background()
        img, lab = S.cmd('render 0')
        dev = body(img, DEV['fold8'], PX_COVER, True)
        x, y = (W - dev.width) // 2, 60
        a = ease(tt / 0.8)
        if tt < 1.2:
            d = ImageDraw.Draw(c)
            f = font(64)
            msg = 'OS999 on foldables'
            d.text(((W - d.textlength(msg, font=f)) / 2, 470), msg, font=f,
                   fill=(255, 255, 255, int(255 * (1 - ease((tt - 0.6) / 0.6)))))
            if tt < 0.6:
                return c
        dev.putalpha(Image.eval(dev.split()[3], lambda v: int(v * ease((tt - 0.6) / 0.6))))
        shadow_paste(c, dev, (x, y))
        caption(c, 'Galaxy Z Fold8  -  folded', 'cover screen 1248 x 1972, 5.5 in  -  Teal Translucent keys, rubber-dome sound')
        pill(c, lab, x + dev.width + 30, y + dev.height - 300)
        return c
    F.frames(0, cover_end, cover_frame)

    # Scene 2: unfold. The inner screen opens like a book; its keyboard rises.
    screen_slot(S.cmd, 1, 'fold8', 'inner', OPEN, PX_OPEN, split=1)
    S.cmd('copytext 0 1')
    t0 = cover_end

    def unfold_frame(tt, p):
        c = background()
        a = spring(p * 1.25, k=8, z=0.7)
        cimg, _ = S.cmd('render 0')
        cover = body(cimg, DEV['fold8'], PX_COVER, True)
        iimg, lab = S.cmd('render 1')
        inner = body(iimg, DEV['fold8'], PX_OPEN, False)
        if a < 0.98:
            sx = max(1, int(cover.width * (1 - ease(p * 1.6))))
            cv = cover.resize((sx, cover.height))
            cv.putalpha(Image.eval(cv.split()[3], lambda v: int(v * (1 - ease(p * 1.4)))))
            shadow_paste(c, cv, ((W - cover.width) // 2 + cover.width - sx, 60))
        iw = max(1, int(inner.width * max(0.08, min(1.04, a))))
        iv = inner.resize((iw, inner.height))
        iv.putalpha(Image.eval(iv.split()[3], lambda v: int(v * ease(p * 2))))
        shadow_paste(c, iv, ((W - iw) // 2, 70))
        caption(c, 'Galaxy Z Fold8  -  unfolded', 'inner screen 2448 x 1848, 7.6 in  -  split thumbs, gutter trackpad, nothing on the crease',
                a=ease(p * 2))
        return c
    F.frames(t0, t0 + 1.6, unfold_frame)
    S.cmd('show 0 0')

    # Scene 3: thumb typing, then the split halves morph into the desk layout and back.
    t = F.type(F.t + 0.5, 1, '\nTwo thumbs, no crease.', fid=1, gap=0.12, thumbs=True)
    F.at(t + 0.4, 'split 1 0')
    F.at(t + 2.0, 'split 1 1')
    open_end = t + 3.3

    def open_frame(tt, p):
        c = background()
        img, lab = S.cmd('render 1')
        dev = body(img, DEV['fold8'], PX_OPEN, False)
        x, y = (W - dev.width) // 2, 70
        shadow_paste(c, dev, (x, y))
        caption(c, 'Galaxy Z Fold8  -  unfolded', 'inner screen 2448 x 1848, 7.6 in  -  split thumbs, gutter trackpad, nothing on the crease')
        pill(c, lab, x + dev.width + 24, y + dev.height - 200)
        return c
    F.frames(F.t, open_end, open_frame)

    # Scene 4: turn it on its side and bend it: Flex. Keys spin into a laptop deck.
    rot_t0 = F.t

    def rotate_frame(tt, p):
        c = background()
        img, _ = S.cmd('render 1')
        dev = body(img, DEV['fold8'], PX_OPEN, False)
        ang = -90 * ease(p)
        r = dev.rotate(ang, resample=Image.BICUBIC, expand=True)
        s = 1 - 0.2 * math.sin(math.pi * p)
        r = r.resize((int(r.width * s), int(r.height * s)), Image.BICUBIC)
        shadow_paste(c, r, ((W - r.width) // 2, (H - 60 - r.height) // 2))
        return c
    F.frames(rot_t0, rot_t0 + 0.9, rotate_frame)
    w, h, ppi = DEV['fold8']['inner']
    S.cmd(f'rotate 1 {h} {w} 1 {FLEX}')
    flex_t0 = F.t
    F.at(flex_t0 + 1.5, 'pad 1 1 0.35 0.6')
    for i in range(1, 50):
        F.at(flex_t0 + 1.5 + i * 0.016, f'padmove 1 1 {0.35 + 0.006 * i:.3f} {0.6 - 0.005 * i:.3f}')
    for i, pz in enumerate([0.15, 0.3, 0.48, 0.55, 0.55, 0.4, 0.2, 0.1]):
        F.at(flex_t0 + 2.5 + i * 0.05, f'force 1 1 {pz}')
    for i, pz in enumerate([0.3, 0.55, 0.75, 0.95, 0.95, 0.95, 0.6, 0.3, 0.1]):
        F.at(flex_t0 + 3.3 + i * 0.05, f'force 1 1 {pz}')
    F.at(flex_t0 + 4.0, 'up 1 1')
    t = F.type(flex_t0 + 4.3, 1, '\nFeels like a portable.', gap=0.11)
    flex_end = t + 0.8

    def flex_frame(tt, p):
        c = background()
        img, lab = S.cmd('render 1')
        bend = ease((tt - flex_t0) / 0.7)
        dev = flex_body(img, DEV['fold8'], PX_OPEN, bend)
        x, y = (W - dev.width) // 2, max(10, (H - 90 - dev.height) // 2)
        shadow_paste(c, dev, (x, y))
        caption(c, 'Galaxy Z Fold8  -  Flex', 'laptop deck: full layout, Force Touch-style clickpad with click and force-click haptics')
        pill(c, lab, x + dev.width + 24, y + dev.height - 260)
        return c
    F.frames(flex_t0, flex_end, flex_frame)

    # Scene 5: the lineup. All three in Flex, each with its own skin and sound.
    S.cmd('show 1 0')
    PX_L = 90.0
    for i, name in enumerate(('fold8', 'iphone', 'pixel')):
        w, h, ppi = DEV[name]['inner']
        S.cmd(f'slot {i + 1} {h} {w} {ppi / 160:.4f} {PX_L / ppi:.5f} {FLEX} 0 {DEV[name]["skin"]} {DEV[name]["wall"]}')
        S.cmd(f'text {i + 1} {DEV[name]["name"]}\\n')
    l0 = F.t
    for i, word in enumerate(('Platinum dreams', 'Click clack', 'Midnight oil')):
        F.type(l0 + 0.9 + i * 0.25, i + 1, word, fid=1, gap=0.15)

    def lineup_frame(tt, p):
        c = background()
        imgs = []
        for i, name in enumerate(('fold8', 'iphone', 'pixel')):
            img, lab = S.cmd(f'render {i + 1}')
            imgs.append((flex_body(img, DEV[name], PX_L, 1.0), name, lab))
        gap = 70
        total = sum(d.width for d, _, _ in imgs) + gap * 2
        x = (W - total) // 2
        a = ease((tt - l0) / 0.5)
        base = 860
        for d, name, lab in imgs:
            y = base - d.height + int(30 * (1 - a))
            d.putalpha(Image.eval(d.split()[3], lambda v: int(v * a)))
            shadow_paste(c, d, (x, y), blur=18)
            dr = ImageDraw.Draw(c)
            f = font(22)
            label = DEV[name]['name']
            dr.text((x + (d.width - dr.textlength(label, font=f)) / 2, y + d.height + 18), label, font=f,
                    fill=(235, 235, 240, int(255 * a)))
            wq, hq, _ = DEV[name]['inner']
            sub = f'{wq} x {hq}  -  {SKIN_NAMES[DEV[name]["skin"]]}'
            f2 = font(17)
            dr.text((x + (d.width - dr.textlength(sub, font=f2)) / 2, y + d.height + 48), sub, font=f2,
                    fill=(160, 162, 172, int(255 * a)))
            x += d.width + gap
        return c
    F.frames(l0, l0 + 5.2, lineup_frame)
    print(F.finish())

def stills():
    """Each phone in each posture at its panel's native resolution."""
    st = Stage('/dev/null')
    out = []
    for name in ('fold8', 'iphone', 'pixel'):
        d = DEV[name]
        for posture, which, label in ((COVER, 'cover', 'folded'), (OPEN, 'inner', 'open'), (FLEX, 'inner', 'flex')):
            w, h, ppi = d[which]
            if posture == FLEX:
                w, h = h, w
            st.cmd(f'slot 0 {w} {h} {ppi / 160:.4f} 1 {posture} 1 {d["skin"]} {d["wall"]}')
            st.cmd(f'text 0 OS999 on the {d["name"]}\\n\\nThe keyboard follows the hinge: phone keys when folded, thumb halves when open, a laptop deck in Flex.')
            st.cmd('step 1500')
            if posture == FLEX:
                st.cmd('pad 0 1 0.45 0.55')
                for i in range(12):
                    st.cmd('step 16')
                    st.cmd(f'padmove 0 1 {0.45 + i * 0.01:.2f} {0.55 - i * 0.008:.3f}')
            st.cmd('down 0 2 g')
            st.cmd('step 70')
            img, _ = st.cmd('render 0')
            pxin = float(ppi)
            dev = flex_body(img, d, pxin, 1.0) if posture == FLEX else body(img, d, pxin, posture == COVER)
            pad = int(0.5 * pxin)
            canvas = Image.new('RGBA', (dev.width + 2 * pad, dev.height + 2 * pad), (30, 32, 38, 255))
            shadow_paste(canvas, dev, (pad, pad), blur=int(0.12 * pxin))
            path = os.path.join(OUT, f'still-{name}-{label}.png')
            canvas.convert('RGB').save(path, optimize=True)
            out.append(path)
            st.cmd('up 0 2')
            st.cmd('up 0 1')
    st.close()
    return out

if __name__ == '__main__':
    if len(sys.argv) > 4 and sys.argv[4] == 'stills':
        print('\n'.join(stills()))
    else:
        main()
