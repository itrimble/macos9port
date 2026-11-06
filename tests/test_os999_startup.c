#include "os999_startup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

void fb_init(Fb *fb, uint32_t *px, int w, int h) {
    *fb = (Fb){.px = px, .w = w, .h = h};
}
void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < fb->w && yy < fb->h)
            fb->px[(size_t)yy * (size_t)fb->w + (size_t)xx] = color;
}
void fb_frame(Fb *fb, int x, int y, int w, int h, uint32_t color) {
    for (int xx = x; xx < x + w; xx++) {
        fb_fill(fb, xx, y, 1, 1, color);
        fb_fill(fb, xx, y + h - 1, 1, 1, color);
    }
    for (int yy = y; yy < y + h; yy++) {
        fb_fill(fb, x, yy, 1, 1, color);
        fb_fill(fb, x + w - 1, yy, 1, 1, color);
    }
}
void fb_text(Fb *fb, int x, int y, uint32_t color, const char *s) {
    (void)fb; (void)x; (void)y; (void)color; (void)s;
}
int fb_text_width(const char *s) { return (int)strlen(s) * 8; }
int typeface_ready(void) { return 0; }
int typeface_startup_ready(void) { return 0; }
int typeface_width(int body, const char *s, int n) { (void)body; (void)s; return n * 8; }
void typeface_draw(void *fb, int body, int x, int y, uint32_t color, const char *s, int n) {
    (void)fb; (void)body; (void)x; (void)y; (void)color; (void)s; (void)n;
}

int main(void) {
    char root[] = "/tmp/os999-startup-prefs-XXXXXX";
    char *directory = mkdtemp(root);
    CHECK(directory != NULL);
    if (!directory) return 1;
    CHECK(os999_startup_pref_read(root) == 1);
    CHECK(os999_startup_pref_write(root, 0) == 0);
    CHECK(os999_startup_pref_read(root) == 0);
    CHECK(os999_startup_pref_write(root, 1) == 0);
    CHECK(os999_startup_pref_read(root) == 1);
    char preference[256];
    snprintf(preference, sizeof preference, "%s/.os999-startup", root);
    CHECK(unlink(preference) == 0);
    CHECK(rmdir(root) == 0);
    uint32_t image[256 * 256], desktop[256 * 256];
    memset(image, 0, sizeof image);
    for (size_t i = 0; i < sizeof desktop / sizeof desktop[0]; i++) desktop[i] = 0xFF203040u;
    Os999Startup s;
    uint32_t start = 0xFFFFFF00u;
    os999_startup_begin(&s, start, 0);
    CHECK(s.phase == OS999_STARTUP_SPIN && os999_startup_active(&s));
    os999_startup_frame(&s, start + 3299u, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_frame(&s, start + 3300u, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_SETTLE);
    os999_startup_frame(&s, start + 4800u, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_HOLD);
    os999_startup_frame(&s, start + 4990u, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_REVEAL);
    os999_startup_frame(&s, start + 5240u, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_DONE && !os999_startup_active(&s));
    CHECK(image[0] == desktop[0]);

    os999_startup_begin(&s, 100, 1);
    os999_startup_frame(&s, 400, 1, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_SETTLE && s.reduced_motion);
    os999_startup_fail(&s, 2000);
    CHECK(s.phase == OS999_STARTUP_ERROR);
    os999_startup_frame(&s, 32000, 0, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_DONE);
    /* An inactive/finished intro must never paint itself over the desktop,
       including callers that have no saved desktop frame. */
    image[0] = 0xFFABCDEFu;
    os999_startup_frame(&s, 32001, 1, image, NULL, 256, 256);
    CHECK(image[0] == 0xFFABCDEFu);
    s.phase = OS999_STARTUP_INACTIVE;
    os999_startup_frame(&s, 32002, 1, image, desktop, 256, 256);
    CHECK(image[0] == desktop[0]);
    /* Replay allocates fresh renderer scratch after completed/aborted runs. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 50, 0, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_release();
    os999_startup_release();
    os999_startup_begin(&s, 0, 1);
    os999_startup_frame(&s, 50, 0, image, desktop, 256, 256);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_release();
    printf("OS999 startup: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
