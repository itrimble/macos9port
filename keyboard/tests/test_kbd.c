/* Headless checks for the OS999 keyboards. No SDL. */
#include "kbd.h"
#include "kbd_audio.h"
#include "../src/kbd_int.h"
#include "../src/kbd_skin.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t T;
static void tick(Kbd *k, int ms) { for (int i = 0; i < ms; i += 8) { T += 8; kbd_tick(k, T); } }

static Kbd *make(int posture, int split, int w, int h) {
    Kbd *k = kbd_create(1);
    kbd_set_surface(k, w, h);
    kbd_set_posture(k, posture);
    kbd_set_split(k, split);
    kbd_show(k, 1);
    T = 1000;
    kbd_tick(k, T);
    tick(k, 1200);
    KbdEvent e;
    while (kbd_poll_event(k, &e)) {}
    return k;
}

static int slot(Kbd *k, int id) {
    for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == id) return i;
    return -1;
}
static void at(Kbd *k, int id, float *x, float *y) {
    int s = slot(k, id);
    KRect r = s >= 0 ? k->lay.keys[s].r : (KRect){0};
    *x = r.x + r.w / 2; *y = r.y + r.h / 2;
}
static void tap(Kbd *k, int id, int64_t f) {
    float x, y; at(k, id, &x, &y);
    kbd_touch_down(k, f, x, y, T); tick(k, 40); kbd_touch_up(k, f, x, y, T); tick(k, 40);
}

typedef struct { char text[128]; int32_t keys[64]; int nkeys, mods, sounds, buttons, clicks_ctrl; float px, py, sy; } Out;

static Out drain(Kbd *k) {
    Out o;
    memset(&o, 0, sizeof o);
    KbdEvent e;
    while (kbd_poll_event(k, &e)) {
        if (e.kind == KBD_EV_TEXT && strlen(o.text) + strlen(e.text) < sizeof o.text) strcat(o.text, e.text);
        if (e.kind == KBD_EV_KEY && e.down && o.nkeys < 64) { o.keys[o.nkeys++] = e.key; o.mods = e.mods; }
        if (e.kind == KBD_EV_SOUND) o.sounds++;
        if (e.kind == KBD_EV_BUTTON && e.down) { o.buttons++; if (e.mods & KBD_MOD_CTRL) o.clicks_ctrl++; }
        if (e.kind == KBD_EV_POINTER) { o.px += e.dx; o.py += e.dy; }
        if (e.kind == KBD_EV_SCROLL) o.sy += e.dy;
    }
    return o;
}

static int overlap(KRect a, KRect b) {
    return a.x < b.x + b.w - 0.5f && b.x < a.x + a.w - 0.5f && a.y < b.y + b.h - 0.5f && b.y < a.y + a.h - 0.5f;
}

static void test_layouts(void) {
    int sizes[][3] = { { KBD_COVER, 390, 860 }, { KBD_COVER, 320, 700 }, { KBD_OPEN, 900, 760 },
                       { KBD_OPEN, 760, 900 }, { KBD_FLEX, 900, 760 }, { KBD_FLEX, 1200, 800 } };
    for (int s = 0; s < 6; s++)
        for (int split = 0; split < 2; split++)
            for (int layer = 0; layer < 3; layer++) {
                KbdLayout L;
                kbd_layout_build(&L, sizes[s][0], split, layer, sizes[s][1], sizes[s][2], 1);
                CHECK(L.n > 30);
                for (int i = 0; i < L.n; i++) {
                    KRect r = L.keys[i].r;
                    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= sizes[s][1] + 0.5f && r.y + r.h <= sizes[s][2]);
                    CHECK(r.w >= 20 && r.h >= 20);
                    CHECK(r.y >= L.deck.y);
                    if (L.crease.w > 0) CHECK(!overlap(r, L.crease));
                    for (int j = i + 1; j < L.n; j++) CHECK(!overlap(r, L.keys[j].r));
                }
                if (L.pad.w > 0) {
                    CHECK(L.pad.y >= L.deck.y);
                    for (int i = 0; i < L.n; i++) CHECK(!overlap(L.pad, L.keys[i].r));
                }
            }
    KbdLayout L;
    kbd_layout_build(&L, KBD_COVER, 0, 0, 390, 860, 1);
    for (int i = 0; i < L.n; i++) {
        KRect r = L.keys[i].r;
        CHECK(kbd_layout_hit(&L, r.x + r.w / 2, r.y + r.h / 2, 10) == i);
    }
    KRect a = L.keys[1].r, b = L.keys[2].r;          /* a gap between neighbours */
    if (a.y == b.y) {
        int h = kbd_layout_hit(&L, (a.x + a.w + b.x) / 2, a.y + a.h / 2, 10);
        CHECK(h == 1 || h == 2);
    }
    CHECK(kbd_layout_hit(&L, 195, 10, 10) == -1);     /* far above the keyboard */
}

