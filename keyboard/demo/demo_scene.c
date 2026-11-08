/* Drawing for the simulator: a phone body, an OS999 screen with a
 * SimpleText-style window that receives what you type, and the control
 * panel. Everything here is original drawing. */
#include "demo.h"
#include <math.h>
#include <string.h>

void demo_editor_insert(DemoEditor *e, const char *s) {
    size_t n = strlen(s);
    if (e->len + (int)n >= DEMO_TEXT - 1) return;
    memcpy(e->text + e->len, s, n);
    e->len += (int)n;
    e->text[e->len] = 0;
}

static void back_one(DemoEditor *e) {
    while (e->len > 0) {
        unsigned char c = (unsigned char)e->text[--e->len];
        if ((c & 0xC0) != 0x80) break;
    }
    e->text[e->len] = 0;
}

void demo_editor_backspace(DemoEditor *e, int word) {
    if (!word) { back_one(e); return; }
    while (e->len > 0 && e->text[e->len - 1] == ' ') back_one(e);
    while (e->len > 0 && e->text[e->len - 1] != ' ' && e->text[e->len - 1] != '\n') back_one(e);
}

void demo_blit(Fb *dst, const Fb *src, int x, int y) {
    for (int yy = 0; yy < src->h; yy++) {
        int dy = y + yy;
        if (dy < 0 || dy >= dst->h) continue;
        for (int xx = 0; xx < src->w; xx++) {
            int dx = x + xx;
            if (dx >= 0 && dx < dst->w) dst->px[(size_t)dy * dst->w + dx] = src->px[(size_t)yy * src->w + xx];
        }
    }
}

/* Greedy word wrap; calls back per line. Returns line count. */
typedef void (*LineFn)(void *ud, int line, const char *s, int n);
static int wrap(const char *s, float px, float maxw, LineFn fn, void *ud) {
    int line = 0;
    while (*s) {
        int best = 0, i = 0;
        char buf[512];
        while (s[i] && s[i] != '\n' && i < 500) {
            int j = i;
            while (s[j] && s[j] != ' ' && s[j] != '\n' && j < 500) j++;
            memcpy(buf, s, (size_t)j);
            buf[j] = 0;
            if (g_text_width(buf, px) > maxw && best > 0) break;
            best = j;
            i = j;
            if (s[i] == ' ') i++;
            if (g_text_width(buf, px) > maxw) break;
        }
        if (best == 0) best = i > 0 ? i : 1;
        if (fn) fn(ud, line, s, best);
        line++;
        s += best;
        while (*s == ' ') s++;
        if (*s == '\n') s++;
    }
    return line;
}

typedef struct { Fb *fb; float x, y, px, lh, top, bottom; uint32_t c; float last_x, last_y; } LineCtx;

static void draw_line(void *ud, int line, const char *s, int n) {
    LineCtx *c = ud;
    char buf[512];
    if (n > 511) n = 511;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    float y = c->y + line * c->lh;
    c->last_y = y;
    c->last_x = c->x + g_text_width(buf, c->px);
    if (y + c->lh < c->top || y > c->bottom) return;
    g_text(c->fb, c->x, y, c->px, c->c, 1, buf);
}

static void arrow(Fb *fb, float x, float y, float s) {
    float o = s * 0.12f;
    g_triangle(fb, x - o, y - o * 2, x - o, y + 16 * s + o * 2, x + 11.5f * s + o * 2, y + 11.5f * s, 0xFFFFFFFFu, 1);
    g_line(fb, x + 4.5f * s, y + 11 * s, x + 7.5f * s, y + 17.5f * s, 4.6f * s, 0xFFFFFFFFu, 1);
    g_triangle(fb, x, y, x, y + 15 * s, x + 10.5f * s, y + 10.5f * s, 0xFF000000u, 1);
    g_line(fb, x + 4.5f * s, y + 11 * s, x + 7.5f * s, y + 17 * s, 2.2f * s, 0xFF000000u, 1);
}

