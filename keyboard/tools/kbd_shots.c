/* Headless renders of every posture and skin for review: kbd_shots OUTDIR FONT */
#include "kbd.h"
#include "../src/kbd_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int png_write(const char *path, const uint32_t *px, int w, int h);

static void backdrop(Fb *fb) {
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++)
            fb->px[y * fb->w + x] = ((x / 2 + y / 2) & 1) ? 0xFF5B71A2u : 0xFF56709Cu;
}

static int slot_of(Kbd *k, int id) {
    for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == id) return i;
    return -1;
}

static void center(Kbd *k, int id, float *x, float *y) {
    int s = slot_of(k, id);
    KRect r = s >= 0 ? k->lay.keys[s].r : (KRect){0};
    *x = r.x + r.w / 2; *y = r.y + r.h / 2;
}

static uint32_t T = 1000;
static void settle(Kbd *k, int ms) { for (int i = 0; i < ms / 16; i++) { T += 16; kbd_tick(k, T); } }

static void shot(const char *dir, const char *name, int posture, int split, int skin, int w, int h,
                 float dp, const char *scene) {
    Kbd *k = kbd_create(dp);
    kbd_set_surface(k, w, h);
    kbd_set_posture(k, posture);
    kbd_set_split(k, split);
    kbd_set_skin(k, skin);
    kbd_show(k, 1);
    settle(k, 900);
    float x, y;
    if (strstr(scene, "press")) { center(k, KK_G, &x, &y); kbd_touch_down(k, 1, x, y, T); settle(k, 80); }
    if (strstr(scene, "option")) { center(k, KK_OPT, &x, &y); kbd_touch_down(k, 2, x, y, T); settle(k, 300); }
    if (strstr(scene, "shift")) { center(k, KK_SHIFT, &x, &y); kbd_touch_down(k, 3, x, y, T); kbd_touch_up(k, 3, x, y, T + 50); settle(k, 300); }
    if (strstr(scene, "alts")) { center(k, KK_E, &x, &y); kbd_touch_down(k, 4, x, y, T); settle(k, 700); kbd_touch_move(k, 4, x + k->lay.unit * 1.2f, y - k->lay.key_h * 2, T); settle(k, 60); }
    if (strstr(scene, "numbers")) { kbd_set_layer(k, KBD_LAYER_NUMBERS); settle(k, 900); }
    if (strstr(scene, "hw")) {
        kbd_hw_key(k, 4, 1, T); kbd_hw_key(k, 4, 0, T + 30);
        static const char *const sc[][2] = { {"N", "New Folder"}, {"O", "Open"}, {"W", "Close Window"}, {"Q", "Quit"} };
        kbd_set_shortcuts(k, sc, 4);
        settle(k, 400); kbd_hw_key(k, 227, 1, T); settle(k, 900);
    }
    uint32_t *px = malloc((size_t)w * h * 4);
    Fb fb = { .px = px, .w = w, .h = h };
    backdrop(&fb);
    kbd_draw(k, &fb);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    png_write(path, fb.px, fb.w, fb.h);
    free(px);
    kbd_destroy(k);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    kbd_load_fonts(argc > 2 ? argv[2] : "../fonts/ChicagoFLF.ttf");
    float dp = 2;
    shot(dir, "cover-platinum", KBD_COVER, 1, 0, 390 * 2, 860 * 2, dp, "press");
    shot(dir, "cover-option", KBD_COVER, 1, 0, 390 * 2, 860 * 2, dp, "option");
    shot(dir, "cover-alts", KBD_COVER, 1, 1, 390 * 2, 860 * 2, dp, "alts");
    shot(dir, "cover-numbers", KBD_COVER, 1, 4, 390 * 2, 860 * 2, dp, "numbers");
    shot(dir, "open-split", KBD_OPEN, 1, 0, 900 * 2, 760 * 2, dp, "shift");
    shot(dir, "open-full", KBD_OPEN, 0, 1, 900 * 2, 760 * 2, dp, "press");
    shot(dir, "flex", KBD_FLEX, 0, 3, 900 * 2, 760 * 2, dp, "");
    shot(dir, "hw-sheet", KBD_COVER, 1, 0, 390 * 2, 860 * 2, dp, "hw");
    for (int s = 0; s < kbd_skin_count(); s++) {
        char n[64];
        snprintf(n, sizeof n, "skin-%d", s);
        shot(dir, n, KBD_OPEN, 0, s, 900, 420, 1, "");
    }
    return 0;
}
