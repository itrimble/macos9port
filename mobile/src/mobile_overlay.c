/* Loupe, posture balloons, first-run guide and the trackpad pointer. */
#include "mobile_ui.h"
#include "kbd_gfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265f

/* ---- text helper: greedy word wrap, returns the height used ---- */
static float wrap_text(Fb *fb, float x, float y, float w, float px, uint32_t c, const char *s,
                       int draw) {
    char line[160];
    float lh = px * 1.3f, yy = y;
    while (s && *s) {
        int n = 0, last_space = -1;
        while (s[n] && n < (int)sizeof line - 1) {
            memcpy(line, s, (size_t)n + 1);
            line[n + 1] = 0;
            if (g_text_width(line, px) > w && last_space > 0) break;
            if (s[n] == ' ') last_space = n;
            n++;
        }
        int take = s[n] ? (last_space > 0 ? last_space : n) : n;
        memcpy(line, s, (size_t)take);
        line[take] = 0;
        if (draw) g_text(fb, x, yy, px, c, 1, line);
        yy += lh;
        s += take;
        while (*s == ' ') s++;
    }
    return yy - y;
}

/* ---- loupe ---- */

void mobile_loupe_draw(Fb *fb, const MobileLayout *l, const MobileView *v,
                       const MobileTouch *t, const uint32_t *desk) {
    if (!t->loupe || !desk) return;
    float dp = l->dp, r = 58 * dp, gap = 96 * dp;
    float cx = t->loupe_sx, cy = t->loupe_sy - gap;
    if (cy - r < 6 * dp) cy = t->loupe_sy + gap;
    cx = fmaxf(r + 6 * dp, fminf(fb->w - r - 6 * dp, cx));
    float k = (float)l->scale * v->zoom;
    float fx = v->ox + (t->loupe_sx - l->desk.x) / k;
    float fy = v->oy + (t->loupe_sy - (l->desk.y - v->lift)) / k;
    float mag = fmaxf(k * 2.5f, 5 * dp);
    g_disc(fb, cx, cy + 4 * dp, r + 3 * dp, MUI_INK, 0.28f);
    int y0 = (int)(cy - r), y1 = (int)(cy + r), x0 = (int)(cx - r), x1 = (int)(cx + r);
    for (int py = y0; py <= y1; py++) {
        if (py < 0 || py >= fb->h) continue;
        for (int px = x0; px <= x1; px++) {
            if (px < 0 || px >= fb->w) continue;
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            if (dx * dx + dy * dy > r * r) continue;
            int sx = (int)floorf(fx + dx / mag), sy = (int)floorf(fy + dy / mag);
            uint32_t c = 0xFF303030u;
            if (sx >= 0 && sy >= 0 && sx < l->desk_w && sy < l->desk_h)
                c = desk[(size_t)sy * l->desk_w + sx];
            fb->px[(size_t)py * fb->w + px] = c | 0xFF000000u;
        }
    }
    g_arc(fb, cx, cy, r, 4 * dp, 0, 2 * PI, MUI_PANEL, 1);
    g_arc(fb, cx, cy, r + 2.2f * dp, 1.2f * dp, 0, 2 * PI, MUI_DARK, 1);
    g_arc(fb, cx, cy, r - 2.2f * dp, 1.0f * dp, 0, 2 * PI, MUI_SHADOW, 1);
    /* crosshair with a white halo so it reads on any wallpaper */
    float a = 4 * dp, b = 14 * dp;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? MUI_INK : MUI_WHITE;
        float th = pass ? 1.4f * dp : 3.4f * dp;
        g_line(fb, cx - b, cy, cx - a, cy, th, c, 1);
        g_line(fb, cx + a, cy, cx + b, cy, th, c, 1);
        g_line(fb, cx, cy - b, cx, cy - a, th, c, 1);
        g_line(fb, cx, cy + a, cx, cy + b, th, c, 1);
    }
    if (t->ring > 0) {
        g_arc(fb, cx, cy, r + 6 * dp, 4.5f * dp, -PI / 2, -PI / 2 + 2 * PI * t->ring, MUI_ACCENT, 1);
        const char *hint = t->ring < 1 ? "Lift to click. Keep holding for menu" : "Menu";
        float px = g_fit(hint, 12 * dp, fb->w - 16 * dp);
        float ty = cy > t->loupe_sy ? cy + r + 16 * dp : cy - r - 22 * dp;
        float tw = g_text_width(hint, px);
        float hx = fmaxf(tw / 2 + 8 * dp, fminf(fb->w - tw / 2 - 8 * dp, cx));
        g_rrect(fb, hx - tw / 2 - 6 * dp, ty - 3 * dp, tw + 12 * dp, px + 8 * dp, 4 * dp,
                MUI_BALLOON, MUI_BALLOON, 0.95f);
        g_text_center(fb, hx, ty + px / 2 + 1 * dp, px, MUI_INK, 1, hint);
    }
}

