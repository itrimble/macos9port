/* Showcase stage: the real OS999 desktop (session_paint) and the real
 * keyboard, rendered for phone screens, driven over stdin by showcase.py.
 *
 * Commands (one per line):
 *   audio PATH                       append 48 kHz s16 stereo switch sounds here
 *   slot I W H DP SCALE POSTURE SPLIT SKIN WALL   a phone screen (native px, px/pt)
 *   rotate I W H TURNS POSTURE       the device turned; keyboard morphs
 *   posture I P | split I S | skin I N | show I S | text I STRING
 *   down I F KEY | move I F KEY | up I F       KEY: a, A, space, shift, delete,
 *   downxy I F X Y | movexy I F X Y           return, 123, ABC, opt, cmd, a glyph
 *   pad I F FX FY | padmove I F FX FY         fractions of the trackpad
 *   nudge I F DX DY                  move a finger by native px
 *   alt I F N                        slide finger to accent N
 *   force I F P                      contact pressure
 *   copytext I J                     screen J continues screen I's document
 *   step MS                          advance every screen and the audio
 *   render I                         -> "FRAME w h label\n" + w*h*4 bytes BGRA
 */
#include "ospi.h"
#include "session_int.h"
#include "desktop_wallpaper.h"
#include "kbd.h"
#include "kbd_audio.h"
#include "../../src/kbd_int.h"
#include "../../src/kbd_gfx.h"
#include "../../src/kbd_skin.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { SLOTS = 4 };

typedef struct {
    int used, w, h, wall;
    float dp, scale;
    Kbd *k;
    Session *s;
    char root[256], text[OSPI_TEXT];
    float fx[16], fy[16];
    float px, py;
    int pointer;
    const char *feel;
    uint32_t feel_at;
} Slot;

static Slot slots[SLOTS];
static uint32_t now = 1000;
static KbdSoundBank *bank;
static KbdMixer mix;
static FILE *audio;
static int painted_wall = -2;

static int disp_w(const Slot *s) { return (int)lroundf(s->w * s->scale); }
static int disp_h(const Slot *s) { return (int)lroundf(s->h * s->scale); }

static void set_wall(Slot *s) {
    if (painted_wall == s->wall) return;
    char pref[300];
    snprintf(pref, sizeof pref, "%s/.os999-wallpaper", s->root);
    desktop_wallpaper_init(NULL, pref);
    painted_wall = s->wall;
}

static void make_slot(int i, int w, int h, float dp, float scale, int posture, int split, int skin, int wall) {
    Slot *s = &slots[i];
    if (s->k) kbd_destroy(s->k);
    if (s->s) session_destroy(s->s);
    memset(s, 0, sizeof *s);
    s->used = 1; s->w = w; s->h = h; s->dp = dp; s->scale = scale; s->wall = wall;
    snprintf(s->root, sizeof s->root, "/tmp/os999-showcase/%d", i);
    mkdir("/tmp/os999-showcase", 0755);
    mkdir(s->root, 0755);
    volume_seed(s->root);
    char pref[300];
    snprintf(pref, sizeof pref, "%s/.os999-wallpaper", s->root);
    FILE *f = fopen(pref, "w");
    if (f) { fprintf(f, "%02d\n", wall); fclose(f); }
    painted_wall = -2;
    s->s = session_create(s->root);
    while (s->s && s->s->nwin > 0) close_index(s->s, 0);
    session_open_text(s->s, "Untitled 1", "", "");
    s->k = kbd_create(dp * scale);
    kbd_set_surface(s->k, disp_w(s), disp_h(s));
    kbd_set_posture(s->k, posture);
    kbd_set_split(s->k, split);
    kbd_set_skin(s->k, skin);
    kbd_show(s->k, 1);
    kbd_tick(s->k, now);
    s->px = disp_w(s) * 0.55f;
    s->py = disp_h(s) * 0.25f;
}

