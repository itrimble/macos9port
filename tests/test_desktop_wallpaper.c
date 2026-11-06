#include "desktop_wallpaper.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put32(unsigned char *b, unsigned n) {
    for (int i = 0; i < 4; i++) b[i] = (unsigned char)(n >> (i * 8));
}
static void fixture(const char *path, int top_down, int bits, int invalid) {
    unsigned char b[70] = {0};
    b[0] = 'B'; b[1] = 'M'; put32(b + 2, sizeof b); put32(b + 10, 54);
    put32(b + 14, 40); put32(b + 18, 2); put32(b + 22, top_down ? (unsigned)-2 : 2);
    b[26] = 1; b[28] = (unsigned char)bits;
    if (invalid) put32(b + 10, 0xfffffff0u);
    /* Each physical row is eight bytes for both 24 and 32bpp. */
    b[54] = 0xff; b[54 + bits / 8 + 1] = 0xff;
    b[62 + 2] = 0xff; b[62 + bits / 8] = 0xff;
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(b, 1, sizeof b, f) == sizeof b); assert(fclose(f) == 0);
}
int main(int argc, char **argv) {
    char dir[] = "/tmp/os999-wallpaper-test.XXXXXX";
    assert(mkdtemp(dir));
    char path[512], pref[512];
    snprintf(path, sizeof path, "%s/01.bmp", dir);
    snprintf(pref, sizeof pref, "%s/preference", dir);
    uint32_t pixels[16] = {0};
    Fb fb = {.px = pixels, .w = 2, .h = 2};
    assert(desktop_wallpaper_count() == 59);
    assert(!strcmp(desktop_wallpaper_name(0), "Halftone Grey"));
    assert(!strcmp(desktop_wallpaper_name(-1), "Plain Desktop"));
    assert(desktop_wallpaper_init(dir, pref) == 0);
    assert(desktop_wallpaper_current() == -1);
    assert(!desktop_wallpaper_draw(&fb));
    fixture(path, 0, 24, 0);
    assert(desktop_wallpaper_select(0) == 1);
    assert(desktop_wallpaper_draw(&fb));
    assert(pixels[0] == 0xffff0000u && pixels[2] == 0xff0000ffu);
    unlink(path); /* Cache must survive source removal; no frame-time file access. */
    assert(desktop_wallpaper_draw(&fb));
    fb.w = fb.h = 4;
    assert(desktop_wallpaper_draw(&fb));
    assert(pixels[0] == 0xffff0000u && pixels[12] == 0xff0000ffu);
    assert(desktop_wallpaper_init(dir, pref) == 0); /* Missing selected image falls back. */
    assert(desktop_wallpaper_current() == 0 && !desktop_wallpaper_draw(&fb));
    fixture(path, 1, 32, 0);
    assert(desktop_wallpaper_init(dir, pref) == 1);
    fb.w = fb.h = 2;
    assert(desktop_wallpaper_draw(&fb));
    assert(pixels[0] == 0xff0000ffu && pixels[2] == 0xffff0000u);
    memset(pixels, 0, sizeof pixels);
    fb.clip = 1; fb.cx = fb.cy = 1; fb.cw = fb.ch = 1;
    assert(desktop_wallpaper_draw(&fb));
    assert(!pixels[0] && !pixels[1] && !pixels[2] && pixels[3]);
    fb.clip = 0;
    fixture(path, 0, 24, 1);
    assert(desktop_wallpaper_select(0) == 0 && !desktop_wallpaper_draw(&fb));
    assert(desktop_wallpaper_select(-2) == -1 && desktop_wallpaper_select(59) == -1);
    assert(desktop_wallpaper_select(-1) == 0);
    assert(desktop_wallpaper_init(dir, pref) == 0 && desktop_wallpaper_current() == -1);
    if (argc == 2) {
        assert(desktop_wallpaper_init(argv[1], NULL) == 0);
        for (int i = 0; i < desktop_wallpaper_count(); i++) {
            assert(desktop_wallpaper_select(i) == 1);
            assert(desktop_wallpaper_draw(&fb));
        }
    }
    desktop_wallpaper_shutdown();
    unlink(path); unlink(pref); rmdir(dir);
    puts("desktop wallpaper checks passed");
    return 0;
}