void demo_draw_os(Fb *fb, float dp, const DemoEditor *e, float area_h, uint32_t now, const char *title) {
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++)
            fb->px[(size_t)y * fb->w + x] = ((x / 2 + y / 2) & 1) ? 0xFF5B71A2u : 0xFF56709Cu;
    float mb = 20 * dp, W = (float)fb->w;
    g_rrect(fb, 0, 0, W, mb, 0, 0xFFEEEEEEu, 0xFFEEEEEEu, 1);
    g_rrect(fb, 0, mb - dp, W, dp, 0, 0xFF000000u, 0xFF000000u, 1);
    g_text(fb, 10 * dp, 4 * dp, 12 * dp, 0xFF000000u, 1, "999   File  Edit  View  Special");
    g_text(fb, W - 44 * dp, 4 * dp, 12 * dp, 0xFF000000u, 1, "9:41");
    float wx = 8 * dp, wy = mb + 8 * dp, ww = W - 16 * dp, wh = fmaxf(70 * dp, area_h - wy - 8 * dp);
    float tb = 19 * dp;
    g_rrect(fb, wx + 2 * dp, wy + 2 * dp, ww, wh, 0, 0xFF000000u, 0xFF000000u, 0.3f);
    g_rrect(fb, wx, wy, ww, wh, 0, 0xFFDDDDDDu, 0xFFDDDDDDu, 1);
    g_rrect_stroke(fb, wx, wy, ww, wh, 0, dp, 0xFF000000u, 1);
    for (float y = wy + 4 * dp; y < wy + tb - 4 * dp; y += 2 * dp)
        g_rrect(fb, wx + 4 * dp, y, ww - 8 * dp, dp, 0, 0xFF999999u, 0xFF999999u, 1);
    g_rrect(fb, wx + 9 * dp, wy + 4 * dp, 11 * dp, 11 * dp, 0, 0xFFE8E8E8u, 0xFFBBBBBBu, 1);
    g_rrect_stroke(fb, wx + 9 * dp, wy + 4 * dp, 11 * dp, 11 * dp, 0, dp, 0xFF333333u, 1);
    float tw = g_text_width(title, 12 * dp);
    g_rrect(fb, wx + (ww - tw) / 2 - 6 * dp, wy + 2 * dp, tw + 12 * dp, tb - 4 * dp, 0, 0xFFDDDDDDu, 0xFFDDDDDDu, 1);
    g_text(fb, wx + (ww - tw) / 2, wy + 3 * dp, 12 * dp, 0xFF000000u, 1, title);
    float cx = wx + dp, cy = wy + tb, cw = ww - 2 * dp - 15 * dp, ch = wh - tb - dp;
    g_rrect(fb, cx, cy, cw, ch, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 1);
    g_rrect(fb, cx + cw, cy, 15 * dp, ch, 0, 0xFFD2D2D2u, 0xFFD2D2D2u, 1);
    g_rrect(fb, cx + cw, cy, dp, ch, 0, 0xFF000000u, 0xFF000000u, 1);
    float px = 13 * dp, lh = 17 * dp, mx = 10 * dp;
    int lines = wrap(e->text, px, cw - 2 * mx, NULL, NULL);
    float visible = (ch - 2 * mx) / lh;
    float scroll = fmaxf(0, fminf(e->scroll, (float)lines - visible + 1));
    float auto_top = fmaxf(0, (float)lines - visible + 0.5f);
    float top_line = e->scroll > 0 ? fmaxf(0, auto_top - scroll) : auto_top;
    Fb clip = *fb;
    clip.clip = 1; clip.cx = (int)cx; clip.cy = (int)cy; clip.cw = (int)cw; clip.ch = (int)ch;
    LineCtx c = { &clip, cx + mx, cy + mx - top_line * lh, px, lh, cy, cy + ch, 0xFF000000u, cx + mx, cy + mx - top_line * lh };
    wrap(e->text, px, cw - 2 * mx, draw_line, &c);
    if (e->len && e->text[e->len - 1] == '\n') { c.last_x = cx + mx; c.last_y += lh; }
    if (e->focused && (now / 530) % 2 == 0)
        g_rrect(&clip, c.last_x + dp, c.last_y - dp, fmaxf(1, dp), px * 1.15f, 0, 0xFF000000u, 0xFF000000u, 1);
    if (lines > visible) {
        float th = fmaxf(20 * dp, ch * visible / lines), t = top_line / fmaxf(1, lines - visible);
        g_rrect(fb, cx + cw + 2 * dp, cy + t * (ch - th), 11 * dp, th, 0, 0xFFC4C4FFu, 0xFF9C9CDCu, 1);
        g_rrect_stroke(fb, cx + cw + 2 * dp, cy + t * (ch - th), 11 * dp, th, 0, dp, 0xFF000000u, 1);
    }
    arrow(fb, e->px, e->py, dp);
}