static void test_typing(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    tap(k, KK_H, 1); tap(k, KK_I, 2);
    Out o = drain(k);
    CHECK(strcmp(o.text, "hi") == 0);
    CHECK(o.nkeys == 2 && o.keys[0] == 'h' && o.keys[1] == 'i');
    CHECK(o.sounds == 4);                              /* press + release each */
    /* shift latches for one letter */
    tap(k, KK_SHIFT, 3); tap(k, KK_A, 4); tap(k, KK_B, 5);
    o = drain(k);
    CHECK(strcmp(o.text, "Ab") == 0);
    /* double-tap shift is caps lock until shift again */
    tap(k, KK_SHIFT, 6); tap(k, KK_SHIFT, 7);
    CHECK(k->caps == 1);
    tap(k, KK_A, 8); tap(k, KK_B, 9);
    tap(k, KK_SHIFT, 10);
    CHECK(k->caps == 0);
    tap(k, KK_C, 11);
    o = drain(k);
    CHECK(strcmp(o.text, "ABc") == 0);
    /* held shift as a chord: no latch afterwards */
    float sx, sy, ax, ay; at(k, KK_SHIFT, &sx, &sy); at(k, KK_D, &ax, &ay);
    kbd_touch_down(k, 20, sx, sy, T); tick(k, 30);
    kbd_touch_down(k, 21, ax, ay, T); tick(k, 30); kbd_touch_up(k, 21, ax, ay, T); tick(k, 30);
    kbd_touch_up(k, 20, sx, sy, T); tick(k, 30);
    tap(k, KK_E, 22);
    o = drain(k);
    CHECK(strcmp(o.text, "De") == 0);
    kbd_destroy(k);
}

static void test_rollover_and_slide(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    float tx, ty, hx, hy;
    at(k, KK_T, &tx, &ty); at(k, KK_H, &hx, &hy);
    kbd_touch_down(k, 1, tx, ty, T); tick(k, 20);
    kbd_touch_down(k, 2, hx, hy, T); tick(k, 20);     /* second thumb lands first-letter commits */
    kbd_touch_up(k, 1, tx, ty, T);
    kbd_touch_up(k, 2, hx, hy, T); tick(k, 20);
    Out o = drain(k);
    CHECK(strcmp(o.text, "th") == 0);
    float rx, ry; at(k, KK_R, &rx, &ry);              /* land on r, slide to t, lift */
    kbd_touch_down(k, 3, rx, ry, T); tick(k, 30);
    kbd_touch_move(k, 3, tx, ty, T); tick(k, 30);
    kbd_touch_up(k, 3, tx, ty, T); tick(k, 20);
    o = drain(k);
    CHECK(strcmp(o.text, "t") == 0);
    kbd_destroy(k);
}

