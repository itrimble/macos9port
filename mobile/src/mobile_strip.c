/* The Control Strip: a Platinum row of tiles tucked into the bottom-left
 * corner. Collapsed it is a small grip tab; tap it and the tiles slide out.
 * Every tile shows its current state, so a first-time user can read the
 * whole mobile setup at a glance.
 */
#include "mobile_ui.h"
#include "kbd_gfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265f

void mobile_platinum_box(Fb *fb, float x, float y, float w, float h, float r, int pressed) {
    g_shadow(fb, x, y + 1, w, h, r, 3, MUI_INK, 0.25f);
    if (pressed) g_rrect(fb, x, y, w, h, r, MUI_SHADOW, 0xFFAAAAAAu, 1);
    else g_rrect(fb, x, y, w, h, r, MUI_WHITE, MUI_PANEL, 1);
    g_rrect_stroke(fb, x, y, w, h, r, 1.2f, MUI_DARK, 1);
}

void mobile_strip_place(MobileStrip *s, Rect area, float dp) {
    int expanded = s->expanded, pressed = s->pressed;
    memset(s, 0, sizeof *s);
    s->expanded = expanded;
    s->pressed = pressed;
    s->dp = dp;
    float tile = 48 * dp, gap = 4 * dp, tab_w = 20 * dp, h = 58 * dp, margin = 8 * dp;
    float room = area.w - 2 * margin - tab_w - gap;
    if (room < MS_TILE_COUNT * (tile + gap)) tile = room / MS_TILE_COUNT - gap;
    if (tile < 30 * dp) tile = 30 * dp;
    float y = area.y + area.h - h - margin;
    float x = area.x + margin;
    s->tab = (Rect){(int)x, (int)y, (int)tab_w, (int)h};
    float bar_w = s->expanded ? tab_w + gap + MS_TILE_COUNT * (tile + gap) : tab_w;
    s->bar = (Rect){(int)x, (int)y, (int)bar_w, (int)h};
    float tx = x + tab_w + gap, ty = y + (h - tile) / 2 - 2 * dp;
    for (int i = 0; i < MS_TILE_COUNT; i++) {
        s->tiles[i] = s->expanded ? (Rect){(int)tx, (int)ty, (int)tile, (int)(tile + 4 * dp)}
                                  : (Rect){0, 0, 0, 0};
        tx += tile + gap;
    }
}

