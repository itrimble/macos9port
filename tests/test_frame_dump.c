/* Headless checks for the developer frame-dump hook helpers. */
#include "frame_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void) {
    int next = 0;
    CHECK(!frame_dump_due(NULL, &next, 99999));
    CHECK(!frame_dump_due("", &next, 99999));
    CHECK(!frame_dump_due("20000,80000", &next, 19999));
    CHECK(frame_dump_due("20000,80000", &next, 20000) && next == 1);
    CHECK(!frame_dump_due("20000,80000", &next, 20001));
    CHECK(frame_dump_due("20000,80000", &next, 90000) && next == 2);
    CHECK(!frame_dump_due("20000,80000", &next, 99999999));
    next = 0;
    CHECK(!frame_dump_due("abc", &next, 1000) && next == 0);
    next = -1;
    CHECK(!frame_dump_due("5", &next, 1000));

    char name[64];
    CHECK(frame_dump_name("/tmp/a.ppm", 2, name, sizeof name) == 0 && !strcmp(name, "/tmp/a-2.ppm"));
    CHECK(frame_dump_name("/tmp/a", 1, name, sizeof name) == 0 && !strcmp(name, "/tmp/a-1.ppm"));
    CHECK(frame_dump_name("/tmp/a.ppm", 1, name, 8) != 0);
    CHECK(frame_dump_name("", 1, name, sizeof name) != 0);

    uint32_t fb[8 * 4];
    for (int i = 0; i < 32; i++) fb[i] = 0xFF102030u;
    CHECK(frame_dump_distinct(fb, 8, 4, 0, 0, 8, 4, 16) == 1);       /* flat is blank */
    fb[9] = 0xFF405060u; fb[10] = 0xFF708090u;
    CHECK(frame_dump_distinct(fb, 8, 4, 0, 0, 8, 4, 16) == 3);
    CHECK(frame_dump_distinct(fb, 8, 4, 0, 0, 8, 4, 2) == 2);        /* capped */
    CHECK(frame_dump_distinct(fb, 8, 4, 5, 2, 20, 20, 16) == 1);     /* clipped to the buffer */
    CHECK(frame_dump_distinct(fb, 8, 4, 9, 0, 3, 3, 16) == 0);       /* fully outside */

    char path[] = "/tmp/os999-framedump-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        close(fd);
        CHECK(frame_dump_write(path, fb, 8, 4) == 0);
        FILE *f = fopen(path, "rb");
        char hdr[32] = {0};
        CHECK(f && fread(hdr, 1, 11, f) == 11 && !strncmp(hdr, "P6\n8 4\n255\n", 11));
        unsigned char px[3];
        CHECK(f && fread(px, 1, 3, f) == 3 && px[0] == 0x10 && px[1] == 0x20 && px[2] == 0x30);
        if (f) fclose(f);
        unlink(path);
    }
    /* Surface statistics: flat white is blank, a gradient is not. */
    {
        enum { W = 200, H = 100 };
        static uint8_t img[W * H * 4];
        SurfaceStats st;
        memset(img, 0xFF, sizeof img);
        CHECK(frame_surface_stats(img, W, H, W * 4, &st) == 0);
        CHECK(st.sampled == 48 * 27 && st.distinct == 1 && st.mean == 255 && st.min == 255 && st.max == 255);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t *p = img + ((size_t)y * W + x) * 4;
                p[0] = (uint8_t)(x * 255 / (W - 1)); p[1] = (uint8_t)(y * 255 / (H - 1)); p[2] = 40; p[3] = 255;
            }
        CHECK(frame_surface_stats(img, W, H, W * 4, &st) == 0);
        CHECK(st.distinct > 100 && st.min < 60 && st.max > 150 && st.mean > 60 && st.mean < 200);
        CHECK(frame_surface_stats(img, W, H, W * 3, &st) != 0);      /* stride too small */
        CHECK(frame_surface_stats(NULL, W, H, W * 4, &st) != 0);
        CHECK(frame_surface_stats(img, 0, H, W * 4, &st) != 0);
        CHECK(frame_surface_stats(img, 1, 1, 4, &st) == 0 && st.distinct == 1);
    }
    CHECK(frame_dump_write("", fb, 8, 4) != 0);
    CHECK(frame_dump_write("/nonexistent-dir/x.ppm", fb, 8, 4) != 0);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("frame dump: ok\n");
    return 0;
}