static void test_option_dead_keys(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    tap(k, KK_OPT, 1); tap(k, KK_E, 2); tap(k, KK_E, 3);
    Out o = drain(k);
    CHECK(strcmp(o.text, "\xC3\xA9") == 0);            /* é */
    tap(k, KK_OPT, 4); tap(k, KK_U, 5); tap(k, KK_SHIFT, 6); tap(k, KK_O, 7);
    o = drain(k);
    CHECK(strcmp(o.text, "\xC3\x96") == 0);            /* Ö */
    tap(k, KK_OPT, 8); tap(k, KK_N, 9); tap(k, KK_X, 10);
    o = drain(k);
    CHECK(strcmp(o.text, "\xCB\x9Cx") == 0);           /* accent then the letter */
    tap(k, KK_OPT, 11); tap(k, KK_G, 12);
    o = drain(k);
    CHECK(strcmp(o.text, "\xC2\xA9") == 0);            /* © */
    tap(k, KK_OPT, 13); tap(k, KK_I, 14); tap(k, KK_DELETE, 15);
    o = drain(k);
    CHECK(o.text[0] == 0 && k->dead == 0);             /* delete cancels a pending accent */
    char out[16];
    kbd_compose(DEAD_ACUTE, " ", out, sizeof out);
    CHECK(strcmp(out, "\xC2\xB4") == 0);
    CHECK(kbd_keydefs[KK_K].optshift == NULL);         /* no logo glyph, ever */
    kbd_destroy(k);
}

static void test_long_press_and_repeat(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    float x, y; at(k, KK_E, &x, &y);
    kbd_touch_down(k, 1, x, y, T); tick(k, 520);
    CHECK(k->alt_open && k->alt_n == 4);
    float cw = k->alt_rect.w / k->alt_n;
    kbd_touch_move(k, 1, k->alt_rect.x + cw * 1.5f, y, T); tick(k, 30);
    kbd_touch_up(k, 1, k->alt_rect.x + cw * 1.5f, y, T); tick(k, 20);
    Out o = drain(k);
    CHECK(strcmp(o.text, "\xC3\xA9") == 0);
    CHECK(!k->alt_open);
    at(k, KK_DELETE, &x, &y);
    kbd_touch_down(k, 2, x, y, T); tick(k, 2000); kbd_touch_up(k, 2, x, y, T);
    o = drain(k);
    int plain = 0;
    for (int i = 0; i < o.nkeys; i++) plain += o.keys[i] == KBD_K_BACKSPACE;
    CHECK(plain > 12);                                 /* 1 + repeats at the classic rate */
    CHECK(o.mods & KBD_MOD_OPT);                       /* ended in word-delete */
    kbd_destroy(k);
}

static void test_double_space_and_layers(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    tap(k, KK_O, 1); tap(k, KK_K, 2); tap(k, KK_SPACE, 3); tap(k, KK_SPACE, 4);
    Out o = drain(k);
    CHECK(strcmp(o.text, "ok . ") == 0 || strstr(o.text, ".") != NULL);
    tap(k, KK_LAYER_NUM, 5); tick(k, 600);
    CHECK(k->layer == KBD_LAYER_NUMBERS);
    tap(k, KK_1, 6); tap(k, KK_SPACE, 7); tick(k, 600);
    CHECK(k->layer == KBD_LAYER_LETTERS);              /* space returns to letters */
    kbd_destroy(k);
}

static void test_trackpad(void) {
    Kbd *k = make(KBD_FLEX, 0, 900, 760);
    KRect p = k->lay.pad;
    CHECK(p.w > 0 && k->lay.pad_button.w > 0);
    float cx = p.x + p.w / 2, cy = p.y + p.h / 2;
    kbd_touch_down(k, 1, cx, cy, T);
    for (int i = 0; i < 10; i++) { tick(k, 16); kbd_touch_move(k, 1, cx + i * 4, cy, T); }
    kbd_touch_up(k, 1, cx + 36, cy, T); tick(k, 20);
    Out o = drain(k);
    CHECK(o.px > 36 && o.buttons == 0);               /* moved, accelerated, no click */
    kbd_touch_down(k, 2, cx, cy, T); tick(k, 60); kbd_touch_up(k, 2, cx, cy, T); tick(k, 600);
    o = drain(k);
    CHECK(o.buttons == 1);                             /* tap to click */
    kbd_touch_down(k, 3, cx, cy, T); kbd_touch_down(k, 4, cx + 40, cy, T); tick(k, 80);
    kbd_touch_up(k, 3, cx, cy, T); kbd_touch_up(k, 4, cx + 40, cy, T); tick(k, 600);
    o = drain(k);
    CHECK(o.clicks_ctrl == 1);                         /* two-finger tap: control-click */
    kbd_touch_down(k, 5, cx, cy, T); kbd_touch_down(k, 6, cx + 40, cy, T);
    for (int i = 1; i <= 8; i++) { tick(k, 16); kbd_touch_move(k, 5, cx, cy + i * 8, T); kbd_touch_move(k, 6, cx + 40, cy + i * 8, T); }
    kbd_touch_up(k, 5, cx, cy + 64, T); kbd_touch_up(k, 6, cx + 40, cy + 64, T);
    o = drain(k);
    float during = o.sy;
    tick(k, 3500);
    o = drain(k);
    CHECK(during > 2 && o.sy > 0.5f);                  /* scroll, then momentum */
    CHECK(k->pad.vy == 0);                             /* and the glide ends */
    KRect b = k->lay.pad_button;
    kbd_touch_down(k, 7, b.x + b.w / 2, b.y + b.h / 2, T); tick(k, 50);
    kbd_touch_up(k, 7, b.x + b.w / 2, b.y + b.h / 2, T);
    o = drain(k);
    CHECK(o.buttons == 1 && o.sounds == 2);
    kbd_destroy(k);
}