static int in_rect(Rect r, int x, int y) {
    return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

int mobile_strip_hit(const MobileStrip *s, int x, int y) {
    if (in_rect(s->tab, x, y)) return MS_TAB;
    if (!s->expanded) return MS_NONE;
    for (int i = 0; i < MS_TILE_COUNT; i++)
        if (in_rect(s->tiles[i], x, y)) return i;
    return in_rect(s->bar, x, y) ? MS_TAB : MS_NONE;   /* gaps don't fall through */
}

const char *mobile_strip_label(MobileStripTile t, const MobileStripState *st, char *buf, int n) {
    switch (t) {
    case MS_KEYBOARD:
        if (st->hw_keyboard) return "HW Keys";
        return st->keyboard ? "Hide Keys" : "Keyboard";
    case MS_POINTER: return st->trackpad ? "Trackpad" : "Touch";
    case MS_ZOOM:
        if (st->zoom <= 1.01f) return "Fit";
        snprintf(buf, (size_t)n, "%.1fx", st->zoom);
        return buf;
    case MS_DISPLAY: snprintf(buf, (size_t)n, "%dx%d", st->desk_w, st->desk_h); return buf;
    case MS_POSTURE:
        if (st->tablet) return "Tablet";
        if (st->posture_locked) return mobile_posture_name(st->posture);
        snprintf(buf, (size_t)n, "Auto %s", mobile_posture_name(st->posture));
        return buf;
    case MS_SKIN: return st->skin ? st->skin : "Skin";
    case MS_SOUND: return st->sound ? "Sound" : "Muted";
    case MS_HELP: return "Help";
    default: return "";
    }
}

void mobile_glyph_phone(Fb *fb, MobilePosture p, float x, float y, float size, uint32_t ink,
                        uint32_t screen) {
    float u = size / 16, t = fmaxf(1, u * 0.9f);
    switch (p) {
    case MP_FLAT:
        g_rrect(fb, x + 2 * u, y + 3 * u, 12 * u, 10 * u, 1.5f * u, ink, ink, 1);
        g_rrect(fb, x + 3 * u, y + 4 * u, 10 * u, 8 * u, 0.6f * u, screen, screen, 1);
        for (float yy = 4.5f; yy < 12; yy += 2)
            g_line(fb, x + 8 * u, y + yy * u, x + 8 * u, y + (yy + 1) * u, t * 0.8f, ink, 0.7f);
        break;
    case MP_LAPTOP:
        g_rrect(fb, x + 3 * u, y + 1.5f * u, 10 * u, 7.5f * u, 1.2f * u, ink, ink, 1);
        g_rrect(fb, x + 4 * u, y + 2.5f * u, 8 * u, 5.5f * u, 0.5f * u, screen, screen, 1);
        g_triangle(fb, x + 3 * u, y + 9.5f * u, x + 13 * u, y + 9.5f * u, x + 15 * u, y + 14 * u, ink, 1);
        g_triangle(fb, x + 3 * u, y + 9.5f * u, x + 15 * u, y + 14 * u, x + 1 * u, y + 14 * u, ink, 1);
        for (int i = 0; i < 4; i++)
            g_line(fb, x + (4 + i * 2.5f) * u, y + 11.5f * u, x + (5 + i * 2.5f) * u, y + 11.5f * u,
                   t, screen, 1);
        break;
    case MP_BOOK:
        g_triangle(fb, x + 8 * u, y + 3 * u, x + 1.5f * u, y + 1.5f * u, x + 1.5f * u, y + 13 * u, ink, 1);
        g_triangle(fb, x + 8 * u, y + 3 * u, x + 1.5f * u, y + 13 * u, x + 8 * u, y + 15 * u, ink, 1);
        g_triangle(fb, x + 8 * u, y + 3 * u, x + 14.5f * u, y + 1.5f * u, x + 14.5f * u, y + 13 * u, ink, 1);
        g_triangle(fb, x + 8 * u, y + 3 * u, x + 14.5f * u, y + 13 * u, x + 8 * u, y + 15 * u, ink, 1);
        g_line(fb, x + 8 * u, y + 3.5f * u, x + 8 * u, y + 14.5f * u, t, screen, 1);
        break;
    default:   /* cover */
        g_rrect(fb, x + 4.5f * u, y + 1 * u, 7 * u, 14 * u, 1.5f * u, ink, ink, 1);
        g_rrect(fb, x + 5.5f * u, y + 2.2f * u, 5 * u, 11.6f * u, 0.6f * u, screen, screen, 1);
        break;
    }
}

void mobile_glyph(Fb *fb, MobileStripTile tile, float x, float y, float size,
                  const MobileStripState *st, uint32_t ink) {
    float u = size / 16, t = fmaxf(1, u);
    switch (tile) {
    case MS_KEYBOARD:
        g_rrect_stroke(fb, x + 1 * u, y + 4 * u, 14 * u, 9 * u, 1.2f * u, t, ink, 1);
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 5; c++)
                g_rrect(fb, x + (2.6f + c * 2.3f) * u, y + (5.6f + r * 2.2f) * u, 1.4f * u, 1.4f * u,
                        0.3f * u, ink, ink, 1);
        g_line(fb, x + 5 * u, y + 10.7f * u, x + 11 * u, y + 10.7f * u, t * 1.2f, ink, 1);
        break;
    case MS_POINTER:
        if (st && st->trackpad) {
            g_rrect_stroke(fb, x + 2.5f * u, y + 1.5f * u, 11 * u, 13 * u, 1.5f * u, t, ink, 1);
            g_line(fb, x + 2.5f * u, y + 11 * u, x + 13.5f * u, y + 11 * u, t, ink, 1);
            g_line(fb, x + 8 * u, y + 11 * u, x + 8 * u, y + 14.5f * u, t * 0.8f, ink, 1);
        } else {
            g_disc(fb, x + 8 * u, y + 8 * u, 2.8f * u, ink, 1);   /* a fingertip and its ripples */
            g_arc(fb, x + 8 * u, y + 8 * u, 5.0f * u, t * 0.9f, 0, 2 * PI, ink, 0.85f);
            g_arc(fb, x + 8 * u, y + 8 * u, 7.0f * u, t * 0.9f, 0, 2 * PI, ink, 0.4f);
        }
        break;
    case MS_ZOOM:
        g_arc(fb, x + 6.5f * u, y + 6.5f * u, 4.6f * u, 1.4f * t, 0, 2 * PI, ink, 1);
        g_line(fb, x + 10 * u, y + 10 * u, x + 14.5f * u, y + 14.5f * u, 2.4f * t, ink, 1);
        g_line(fb, x + 4.5f * u, y + 6.5f * u, x + 8.5f * u, y + 6.5f * u, t, ink, 1);
        if (!st || st->zoom <= 1.01f)
            g_line(fb, x + 6.5f * u, y + 4.5f * u, x + 6.5f * u, y + 8.5f * u, t, ink, 1);
        break;
    case MS_DISPLAY:
        g_rrect(fb, x + 1.5f * u, y + 2 * u, 13 * u, 9.5f * u, 1 * u, ink, ink, 1);
        g_rrect(fb, x + 2.7f * u, y + 3.2f * u, 10.6f * u, 7.1f * u, 0.4f * u, MUI_ACCENT, 0xFF8E8EE0u, 1);
        g_triangle(fb, x + 6 * u, y + 11.5f * u, x + 10 * u, y + 11.5f * u, x + 11 * u, y + 13.5f * u, ink, 1);
        g_triangle(fb, x + 6 * u, y + 11.5f * u, x + 11 * u, y + 13.5f * u, x + 5 * u, y + 13.5f * u, ink, 1);
        g_line(fb, x + 3.5f * u, y + 14.2f * u, x + 12.5f * u, y + 14.2f * u, t, ink, 1);
        break;
    case MS_POSTURE:
        if (st && st->tablet) {   /* a landscape slate, with keys under it when attached */
            g_rrect(fb, x + 1.5f * u, y + 2.5f * u, 13 * u, 9 * u, 1.2f * u, ink, ink, 1);
            g_rrect(fb, x + 2.5f * u, y + 3.5f * u, 11 * u, 7 * u, 0.4f * u, MUI_SELECT, MUI_SELECT, 1);
            if (st->hw_keyboard)
                g_rrect(fb, x + 1 * u, y + 12.3f * u, 14 * u, 2.4f * u, 0.6f * u, ink, ink, 1);
            break;
        }
        mobile_glyph_phone(fb, st ? st->posture : MP_COVER, x, y, size, ink, MUI_SELECT);
        break;
    case MS_SKIN: {
        static const uint32_t flavors[] = {0xFFF29B2Bu, 0xFF7CC242u, 0xFF3D7DD8u, 0xFF9B59C6u, 0xFFE0454Fu};
        g_arc(fb, x + 8 * u, y + 8 * u, 6.2f * u, t, 0, 2 * PI, ink, 1);
        for (int i = 0; i < 5; i++) {
            float a = -PI / 2 + i * 2 * PI / 5;
            g_disc(fb, x + (8 + 3.6f * cosf(a)) * u, y + (8 + 3.6f * sinf(a)) * u, 1.5f * u, flavors[i], 1);
        }
        break;
    }
    case MS_SOUND:
        g_rrect(fb, x + 2 * u, y + 6 * u, 3 * u, 4 * u, 0.3f * u, ink, ink, 1);
        g_triangle(fb, x + 4 * u, y + 6 * u, x + 8.5f * u, y + 2.5f * u, x + 8.5f * u, y + 13.5f * u, ink, 1);
        g_triangle(fb, x + 4 * u, y + 6 * u, x + 8.5f * u, y + 13.5f * u, x + 4 * u, y + 10 * u, ink, 1);
        if (!st || st->sound) {
            g_arc(fb, x + 8.5f * u, y + 8 * u, 3 * u, t, -PI / 3, PI / 3, ink, 1);
            g_arc(fb, x + 8.5f * u, y + 8 * u, 5.5f * u, t, -PI / 3, PI / 3, ink, 1);
        } else {
            g_line(fb, x + 10.5f * u, y + 5.5f * u, x + 14.5f * u, y + 10.5f * u, t, ink, 1);
            g_line(fb, x + 14.5f * u, y + 5.5f * u, x + 10.5f * u, y + 10.5f * u, t, ink, 1);
        }
        break;
    case MS_HELP:
        g_arc(fb, x + 8 * u, y + 8 * u, 6.4f * u, t, 0, 2 * PI, ink, 1);
        g_text_center(fb, x + 8 * u, y + 8 * u, 9.5f * u, ink, 1, "?");
        break;
    default:
        break;
    }
}

