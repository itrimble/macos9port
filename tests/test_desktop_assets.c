#include "desktop_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); failures++; } } while (0)

/* Isolate the loader/draw contract; the real framebuffer clipping is tested elsewhere. */
void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t c) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) {
        if (xx >= 0 && yy >= 0 && xx < fb->w && yy < fb->h) fb->px[yy * fb->w + xx] = c;
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (argc != 2) return 1;
    uint32_t pixels[64 * 64] = {0};
    Fb fb = {.px = pixels, .w = 64, .h = 64};
    CHECK(desktop_assets_load(argv[1]));
    CHECK(desktop_asset_draw(&fb, 8, 8, "folder"));
    int visible = 0;
    for (int i = 0; i < 64 * 64; i++) visible += pixels[i] != 0;
    CHECK(visible > 50 && visible <= 1024);
    uint32_t before[64 * 64];
    memcpy(before, pixels, sizeof before);
    CHECK(!desktop_asset_draw(&fb, 0, 0, "not-an-icon"));
    CHECK(memcmp(before, pixels, sizeof before) == 0);
    CHECK(!desktop_assets_load("/nonexistent/os999-icons"));
    CHECK(desktop_asset_draw(&fb, 0, 0, "hard_disk"));
    CHECK(desktop_asset_draw(&fb, -16, -16, "trash_empty"));
    CHECK(desktop_asset_draw(&fb, 0, 0, "system_folder"));
    CHECK(desktop_asset_draw(&fb, 0, 0, "application"));
    CHECK(desktop_asset_draw(&fb, 0, 0, "document"));
    CHECK(!desktop_asset_draw(NULL, 0, 0, "folder"));
    desktop_assets_reset();
    printf("desktop assets: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