static void test_space_cursor(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    float x, y; at(k, KK_SPACE, &x, &y);
    kbd_touch_down(k, 1, x, y, T); tick(k, 450);
    CHECK(k->cursor_mode);
    for (int i = 1; i <= 6; i++) { tick(k, 16); kbd_touch_move(k, 1, x + i * 10, y, T); }
    kbd_touch_down(k, 2, 40, y - 100, T); kbd_touch_up(k, 2, 40, y - 100, T);
    kbd_touch_up(k, 1, x + 60, y, T); tick(k, 30);
    Out o = drain(k);
    CHECK(o.px > 40 && o.buttons == 1 && o.text[0] == 0);
    CHECK(!k->cursor_mode);
    kbd_destroy(k);
}

static void test_hardware(void) {
    Kbd *k = make(KBD_COVER, 1, 390, 860);
    CHECK(kbd_visible(k));
    kbd_hw_key(k, 4, 1, T); kbd_hw_key(k, 4, 0, T + 20); tick(k, 600);
    CHECK(kbd_hw_active(k) && !kbd_visible(k));
    CHECK(kbd_occluded_height(k) > 0 && kbd_occluded_height(k) < 60);   /* strip only */
    static const char *const sc[][2] = { { "N", "New Folder" } };
    kbd_set_shortcuts(k, sc, 1);
    kbd_hw_key(k, 227, 1, T); tick(k, 800);
    CHECK(k->sheet);
    kbd_hw_key(k, 227, 0, T); tick(k, 20);
    CHECK(!k->sheet);
    kbd_settings(k)->pc_modifiers = 1;
    kbd_hw_key(k, 226, 1, T);                          /* PC Alt sits where Command does */
    CHECK(k->hw_mods & KBD_MOD_CMD);
    kbd_hw_key(k, 226, 0, T);
    KRect b = kbd_hud_button(k);
    kbd_touch_down(k, 1, b.x + 4, b.y + 4, T); kbd_touch_up(k, 1, b.x + 4, b.y + 4, T); tick(k, 600);
    CHECK(!kbd_hw_active(k) && kbd_visible(k));
    kbd_destroy(k);
}

static void test_animation(void) {
    Kbd *k = make(KBD_OPEN, 1, 900, 760);
    CHECK(!kbd_animating(k));
    kbd_set_posture(k, KBD_FLEX);
    tick(k, 16);
    CHECK(kbd_animating(k));
    int qs = slot(k, KK_Q);
    KRect mid = kbd_anim_rect(k, qs), end = k->lay.keys[qs].r;
    CHECK(fabsf(mid.x - end.x) + fabsf(mid.y - end.y) > 1);   /* keys travel, not jump */
    tick(k, 2500);
    CHECK(!kbd_animating(k));
    mid = kbd_anim_rect(k, qs);
    CHECK(fabsf(mid.x - end.x) < 0.01f && fabsf(mid.y - end.y) < 0.01f);
    kbd_settings(k)->reduce_motion = 1;
    kbd_set_posture(k, KBD_OPEN);
    tick(k, 16);
    CHECK(!kbd_animating(k));
    /* frame-rate independence: the same 400 ms at 60 Hz and 144 Hz */
    Spring a = {0, 0}, b = {0, 0};
    for (int i = 0; i < 24; i++) spring_step(&a, 1, 260, 28, 1.0f / 60);
    for (int i = 0; i < 58; i++) spring_step(&b, 1, 260, 28, 0.4f / 58);
    CHECK(fabsf(a.x - b.x) < 0.01f);
    kbd_destroy(k);
}

