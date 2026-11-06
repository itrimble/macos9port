"""4x upscale with Real-ESRGAN general v3 (compact), tiled, CPU friendly.

usage: python3 upscale.py WEIGHTS IN.png OUT.png
"""
import sys
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as Fn
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


class SRVGGNetCompact(nn.Module):
    def __init__(self, feat=64, conv=32, up=4):
        super().__init__()
        self.up = up
        body = [nn.Conv2d(3, feat, 3, 1, 1), nn.PReLU(feat)]
        for _ in range(conv):
            body += [nn.Conv2d(feat, feat, 3, 1, 1), nn.PReLU(feat)]
        body += [nn.Conv2d(feat, 3 * up * up, 3, 1, 1)]
        self.body = nn.ModuleList(body)
        self.upsampler = nn.PixelShuffle(up)

    def forward(self, x):
        out = x
        for m in self.body:
            out = m(out)
        return self.upsampler(out) + Fn.interpolate(x, scale_factor=self.up, mode='nearest')


def main():
    w, src, dst = sys.argv[1:4]
    torch.set_num_threads(2)
    net = SRVGGNetCompact()
    sd = torch.load(w, map_location='cpu')
    net.load_state_dict(sd.get('params', sd))
    net.eval()
    img = np.asarray(Image.open(src).convert('RGB')).astype(np.float32) / 255
    H, W, _ = img.shape
    out = np.zeros((H * 4, W * 4, 3), np.float32)
    T, P = 384, 16
    with torch.inference_mode():
        for y in range(0, H, T):
            for x in range(0, W, T):
                y0, x0 = max(0, y - P), max(0, x - P)
                y1, x1 = min(H, y + T + P), min(W, x + T + P)
                t = torch.from_numpy(img[y0:y1, x0:x1].transpose(2, 0, 1)).unsqueeze(0)
                o = net(t)[0].numpy().transpose(1, 2, 0)
                oy, ox = (y - y0) * 4, (x - x0) * 4
                hh, ww = min(T, H - y) * 4, min(T, W - x) * 4
                out[y * 4:y * 4 + hh, x * 4:x * 4 + ww] = o[oy:oy + hh, ox:ox + ww]
    Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(dst)


if __name__ == '__main__':
    main()