/* ---- balloons ---- */

enum { TOAST_MS = 2800, TOAST_FADE = 450 };

void mobile_toast_show(MobileToast *t, const char *title, const char *body, uint32_t ms) {
    snprintf(t->title, sizeof t->title, "%s", title ? title : "");
    snprintf(t->body, sizeof t->body, "%s", body ? body : "");
    t->start_ms = ms;
    t->active = 1;
}

int mobile_toast_draw(MobileToast *t, Fb *fb, Rect area, float dp, uint32_t ms) {
    if (!t->active) return 0;
    uint32_t age = ms - t->start_ms;
    if (age >= TOAST_MS) { t->active = 0; return 0; }
    float alpha = age > TOAST_MS - TOAST_FADE ? (TOAST_MS - age) / (float)TOAST_FADE : 1;
    if (age < 180) alpha = age / 180.0f;
    float w = fminf(area.w - 24 * dp, 380 * dp), pad = 12 * dp;
    float bh = wrap_text(fb, 0, 0, w - 2 * pad, 12.5f * dp, MUI_INK, t->body, 0);
    float h = pad * 2 + 16 * dp + bh;
    float x = area.x + (area.w - w) / 2, y = area.y + 30 * dp;
    g_shadow(fb, x, y + 3 * dp, w, h, 10 * dp, 8 * dp, MUI_INK, 0.3f * alpha);
    g_triangle(fb, x + 34 * dp, y - 12 * dp, x + 26 * dp, y + 2 * dp, x + 48 * dp, y + 2 * dp,
               MUI_INK, alpha);
    g_triangle(fb, x + 34.5f * dp, y - 9 * dp, x + 28 * dp, y + 3 * dp, x + 46 * dp, y + 3 * dp,
               MUI_BALLOON, alpha);
    g_rrect(fb, x, y, w, h, 10 * dp, MUI_BALLOON, MUI_BALLOON, alpha);
    g_rrect_stroke(fb, x, y, w, h, 10 * dp, 1.3f * dp, MUI_INK, alpha);
    g_text(fb, x + pad, y + pad, 14 * dp, MUI_INK, alpha, t->title);
    if (alpha > 0.5f)
        wrap_text(fb, x + pad, y + pad + 18 * dp, w - 2 * pad, 12.5f * dp, MUI_INK, t->body, 1);
    return 1;
}

/* ---- first-run guide ---- */

typedef enum { G_TAP, G_HOLD, G_TWO_TAP, G_SCROLL, G_FOLD, G_STRIP } Gesture;

static const char *coach_lines[] = {
    "Tap to click. Double-tap to open.",
    "Touch and hold to aim with the loupe, then lift to click.",
    "Keep holding, or tap with two fingers, for the contextual menu.",
    "Drag two fingers to scroll. Pinch to zoom in on anything small.",
    NULL,   /* posture line */
    "The tab in the bottom corner opens the Control Strip.",
};

static const char *fold_line(MobilePosture p, int tablet) {
    if (tablet) return "Attach a keyboard and trackpad or mouse: the pointer follows it, and holding Command lists shortcuts.";
    switch (p) {
    case MP_LAPTOP: return "Laptop mode: keyboard and trackpad below the fold.";
    case MP_FLAT: case MP_BOOK: return "Half-fold it like a laptop for a keyboard and trackpad.";
    default: return "Unfold for the big desktop. Your windows come along.";
    }
}

