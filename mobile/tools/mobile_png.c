/* PNG writer (zlib) for the mobile tools: RGB or RGBA from ARGB8888. */
#include "mobile_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    unsigned char c[4];
    be32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

int mobile_png_write(const char *path, const uint32_t *px, int w, int h, int alpha) {
    int bpp = alpha ? 4 : 3;
    size_t row = (size_t)w * bpp + 1, raw_n = row * h;
    unsigned char *raw = malloc(raw_n);
    uLongf z_n = compressBound((uLong)raw_n);
    unsigned char *z = malloc(z_n);
    if (!raw || !z) { free(raw); free(z); return -1; }
    for (int y = 0; y < h; y++) {
        unsigned char *r = raw + row * y;
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x];
            *r++ = (unsigned char)(c >> 16);
            *r++ = (unsigned char)(c >> 8);
            *r++ = (unsigned char)c;
            if (alpha) *r++ = (unsigned char)(c >> 24);
        }
    }
    if (compress2(z, &z_n, raw, (uLong)raw_n, 9) != Z_OK) { free(raw); free(z); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = alpha ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)z_n);
    chunk(f, "IEND", NULL, 0);
    int ok = fclose(f) == 0;
    free(raw);
    free(z);
    return ok ? 0 : -1;
}

/* Area-average downscale by an integer factor, keeping alpha. */
uint32_t *mobile_downscale(const uint32_t *px, int w, int h, int k, int *ow, int *oh) {
    *ow = w / k;
    *oh = h / k;
    uint32_t *out = malloc((size_t)*ow * *oh * sizeof *out);
    if (!out) return NULL;
    for (int y = 0; y < *oh; y++)
        for (int x = 0; x < *ow; x++) {
            unsigned a = 0, r = 0, g = 0, b = 0;
            for (int j = 0; j < k; j++)
                for (int i = 0; i < k; i++) {
                    uint32_t c = px[(size_t)(y * k + j) * w + x * k + i];
                    a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
                }
            unsigned n = (unsigned)(k * k);
            out[(size_t)y * *ow + x] = (a / n) << 24 | (r / n) << 16 | (g / n) << 8 | (b / n);
        }
    return out;
}