static int key_slot(Kbd *k, const char *name) {
    static const struct { const char *n; int id; } named[] = {
        { "space", KK_SPACE }, { "shift", KK_SHIFT }, { "delete", KK_DELETE }, { "return", KK_RETURN },
        { "123", KK_LAYER_NUM }, { "ABC", KK_LAYER_ABC }, { "#+=", KK_LAYER_SYM }, { "opt", KK_OPT },
        { "cmd", KK_CMD }, { "caps", KK_CAPS }, { "tab", KK_TAB }, { "esc", KK_ESC } };
    for (size_t j = 0; j < sizeof named / sizeof named[0]; j++)
        if (!strcmp(name, named[j].n))
            for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == named[j].id) return i;
    char low[8];
    snprintf(low, sizeof low, "%s", name);
    if (low[0] >= 'A' && low[0] <= 'Z' && !low[1]) low[0] = (char)(low[0] + 32);
    for (int i = 0; i < k->lay.n; i++) {
        const KbdSlot *s = &k->lay.keys[i];
        if (s->id == KK_GLYPH ? !strcmp(s->glyph, name) : !strcmp(kbd_keydefs[s->id].label, low)) return i;
    }
    return -1;
}

static void key_center(Slot *s, const char *name, float *x, float *y) {
    int i = key_slot(s->k, name);
    KRect r = i >= 0 ? s->k->lay.keys[i].r : (KRect){ 0, 0, 0, 0 };
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

static void text_backspace(Slot *s, int word) {
    int n = (int)strlen(s->text);
    do {
        while (n > 0 && ((unsigned char)s->text[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
    } while (word && n > 0 && s->text[n - 1] != ' ');
    s->text[n] = 0;
}

static const char *const feel_names[KBD_HAPTIC_COUNT] = {
    "key", "wide key", "modifier", "detent", "tap", "click", "release", "force click", "trackpad mode" };

static void drain(Slot *s) {
    KbdEvent e;
    while (kbd_poll_event(s->k, &e)) {
        if (e.kind == KBD_EV_TEXT) {
            if (strlen(s->text) + strlen(e.text) < sizeof s->text - 1) strcat(s->text, e.text);
        } else if (e.kind == KBD_EV_KEY && e.down) {
            if (e.key == KBD_K_BACKSPACE) text_backspace(s, (e.mods & KBD_MOD_OPT) != 0);
            if (e.key == KBD_K_RETURN) strcat(s->text, "\n");
        } else if (e.kind == KBD_EV_POINTER) {
            s->pointer = 1;
            s->px = fminf(fmaxf(s->px + e.dx, 0), (float)disp_w(s) - 2);
            s->py = fminf(fmaxf(s->py + e.dy, 0), (float)disp_h(s) - 2);
        } else if (e.kind == KBD_EV_HAPTIC && e.haptic >= 0 && e.haptic < KBD_HAPTIC_COUNT) {
            s->feel = feel_names[e.haptic];
            s->feel_at = now;
        } else if (e.kind == KBD_EV_SOUND && bank) {
            kbd_mixer_trigger(&mix, kbd_bank_get(bank, kbd_skin_sound(kbd_skin(s->k)), e.sound, e.variant),
                              e.pitch, e.gain * 0.9f, e.pan);
        }
    }
}

static void step(int ms) {
    static double frac;
    for (int t = 0; t < ms; t += 4) {
        now += 4;
        for (int i = 0; i < SLOTS; i++) if (slots[i].used) { kbd_tick(slots[i].k, now); drain(&slots[i]); }
    }
    double want = KBD_SYNTH_RATE * ms / 1000.0 + frac;
    int frames = (int)want;
    frac = want - frames;
    if (audio && frames > 0) {
        int16_t *pcm = malloc((size_t)frames * 4);
        kbd_mixer_render(&mix, pcm, frames);
        fwrite(pcm, 4, (size_t)frames, audio);
        free(pcm);
    }
}

/* Bilinear upscale of the logical desktop into the displayed screen. */
static void blit_scaled(Fb *dst, const uint32_t *src, int sw, int sh, int dh) {
    float sx = (float)sw / dst->w, sy = (float)sh / dh;
    for (int y = 0; y < dh && y < dst->h; y++) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        if (y0 < 0) { y0 = 0; ty = 0; }
        int y1 = y0 + 1 < sh ? y0 + 1 : y0;
        for (int x = 0; x < dst->w; x++) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            if (x0 < 0) { x0 = 0; tx = 0; }
            int x1 = x0 + 1 < sw ? x0 + 1 : x0;
            uint32_t a = src[y0 * sw + x0], b = src[y0 * sw + x1], c = src[y1 * sw + x0], d = src[y1 * sw + x1];
            uint32_t o = 0xFF000000u;
            for (int k = 0; k < 24; k += 8) {
                float top = (a >> k & 255) * (1 - tx) + (b >> k & 255) * tx;
                float bot = (c >> k & 255) * (1 - tx) + (d >> k & 255) * tx;
                o |= (uint32_t)(top * (1 - ty) + bot * ty + 0.5f) << k;
            }
            dst->px[(size_t)y * dst->w + x] = o;
        }
    }
}

static void arrow(Fb *fb, float x, float y, float s) {
    float o = s * 0.12f;
    g_triangle(fb, x - o, y - o * 2, x - o, y + 16 * s + o * 2, x + 11.5f * s + o * 2, y + 11.5f * s, 0xFFFFFFFFu, 1);
    g_line(fb, x + 4.5f * s, y + 11 * s, x + 7.5f * s, y + 17.5f * s, 4.6f * s, 0xFFFFFFFFu, 1);
    g_triangle(fb, x, y, x, y + 15 * s, x + 10.5f * s, y + 10.5f * s, 0xFF000000u, 1);
    g_line(fb, x + 4.5f * s, y + 11 * s, x + 7.5f * s, y + 17 * s, 2.2f * s, 0xFF000000u, 1);
}

static void render(int i) {
    Slot *s = &slots[i];
    int W = disp_w(s), H = disp_h(s);
    uint32_t *px = calloc((size_t)W * H, 4);
    Fb fb = { .px = px, .w = W, .h = H };
    float f = s->dp * s->scale;                       /* displayed px per OS point */
    int flex = kbd_posture(s->k) == KBD_FLEX;
    int os_h = flex ? H / 2 : H - kbd_occluded_height(s->k);
    if (os_h < (int)(120 * f)) os_h = (int)(120 * f);
    int lw = (int)lroundf(W / f), lh = (int)lroundf(os_h / f);
    if (lw < 200) lw = 200;
    if (lh < 200) lh = 200;
    Session *os = s->s;
    os->sw = lw;
    os->sh = lh;
    for (int w = 0; w < os->nwin; w++) {
        Window *win = &os->win[w];
        if (win->kind != WIN_TEXT) continue;
        win->x = 6; win->y = 28; win->w = lw - 12; win->h = lh - 34 - (lh > 420 ? 54 : 0);
        win->shade_h = win->h;
        snprintf(win->text, sizeof win->text, "%s", s->text);
    }
    set_wall(s);
    uint32_t *lp = calloc((size_t)lw * lh, 4);
    session_paint(os, lp, lw, lh);
    blit_scaled(&fb, lp, lw, lh, os_h);
    free(lp);
    if (flex) g_rrect(&fb, 0, (float)os_h, (float)W, (float)(H - os_h), 0, 0xFF111114u, 0xFF111114u, 1);
    kbd_draw(s->k, &fb);
    if (s->pointer) arrow(&fb, s->px, s->py, f);
    const char *label = s->feel && now - s->feel_at < 450 ? s->feel : "-";
    printf("FRAME %d %d %s\n", W, H, label);
    fflush(stdout);
    fwrite(px, 4, (size_t)W * H, stdout);
    fflush(stdout);
    free(px);
}

static Slot *slot_arg(const char *a) {
    int i = atoi(a);
    return i >= 0 && i < SLOTS && slots[i].used ? &slots[i] : NULL;
}

int main(int argc, char **argv) {
    kbd_load_fonts(argc > 1 ? argv[1] : "fonts/ChicagoFLF.ttf");
    bank = kbd_bank_create();
    kbd_mixer_init(&mix);
    mix.master = 0.9f;
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        char *v[16] = { 0 };
        int n = 0;
        char *rest = NULL;
        if (!strncmp(line, "text ", 5)) {          /* text I STRING: keep the spaces */
            char *sp = strchr(line + 5, ' ');
            if (sp) { *sp = 0; rest = sp + 1; rest[strcspn(rest, "\n")] = 0; }
            v[n++] = line;
            line[4] = 0;
            v[n++] = line + 5;
        } else {
            for (char *p = strtok(line, " \n"); p && n < 16; p = strtok(NULL, " \n")) v[n++] = p;
        }
        if (!n) continue;
        Slot *s = n > 1 ? slot_arg(v[1]) : NULL;
        if (!strcmp(v[0], "quit")) break;
        else if (!strcmp(v[0], "audio") && n > 1) audio = fopen(v[1], "wb");
        else if (!strcmp(v[0], "slot") && n >= 10)
            make_slot(atoi(v[1]), atoi(v[2]), atoi(v[3]), (float)atof(v[4]), (float)atof(v[5]), atoi(v[6]),
                      atoi(v[7]), atoi(v[8]), atoi(v[9]));
        else if (!strcmp(v[0], "step") && n > 1) step(atoi(v[1]));
        else if (!strcmp(v[0], "render") && s) { render(atoi(v[1])); continue; }
        else if (!s) { }
        else if (!strcmp(v[0], "rotate") && n >= 6) {
            s->w = atoi(v[2]); s->h = atoi(v[3]);
            kbd_rotate(s->k, disp_w(s), disp_h(s), atoi(v[4]), atoi(v[5]));
            s->px = disp_w(s) * 0.5f; s->py = disp_h(s) * 0.25f;
        }
        else if (!strcmp(v[0], "copytext") && n > 2 && slot_arg(v[2]))
            snprintf(slot_arg(v[2])->text, sizeof s->text, "%s", s->text);
        else if (!strcmp(v[0], "posture") && n > 2) kbd_set_posture(s->k, atoi(v[2]));
        else if (!strcmp(v[0], "split") && n > 2) kbd_set_split(s->k, atoi(v[2]));
        else if (!strcmp(v[0], "skin") && n > 2) kbd_set_skin(s->k, atoi(v[2]));
        else if (!strcmp(v[0], "show") && n > 2) kbd_show(s->k, atoi(v[2]));
        else if (!strcmp(v[0], "text")) {
            char *o = s->text;                         /* "\\n" in the line is a newline */
            for (const char *q = rest ? rest : ""; *q && o < s->text + sizeof s->text - 1; q++) {
                if (q[0] == '\\' && q[1] == 'n') { *o++ = '\n'; q++; }
                else *o++ = *q;
            }
            *o = 0;
        }
        else if (!strcmp(v[0], "nudge") && n > 4) {    /* move a finger by native px */
            int fid = atoi(v[2]) & 15;
            s->fx[fid] += (float)atof(v[3]) * s->scale;
            s->fy[fid] += (float)atof(v[4]) * s->scale;
            kbd_touch_move(s->k, fid, s->fx[fid], s->fy[fid], now);
        }
        else if ((!strcmp(v[0], "down") || !strcmp(v[0], "move")) && n > 3) {
            int fid = atoi(v[2]) & 15;
            key_center(s, v[3], &s->fx[fid], &s->fy[fid]);
            if (v[0][0] == 'd') kbd_touch_down(s->k, fid, s->fx[fid], s->fy[fid], now);
            else kbd_touch_move(s->k, fid, s->fx[fid], s->fy[fid], now);
        } else if ((!strcmp(v[0], "downxy") || !strcmp(v[0], "movexy")) && n > 4) {
            int fid = atoi(v[2]) & 15;
            s->fx[fid] = (float)atof(v[3]) * s->scale;
            s->fy[fid] = (float)atof(v[4]) * s->scale;
            if (v[0][0] == 'd') kbd_touch_down(s->k, fid, s->fx[fid], s->fy[fid], now);
            else kbd_touch_move(s->k, fid, s->fx[fid], s->fy[fid], now);
        } else if ((!strcmp(v[0], "pad") || !strcmp(v[0], "padmove")) && n > 4) {
            int fid = atoi(v[2]) & 15;
            KRect p = s->k->lay.pad;
            s->fx[fid] = p.x + p.w * (float)atof(v[3]);
            s->fy[fid] = p.y + p.h * (float)atof(v[4]);
            if (!strcmp(v[0], "pad")) kbd_touch_down(s->k, fid, s->fx[fid], s->fy[fid], now);
            else kbd_touch_move(s->k, fid, s->fx[fid], s->fy[fid], now);
        } else if (!strcmp(v[0], "alt") && n > 3) {
            int fid = atoi(v[2]) & 15, a = atoi(v[3]);
            KRect r = s->k->alt_rect;
            float cw = s->k->alt_n > 0 ? r.w / s->k->alt_n : 0;
            s->fx[fid] = r.x + cw * (a + 0.5f);
            kbd_touch_move(s->k, fid, s->fx[fid], s->fy[fid], now);
        } else if (!strcmp(v[0], "force") && n > 3) {
            kbd_touch_force(s->k, atoi(v[2]) & 15, (float)atof(v[3]), now);
        } else if (!strcmp(v[0], "up") && n > 2) {
            int fid = atoi(v[2]) & 15;
            kbd_touch_up(s->k, fid, s->fx[fid], s->fy[fid], now);
        }
        printf("ok\n");
        fflush(stdout);
    }
    if (audio) fclose(audio);
    return 0;
}