static void test_audio(void) {
    static float buf[12000], again[12000];
    for (int f = 0; f < KBD_SW_COUNT; f++)
        for (int s = 0; s < KBD_SND_COUNT; s++) {
            int n = kbd_synth_render(f, s, 0, buf, 12000);
            CHECK(n > 600 && n < 12000);                /* audible, and short */
            float peak = 0, dc = 0;
            for (int i = 0; i < n; i++) { peak = fmaxf(peak, fabsf(buf[i])); dc += buf[i]; }
            CHECK(peak > 0.2f && peak <= 0.65f);
            CHECK(fabsf(dc / n) < 0.01f);
            kbd_synth_render(f, s, 0, again, 12000);
            CHECK(memcmp(buf, again, sizeof(float) * (size_t)n) == 0);   /* deterministic */
            kbd_synth_render(f, s, 1, again, 12000);
            CHECK(memcmp(buf, again, sizeof(float) * (size_t)n) != 0);         /* variants differ */
        }
    KbdSoundBank *bank = kbd_bank_create();
    KbdMixer m;
    kbd_mixer_init(&m);
    for (int i = 0; i < 40; i++)
        kbd_mixer_trigger(&m, kbd_bank_get(bank, KBD_SW_ALPS, KBD_SND_WIDE_PRESS, i), 1, 1, i % 2 ? 1 : -1);
    CHECK(kbd_mixer_active(&m) == KBD_MIX_VOICES);     /* steals instead of growing */
    int16_t pcm[2 * 4800];
    kbd_mixer_render(&m, pcm, 4800);
    int clip = 0;
    for (int i = 0; i < 9600; i++) clip += pcm[i] >= 32767 || pcm[i] <= -32767;
    CHECK(clip == 0);                                  /* soft knee under a pile-up */
    kbd_mixer_init(&m);
    kbd_mixer_trigger(&m, kbd_bank_get(bank, KBD_SW_DOME, KBD_SND_PRESS, 0), 1, 1, -1);
    kbd_mixer_render(&m, pcm, 2400);
    long l = 0, r = 0;
    for (int i = 0; i < 2400; i++) { l += labs(pcm[2 * i]); r += labs(pcm[2 * i + 1]); }
    CHECK(l > 100 * r + 1);                            /* hard left is left */
    kbd_bank_destroy(bank);
    CHECK(kbd_skin_count() >= 8);
    for (int i = 0; i < kbd_skin_count(); i++) CHECK(kbd_skin_get(i)->sound < KBD_SW_COUNT);
}

static void test_draw_smoke(void) {
    for (int p = 0; p < KBD_POSTURE_COUNT; p++)
        for (int s = 0; s < kbd_skin_count(); s++) {
            Kbd *k = make(p, 1, 400, 700);
            kbd_set_skin(k, s);
            uint32_t *px = calloc(400 * 700, 4);
            Fb fb = { .px = px, .w = 400, .h = 700 };
            kbd_draw(k, &fb);
            int changed = 0;
            for (int i = 0; i < 400 * 700; i++) changed += px[i] != 0;
            CHECK(changed > 400 * 100);
            CHECK(px[0] == 0);                         /* nothing drawn over the top of the screen */
            free(px);
            kbd_destroy(k);
        }
}

int main(void) {
    test_layouts();
    test_typing();
    test_rollover_and_slide();
    test_option_dead_keys();
    test_long_press_and_repeat();
    test_double_space_and_layers();
    test_trackpad();
    test_space_cursor();
    test_hardware();
    test_animation();
    test_audio();
    test_draw_smoke();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
