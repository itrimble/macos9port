/* Minimal PNG writer (stored deflate blocks), so the shot tool needs no
 * image library. Writes 8-bit RGB from an ARGB8888 buffer. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t c, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 255] ^ (c >> 8);
    return c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc_update(c, data, len) ^ 0xFFFFFFFFu;
    uint8_t t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}

int png_write(const char *path, const uint32_t *px, int w, int h) {
    crc_init();
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 3);
        row[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x];
            row[1 + x * 3] = c >> 16; row[2 + x * 3] = c >> 8; row[3 + x * 3] = (uint8_t)c;
        }
    }
    size_t blocks = (raw_len + 65534) / 65535, zlen = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(zlen);
    if (!z) { free(raw); return 0; }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i += 65535) {
        size_t n = raw_len - i < 65535 ? raw_len - i : 65535;
        z[o++] = i + n >= raw_len;
        z[o++] = n & 255; z[o++] = n >> 8; z[o++] = ~n & 255; z[o++] = (~n >> 8) & 255;
        memcpy(z + o, raw + i, n);
        o += n;
    }
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + o, b << 16 | a);
    o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}