void demo_draw_device(Fb *fb, float dp, DRect s, int posture) {
    float b = 10 * dp, r = posture == 0 ? 34 * dp : 26 * dp;
    g_shadow(fb, s.x - b, s.y - b + 10 * dp, s.w + 2 * b, s.h + 2 * b, r, 22 * dp, 0xFF000000u, 0.45f);
    g_rrect(fb, s.x - b, s.y - b, s.w + 2 * b, s.h + 2 * b, r, 0xFF2B2C30u, 0xFF16171Au, 1);
    g_rrect_stroke(fb, s.x - b, s.y - b, s.w + 2 * b, s.h + 2 * b, r, 1.5f * dp, 0xFF55565Cu, 1);
    if (posture == 1) {
        g_rrect(fb, s.x + s.w / 2 - 2 * dp, s.y - b, 4 * dp, b, 0, 0xFF0D0D0Fu, 0xFF2B2C30u, 1);
        g_rrect(fb, s.x + s.w / 2 - 2 * dp, s.y + s.h, 4 * dp, b, 0, 0xFF2B2C30u, 0xFF0D0D0Fu, 1);
    } else if (posture == 2) {
        float y = s.y + s.h / 2;
        g_rrect(fb, s.x - b - 4 * dp, y - 7 * dp, s.w + 2 * b + 8 * dp, 14 * dp, 7 * dp, 0xFF3A3B40u, 0xFF101114u, 1);
    }
}

void demo_draw_panel(Fb *fb, float dp, DemoButton *bt, int n, const char *skin, const char *blurb) {
    for (int i = 0; i < n; i++) {
        DRect r = bt[i].r;
        if (bt[i].id < 0) {
            g_text(fb, r.x, r.y + r.h * 0.25f, 12 * dp, 0xFFDDDDE6u, 1, bt[i].label);
            continue;
        }
        uint32_t t = bt[i].on ? 0xFF5A5AB0u : 0xFFF4F4F4u, bo = bt[i].on ? 0xFF3A3A8Au : 0xFFC8C8C8u;
        g_rrect(fb, r.x, r.y, r.w, r.h, 5 * dp, t, bo, 1);
        g_rrect_stroke(fb, r.x, r.y, r.w, r.h, 5 * dp, dp, 0xFF000000u, 1);
        g_text_center(fb, r.x + r.w / 2, r.y + r.h / 2, g_fit(bt[i].label, 12 * dp, r.w - 8 * dp),
                      bt[i].on ? 0xFFFFFFFFu : 0xFF000000u, 1, bt[i].label);
    }
    float x = 24 * dp, y = fb->h - 150 * dp;
    g_text(fb, x, y, 14 * dp, 0xFFFFFFFFu, 1, skin);
    Fb c = *fb;
    LineCtx lc = { &c, x, y + 22 * dp, 11 * dp, 15 * dp, 0, (float)fb->h, 0xFFC8C8D2u, 0, 0 };
    wrap(blurb, 11 * dp, 230 * dp, draw_line, &lc);
}