static void gesture_glyph(Fb *fb, Gesture g, float x, float y, float s, MobilePosture p) {
    float u = s / 16;
    uint32_t ink = MUI_INK, soft = MUI_ACCENT;
    switch (g) {
    case G_TAP:
        g_disc(fb, x + 8 * u, y + 8 * u, 3 * u, ink, 1);
        g_arc(fb, x + 8 * u, y + 8 * u, 5.5f * u, u, 0, 2 * PI, soft, 1);
        g_arc(fb, x + 8 * u, y + 8 * u, 7.5f * u, u, 0, 2 * PI, soft, 0.5f);
        break;
    case G_HOLD:
        g_disc(fb, x + 8 * u, y + 8 * u, 3 * u, ink, 1);
        g_arc(fb, x + 8 * u, y + 8 * u, 6.5f * u, 1.6f * u, -PI / 2, PI, soft, 1);
        break;
    case G_TWO_TAP:
        g_disc(fb, x + 5 * u, y + 8 * u, 2.6f * u, ink, 1);
        g_disc(fb, x + 11 * u, y + 8 * u, 2.6f * u, ink, 1);
        g_arc(fb, x + 8 * u, y + 8 * u, 7.5f * u, u, 0, 2 * PI, soft, 0.7f);
        break;
    case G_SCROLL:
        g_disc(fb, x + 5 * u, y + 10 * u, 2.4f * u, ink, 1);
        g_disc(fb, x + 11 * u, y + 10 * u, 2.4f * u, ink, 1);
        g_line(fb, x + 8 * u, y + 1.5f * u, x + 8 * u, y + 8 * u, 1.2f * u, soft, 1);
        g_triangle(fb, x + 5.5f * u, y + 4 * u, x + 10.5f * u, y + 4 * u, x + 8 * u, y + 0.5f * u, soft, 1);
        break;
    case G_FOLD:
        if (p == MP_POSTURE_COUNT) {   /* tablet: a slate over a keyboard */
            g_rrect(fb, x + 2 * u, y + 2 * u, 12 * u, 8.5f * u, 1.2f * u, ink, ink, 1);
            g_rrect(fb, x + 3 * u, y + 3 * u, 10 * u, 6.5f * u, 0.4f * u, MUI_SELECT, MUI_SELECT, 1);
            g_rrect(fb, x + 1 * u, y + 11.5f * u, 14 * u, 3 * u, 0.8f * u, ink, ink, 1);
            break;
        }
        mobile_glyph_phone(fb, p == MP_LAPTOP ? MP_LAPTOP : (p == MP_COVER ? MP_FLAT : MP_LAPTOP),
                           x, y, s, ink, MUI_SELECT);
        break;
    default:
        mobile_platinum_box(fb, x + 2 * u, y + 2 * u, 5 * u, 12 * u, 1.2f * u, 0);
        for (int i = 0; i < 3; i++)
            mobile_platinum_box(fb, x + (7.5f + i * 2.8f) * u, y + 4 * u, 2.4f * u, 8 * u, 0.6f * u, 0);
        break;
    }
}

void mobile_coach_layout(MobileCoach *c, Rect area, float dp) {
    float w = fminf(area.w - 24 * dp, 460 * dp);
    float h = 26 * dp + 18 * dp + 6 * 52 * dp + 64 * dp;
    if (h > area.h - 16 * dp) h = area.h - 16 * dp;
    float x = area.x + (area.w - w) / 2, y = area.y + (area.h - h) / 2;
    c->window = (Rect){(int)x, (int)y, (int)w, (int)h};
    float bw = fminf(w - 40 * dp, 220 * dp), bh = 36 * dp;
    c->button = (Rect){(int)(x + (w - bw) / 2), (int)(y + h - bh - 16 * dp), (int)bw, (int)bh};
}