static void draw_tab(const MobileStrip *s, Fb *fb) {
    Rect r = s->tab;
    float dp = s->dp;
    mobile_platinum_box(fb, r.x, r.y, r.w, r.h, 4 * dp, s->pressed == MS_TAB);
    for (int i = 0; i < 3; i++) {
        float lx = r.x + r.w / 2.0f + (i - 1) * 3.5f * dp;
        g_line(fb, lx, r.y + 12 * dp, lx, r.y + r.h - 22 * dp, 1.2f * dp, MUI_SHADOW, 1);
        g_line(fb, lx + dp, r.y + 12 * dp, lx + dp, r.y + r.h - 22 * dp, 1.2f * dp, MUI_WHITE, 1);
    }
    float cx = r.x + r.w / 2.0f, cy = r.y + r.h - 12 * dp, a = 3.5f * dp;
    if (s->expanded) g_triangle(fb, cx + a, cy - a, cx + a, cy + a, cx - a, cy, MUI_DARK, 1);
    else g_triangle(fb, cx - a, cy - a, cx - a, cy + a, cx + a, cy, MUI_DARK, 1);
}

void mobile_strip_draw(const MobileStrip *s, Fb *fb, const MobileStripState *st) {
    float dp = s->dp;
    if (s->expanded) {
        Rect b = s->bar;
        g_shadow(fb, b.x, b.y + 2 * dp, b.w, b.h, 6 * dp, 6 * dp, MUI_INK, 0.35f);
        g_rrect(fb, b.x, b.y, b.w, b.h, 6 * dp, MUI_LIGHT, 0xFFC8C8C8u, 1);
        g_rrect_stroke(fb, b.x, b.y, b.w, b.h, 6 * dp, 1.2f * dp, MUI_DARK, 1);
    }
    draw_tab(s, fb);
    if (!s->expanded) return;
    char buf[32];
    for (int i = 0; i < MS_TILE_COUNT; i++) {
        Rect r = s->tiles[i];
        int on = (i == MS_KEYBOARD && st->keyboard) || (i == MS_POINTER && st->trackpad) ||
                 (i == MS_ZOOM && st->zoom > 1.01f) || (i == MS_POSTURE && st->posture_locked);
        mobile_platinum_box(fb, r.x, r.y, r.w, r.h, 4 * dp, s->pressed == i);
        if (on) g_rrect(fb, r.x + 2 * dp, r.y + 2 * dp, r.w - 4 * dp, r.h - 4 * dp, 3 * dp,
                        MUI_SELECT, 0xFFB4B4F0u, 1);
        float gs = fminf(r.w - 14 * dp, r.h - 22 * dp);
        mobile_glyph(fb, (MobileStripTile)i, r.x + (r.w - gs) / 2, r.y + 5 * dp, gs, st, MUI_INK);
        const char *label = mobile_strip_label((MobileStripTile)i, st, buf, sizeof buf);
        float px = g_fit(label, 9.5f * dp, r.w - 4 * dp);
        g_text_center(fb, r.x + r.w / 2.0f, r.y + r.h - 8 * dp, px, MUI_INK, 1, label);
    }
}
