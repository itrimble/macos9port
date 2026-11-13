#include "frame_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int frame_dump_write(const char *path, const uint32_t *fb, int w, int h) {
    if (!path || !*path || !fb || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fprintf(f, "P6\n%d %d\n255\n", w, h) > 0;
    for (long i = 0; ok && i < (long)w * h; i++) {
        unsigned char rgb[3] = {(unsigned char)(fb[i] >> 16), (unsigned char)(fb[i] >> 8), (unsigned char)fb[i]};
        ok = fwrite(rgb, 1, 3, f) == 3;
    }
    return fclose(f) == 0 && ok ? 0 : -1;
}

int frame_dump_due(const char *times, int *next, uint32_t elapsed_ms) {
    if (!times || !*times || !next || *next < 0) return 0;
    const char *p = times;
    for (int i = 0; i < *next; i++) {
        p = strchr(p, ',');
        if (!p) return 0;
        p++;
    }
    char *end = NULL;
    unsigned long at = strtoul(p, &end, 10);
    if (end == p || (*end && *end != ',')) return 0;
    if (elapsed_ms < at) return 0;
    (*next)++;
    return 1;
}

int frame_dump_name(const char *base, int index, char *out, size_t n) {
    if (!base || !*base || !out || index < 0) return -1;
    size_t len = strlen(base);
    if (len > 4 && !strcmp(base + len - 4, ".ppm")) len -= 4;
    int k = snprintf(out, n, "%.*s-%d.ppm", (int)len, base, index);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

int frame_dump_distinct(const uint32_t *fb, int w, int h, int x, int y, int rw, int rh, int cap) {
    if (!fb || cap < 1) return 0;
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return 0;
    uint32_t *seen = malloc((size_t)cap * sizeof *seen);
    if (!seen) return 0;
    int n = 0;
    for (int j = y; j < y + rh && n < cap; j++)
        for (int i = x; i < x + rw && n < cap; i++) {
            uint32_t v = fb[(size_t)j * w + i] & 0xFFFFFFu;
            int k = 0;
            while (k < n && seen[k] != v) k++;
            if (k == n) seen[n++] = v;
        }
    free(seen);
    return n;
}

int frame_surface_stats(const uint8_t *px, int w, int h, size_t stride, SurfaceStats *out) {
    if (!px || !out || w <= 0 || h <= 0 || stride < (size_t)w * 4) return -1;
    enum { GX = 48, GY = 27 };
    uint16_t seen[GX * GY];
    SurfaceStats s = {0, 0, 0, 255, 0};
    long sum = 0;
    for (int j = 0; j < GY; j++)
        for (int i = 0; i < GX; i++) {
            int x = (int)(((long)i * 2 + 1) * w / (GX * 2)), y = (int)(((long)j * 2 + 1) * h / (GY * 2));
            const uint8_t *p = px + (size_t)y * stride + (size_t)x * 4;
            int avg = (p[0] + p[1] + p[2]) / 3;
            uint16_t key = (uint16_t)(((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
            int k = 0;
            while (k < s.distinct && seen[k] != key) k++;
            if (k == s.distinct) seen[s.distinct++] = key;
            sum += avg;
            if (avg < s.min) s.min = avg;
            if (avg > s.max) s.max = avg;
            s.sampled++;
        }
    s.mean = (int)(sum / s.sampled);
    *out = s;
    return 0;
}