void mobile_coach_draw(const MobileCoach *c, Fb *fb, float dp) {
    if (!c->visible) return;
    Rect r = c->window;
    float tb = 26 * dp;
    g_rrect(fb, 0, 0, fb->w, fb->h, 0, MUI_INK, MUI_INK, 0.35f);   /* dim the desktop */
    g_shadow(fb, r.x, r.y + 4 * dp, r.w, r.h, 4 * dp, 12 * dp, MUI_INK, 0.45f);
    g_rrect(fb, r.x, r.y, r.w, r.h, 3 * dp, MUI_PANEL, MUI_PANEL, 1);
    g_rrect_stroke(fb, r.x, r.y, r.w, r.h, 3 * dp, 1.3f * dp, MUI_INK, 1);
    g_rrect(fb, r.x + 1.5f * dp, r.y + 1.5f * dp, r.w - 3 * dp, tb, 2 * dp, MUI_LIGHT, 0xFFCFCFCFu, 1);
    const char *title = "Welcome to OS999";
    float tp = 14 * dp, tw = g_text_width(title, tp);
    for (int i = 0; i < 6; i++) {   /* Platinum title-bar ridges either side of the title */
        float ly = r.y + 6 * dp + i * 3 * dp;
        g_line(fb, r.x + 30 * dp, ly, r.x + r.w / 2 - tw / 2 - 10 * dp, ly, 1.0f * dp, MUI_SHADOW, 1);
        g_line(fb, r.x + r.w / 2 + tw / 2 + 10 * dp, ly, r.x + r.w - 10 * dp, ly, 1.0f * dp, MUI_SHADOW, 1);
    }
    mobile_platinum_box(fb, r.x + 9 * dp, r.y + 7 * dp, 13 * dp, 13 * dp, 1 * dp, 0);
    g_text_center(fb, r.x + r.w / 2.0f, r.y + tb / 2 + 1.5f * dp, tp, MUI_INK, 1, title);
    float y = r.y + tb + 14 * dp, gs = 32 * dp, tx = r.x + 16 * dp + gs + 12 * dp;
    float tw_max = r.x + r.w - 16 * dp - tx;
    for (int i = 0; i < 6; i++) {
        const char *line = coach_lines[i] ? coach_lines[i] : fold_line(c->posture, c->tablet);
        float th = wrap_text(fb, 0, 0, tw_max, 13 * dp, MUI_INK, line, 0);
        float row = fmaxf(gs, th) + 14 * dp;
        if (y + row > c->button.y - 6 * dp) break;
        g_rrect(fb, r.x + 14 * dp, y - 2 * dp, gs + 4 * dp, gs + 4 * dp, 6 * dp, MUI_WHITE, MUI_LIGHT, 1);
        gesture_glyph(fb, (Gesture)i, r.x + 16 * dp, y, gs, c->tablet ? MP_POSTURE_COUNT : c->posture);
        wrap_text(fb, tx, y + (gs - th) / 2 + 2 * dp, tw_max, 13 * dp, MUI_INK, line, 1);
        y += row;
    }
    Rect b = c->button;
    g_rrect_stroke(fb, b.x - 4 * dp, b.y - 4 * dp, b.w + 8 * dp, b.h + 8 * dp, 9 * dp, 3 * dp, MUI_INK, 1);
    mobile_platinum_box(fb, b.x, b.y, b.w, b.h, 6 * dp, 0);
    g_text_center(fb, b.x + b.w / 2.0f, b.y + b.h / 2.0f, 14 * dp, MUI_INK, 1, "Start Using OS999");
}

int mobile_coach_hit_button(const MobileCoach *c, int x, int y) {
    Rect b = c->button;
    return c->visible && x >= b.x - 8 && y >= b.y - 8 && x < b.x + b.w + 8 && y < b.y + b.h + 8;
}

/* ---- trackpad pointer ---- */

static uint16_t cur_data[16], cur_mask[16];
static int cur_hot_x, cur_hot_y, cur_loaded;

int mobile_pointer_load(const char *curs_path) {
    FILE *f = curs_path ? fopen(curs_path, "rb") : NULL;
    unsigned char b[68];
    if (!f) return 0;
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n != sizeof b) return 0;
    for (int i = 0; i < 16; i++) {
        cur_data[i] = (uint16_t)(b[i * 2] << 8 | b[i * 2 + 1]);
        cur_mask[i] = (uint16_t)(b[32 + i * 2] << 8 | b[32 + i * 2 + 1]);
    }
    cur_hot_y = (int16_t)(b[64] << 8 | b[65]);
    cur_hot_x = (int16_t)(b[66] << 8 | b[67]);
    cur_loaded = 1;
    return 1;
}

void mobile_pointer_draw(Fb *fb, const MobileLayout *l, const MobileView *v, float dx, float dy) {
    float sx, sy;
    mobile_view_to_screen(v, l, dx, dy, &sx, &sy);
    float k = (float)l->scale * v->zoom;
    if (k > 4 * l->scale) k = 4.0f * l->scale;
    if (!cur_loaded) {   /* simple arrow until the cursor file loads */
        g_triangle(fb, sx, sy, sx, sy + 16 * k, sx + 11 * k, sy + 11 * k, MUI_WHITE, 1);
        g_triangle(fb, sx + k, sy + 2.5f * k, sx + k, sy + 13.5f * k, sx + 8.5f * k, sy + 10 * k, MUI_INK, 1);
        return;
    }
    int bk = (int)ceilf(k);
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 16; col++) {
            uint16_t bit = (uint16_t)(0x8000u >> col);
            if (!(cur_mask[row] & bit) && !(cur_data[row] & bit)) continue;
            uint32_t c = (cur_data[row] & bit) ? MUI_INK : MUI_WHITE;
            fb_fill(fb, (int)(sx + (col - cur_hot_x) * k), (int)(sy + (row - cur_hot_y) * k), bk, bk, c);
        }
}
