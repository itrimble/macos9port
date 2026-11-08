/* Trackpad, key preview, accent menu, pending-accent tag, the hardware
 * keyboard strip and the Command-hold shortcut sheet. Popups use Platinum
 * conventions: one-pixel black frame, hard drop shadow, menu-gray body and
 * the deep blue-violet selection of the OS 8 menus. */
#include "kbd_draw_int.h"
#include <math.h>
#include <stdio.h>

#define MENU_BG 0xFFEEEEEEu
#define MENU_HI 0xFF34349Au

static void platinum_box(Fb *fb, float dp, KRect r, uint32_t top, uint32_t bot, float a) {
    float s = fmaxf(1, dp * 1.5f);
    g_rrect(fb, r.x + s, r.y + s, r.w, r.h, 0, 0xFF000000u, 0xFF000000u, 0.35f * a);
    g_rrect(fb, r.x, r.y, r.w, r.h, 0, top, bot, a);
    g_rrect_stroke(fb, r.x, r.y, r.w, r.h, 0, fmaxf(1, dp * 0.75f), 0xFF000000u, a);
}

static void draw_pad(Kbd *k, Fb *fb, const KbdSkin *sk, float yoff) {
    float pa, ba;
    KRect p = kbd_anim_pad(k, 0, &pa);
    float dp = k->dp;
    if (p.w >= 2 && p.h >= 2 && pa > 0.01f) {
        float r = fminf(p.h, p.w) * 0.08f;
        p.y += yoff;
        g_rrect(fb, p.x - 2 * dp, p.y - 2 * dp, p.w + 4 * dp, p.h + 4 * dp, r + 2 * dp,
                g_shade(sk->deck_top, 0.82f), g_shade(sk->deck_bot, 1.06f), pa);
        g_rrect(fb, p.x, p.y, p.w, p.h, r, sk->pad_top, sk->pad_bot, pa);
        int dots = (int)(p.w * p.h / (18 * dp * dp));
        for (int i = 0; i < dots && i < 9000 && p.w >= 4 && p.h >= 4; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            g_disc(fb, p.x + (float)(h % (uint32_t)p.w), p.y + (float)((h >> 11) % (uint32_t)p.h),
                   0.45f * dp, (h & 0x40000) ? 0xFFFFFFFFu : 0xFF000000u, 0.05f * pa);
        }
        g_rrect(fb, p.x, p.y, p.w, p.h * 0.12f, r, 0xFF000000u, sk->pad_top, 0.16f * pa);
        g_rrect_stroke(fb, p.x, p.y, p.w, p.h, r, fmaxf(1, dp * 0.6f), sk->pad_edge, 0.9f * pa);
        int pressed = 0;
        for (int i = 0; i < KBD_MAX_TOUCH; i++) {
            const KbdTouch *t = &k->touch[i];
            if (!t->active || t->role != ROLE_PAD) continue;
            pressed |= t->stage;
            /* The contact glow grows with pressure and rings on a force click. */
            float r0 = 13 * dp * (1 + 0.35f * t->ps);
            g_disc(fb, t->x, t->y + yoff, r0, 0xFFFFFFFFu, 0.12f + 0.1f * (t->stage > 0));
            if (t->stage == 2) g_arc(fb, t->x, t->y + yoff, r0 + 5 * dp, 1.5f * dp, 0, 6.2831853f, 0xFFFFFFFFu, 0.35f);
        }
        if (pressed) g_rrect(fb, p.x, p.y, p.w, p.h, r, 0xFF000000u, 0xFF000000u, 0.06f);   /* the pad gives */
    }
    KRect b = kbd_anim_pad(k, 1, &ba);
    if (b.w < 2 || b.h < 2 || ba <= 0.01f) return;
    b.y += yoff;
    int down = k->pad.button_down;
    float sink = down ? 1.2f * dp : 0;
    g_rrect(fb, b.x - 2 * dp, b.y - 2 * dp, b.w + 4 * dp, b.h + 4 * dp, b.h * 0.3f,
            g_shade(sk->deck_top, 0.82f), g_shade(sk->deck_bot, 1.05f), ba);
    g_rrect(fb, b.x, b.y + sink, b.w, b.h - sink, b.h * 0.28f,
            down ? g_shade(sk->button_top, 0.9f) : sk->button_top, sk->button_bot, ba);
    g_rrect(fb, b.x + b.w * 0.04f, b.y + sink + 1 * dp, b.w * 0.92f, fmaxf(1, dp * 0.7f), 0,
            0xFFFFFFFFu, 0xFFFFFFFFu, (down ? 0.08f : 0.3f) * ba);
    g_rrect_stroke(fb, b.x, b.y + sink, b.w, b.h - sink, b.h * 0.28f, fmaxf(1, dp * 0.6f), sk->pad_edge, 0.9f * ba);
}

static void draw_cursor_mode(Kbd *k, Fb *fb, float yoff) {
    float a = k->cursor_s.x;
    if (a < 0.01f) return;
    KRect d = k->lay.deck;
    g_rrect(fb, d.x, d.y + yoff, d.w, d.h, 0, 0xFF000000u, 0xFF000000u, 0.08f * a);
    for (int i = 0; i < KBD_MAX_TOUCH; i++) {
        const KbdTouch *t = &k->touch[i];
        if (t->active && t->role == ROLE_CURSOR)
            g_disc(fb, t->x, t->y + yoff, 18 * k->dp, 0xFFFFFFFFu, 0.18f * a);
    }
}

static void draw_preview(Kbd *k, Fb *fb, const KbdSkin *sk, float yoff) {
    float s = k->preview_s.x;
    int p = k->preview;
    if (p < 0 || p >= k->lay.n || s < 0.02f) return;
    KRect r = kbd_anim_rect(k, p);
    r.y += yoff;
    float dp = k->dp, w = fmaxf(r.w * 1.5f, k->lay.unit * 1.2f), h = r.h * 1.32f;
    float sc = 0.6f + 0.4f * s;
    w *= sc; h *= sc;
    float cx = r.x + r.w * 0.5f, bottom = r.y - k->lay.gap * 0.5f;
    cx = fmaxf(w * 0.5f + 2 * dp, fminf(k->sw - w * 0.5f - 2 * dp, cx));
    KRect b = { cx - w * 0.5f, bottom - h, w, h };
    float a = fminf(1, s * 1.3f);
    float stem_w = r.w * 0.86f;
    g_shadow(fb, b.x, b.y + 3 * dp, b.w, b.h + r.h * 0.4f, 6 * dp, 7 * dp, 0xFF000000u, 0.35f * a);
    g_rrect(fb, r.x + (r.w - stem_w) * 0.5f - 1, bottom - 4 * dp, stem_w + 2, r.h * 0.9f + 4 * dp,
            4 * dp, 0xFF000000u, 0xFF000000u, 0.55f * a);
    g_rrect(fb, r.x + (r.w - stem_w) * 0.5f, bottom - 4 * dp, stem_w, r.h * 0.9f + 3 * dp, 4 * dp,
            sk->cap_bot, sk->cap_bot, a);
    g_rrect(fb, b.x, b.y, b.w, b.h, 6 * dp, sk->cap_top, sk->cap_bot, a);
    g_rrect_stroke(fb, b.x, b.y, b.w, b.h, 6 * dp, fmaxf(1, dp * 0.8f), 0xFF000000u, 0.75f * a);
    g_rrect(fb, r.x + (r.w - stem_w) * 0.5f, b.y + b.h - 6 * dp, stem_w, 8 * dp, 0, sk->cap_bot, sk->cap_bot, a);
    g_rrect(fb, b.x + 3 * dp, b.y + 2 * dp, b.w - 6 * dp, fmaxf(1, dp), 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0.45f * a);
    const KbdSlot *sl = &k->lay.keys[p];
    int dead = 0;
    const char *t = kbd_slot_output(sl, kbd_mods(k) & ~(KBD_MOD_CMD | KBD_MOD_CTRL), k->caps, &dead);
    if (dead) t = kbd_dead_mark(dead);
    if (t && *t) g_text_center(fb, b.x + b.w * 0.5f, b.y + b.h * 0.47f, g_fit(t, b.h * 0.56f, b.w * 0.8f),
                               sk->legend, a, t);
}

static void draw_alts(Kbd *k, Fb *fb, float yoff) {
    float s = k->alt_s.x;
    if (s < 0.02f || k->alt_n <= 0) return;
    KRect r = k->alt_rect;
    r.y += yoff;
    float sc = 0.7f + 0.3f * s, a = fminf(1, s * 1.4f);
    float cx = r.x + r.w * 0.5f, by = r.y + r.h;
    r = (KRect){ cx - r.w * sc * 0.5f, by - r.h * sc, r.w * sc, r.h * sc };
    platinum_box(fb, k->dp, r, MENU_BG, MENU_BG, a);
    float cw = r.w / (float)k->alt_n;
    for (int i = 0; i < k->alt_n; i++) {
        KRect c = { r.x + i * cw, r.y, cw, r.h };
        int sel = i == k->alt_sel && k->alt_open;
        if (sel) g_rrect(fb, c.x + 1, c.y + 1, c.w - 2, c.h - 2, 0, MENU_HI, MENU_HI, a);
        g_text_center(fb, c.x + c.w * 0.5f, c.y + c.h * 0.5f, c.h * 0.5f,
                      sel ? 0xFFFFFFFFu : 0xFF000000u, a, k->alts[i]);
    }
}

static void draw_dead_tag(Kbd *k, Fb *fb, float yoff) {
    if (!k->dead) return;
    float dp = k->dp;
    KRect r = { 8 * dp, k->lay.deck.y + yoff - 30 * dp, 92 * dp, 22 * dp };
    platinum_box(fb, dp, r, MENU_BG, 0xFFDDDDDDu, 1);
    char buf[32];
    snprintf(buf, sizeof buf, "%s  accent", kbd_dead_mark(k->dead));
    g_text_center(fb, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 12 * dp, 0xFF000000u, 1, buf);
}

static void draw_hud(Kbd *k, Fb *fb) {
    float s = k->hud_s.x;
    if (s < 0.01f) return;
    float dp = k->dp, h = 36 * dp, y = k->sh - h * fminf(s, 1.05f);
    g_rrect(fb, 0, y - 5 * dp, (float)k->sw, 5 * dp, 0, 0x00000000u, 0x40000000u, 1);
    g_rrect(fb, 0, y, (float)k->sw, h + 4 * dp, 0, 0xFFE2E2E2u, 0xFFC4C4C4u, 1);
    g_rrect(fb, 0, y, (float)k->sw, fmaxf(1, dp * 0.7f), 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 1);
    static const char *const names[] = { "control", "option", "", "shift", "caps lock" };
    static const int bits[] = { KBD_MOD_CTRL, KBD_MOD_OPT, KBD_MOD_CMD, KBD_MOD_SHIFT, 0 };
    float x = 8 * dp;
    for (int i = 0; i < 5; i++) {
        int on = i == 4 ? k->hw_caps : (k->hw_mods & bits[i]) != 0;
        float w = i == 2 ? 26 * dp : g_text_width(names[i], 11 * dp) + 14 * dp;
        KRect c = { x, y + 8 * dp, w, h - 16 * dp };
        g_rrect(fb, c.x, c.y, c.w, c.h, 4 * dp, on ? 0xFF4A4A9Eu : 0xFFF2F2F2u, on ? 0xFF34348Au : 0xFFD6D6D6u, 1);
        g_rrect_stroke(fb, c.x, c.y, c.w, c.h, 4 * dp, fmaxf(1, dp * 0.6f), 0xFF6A6A6Au, 1);
        uint32_t tc = on ? 0xFFFFFFFFu : 0xFF333333u;
        if (i == 2) kbd_draw_cmd(fb, c.x + c.w * 0.5f, c.y + c.h * 0.5f, c.h * 0.8f, tc, 1);
        else g_text_center(fb, c.x + c.w * 0.5f, c.y + c.h * 0.5f, 11 * dp, tc, 1, names[i]);
        x += w + 5 * dp;
    }
    KRect b = kbd_hud_button(k);
    b.y = y + 5 * dp;
    if (x + 120 * dp < b.x)
        g_text(fb, x + 8 * dp, y + 12 * dp, 11 * dp, 0xFF555555u, 1, "Hardware keyboard");
    g_rrect(fb, b.x, b.y, b.w, b.h, 5 * dp, 0xFFF8F8F8u, 0xFFD0D0D0u, 1);
    g_rrect_stroke(fb, b.x, b.y, b.w, b.h, 5 * dp, fmaxf(1, dp * 0.8f), 0xFF000000u, 1);
    float ix = b.x + 10 * dp, iy = b.y + b.h * 0.5f - 6 * dp;
    g_rrect_stroke(fb, ix, iy, 20 * dp, 12 * dp, 2 * dp, fmaxf(1, dp * 0.7f), 0xFF222222u, 1);
    for (int rr = 0; rr < 2; rr++)
        for (int cc = 0; cc < 5; cc++)
            g_rrect(fb, ix + (2.5f + cc * 3.2f) * dp, iy + (2.5f + rr * 3.4f) * dp, 2.2f * dp, 2.2f * dp, 0,
                    0xFF222222u, 0xFF222222u, 1);
    g_text(fb, ix + 26 * dp, b.y + b.h * 0.5f - 6.5f * dp, 12 * dp, 0xFF000000u, 1, "Keyboard");
}

static void draw_sheet(Kbd *k, Fb *fb) {
    float s = k->sheet_s.x;
    if (s < 0.01f || k->nshort <= 0) return;
    float dp = k->dp, a = fminf(1, s * 1.2f);
    float w = fminf(k->sw * 0.86f, 300 * dp), rowh = 22 * dp, title = 20 * dp;
    float h = title + k->nshort * rowh + 12 * dp;
    float x = (k->sw - w) * 0.5f, y = (k->sh - h) * 0.42f + (1 - s) * 16 * dp;
    KRect r = { x, y, w, h };
    platinum_box(fb, dp, r, MENU_BG, 0xFFE4E4E4u, a);
    g_rrect(fb, x + 1, y + 1, w - 2, title - 1, 0, 0xFFE8E8E8u, 0xFFD2D2D2u, a);
    for (float yy = y + 4 * dp; yy < y + title - 3 * dp; yy += 2 * dp)
        g_rrect(fb, x + 6 * dp, yy, w - 12 * dp, fmaxf(1, dp * 0.6f), 0, 0xFF8C8C8Cu, 0xFF8C8C8Cu, 0.7f * a);
    const char *tt = "Keyboard Shortcuts";
    float tw = g_text_width(tt, 12 * dp);
    g_rrect(fb, x + (w - tw) * 0.5f - 6 * dp, y + 2 * dp, tw + 12 * dp, title - 4 * dp, 0,
            0xFFDEDEDEu, 0xFFDEDEDEu, a);
    g_text_center(fb, x + w * 0.5f, y + title * 0.5f, 12 * dp, 0xFF000000u, a, tt);
    g_rrect(fb, x, y + title, w, fmaxf(1, dp * 0.6f), 0, 0xFF000000u, 0xFF000000u, a);
    for (int i = 0; i < k->nshort; i++) {
        float ry = y + title + 6 * dp + i * rowh;
        kbd_draw_cmd(fb, x + 18 * dp, ry + rowh * 0.5f, 13 * dp, 0xFF000000u, a);
        g_text(fb, x + 30 * dp, ry + 4 * dp, 13 * dp, 0xFF000000u, a, k->shortcuts[i][0]);
        g_text(fb, x + 62 * dp, ry + 4 * dp, 13 * dp, 0xFF000000u, a, k->shortcuts[i][1]);
    }
}

void kbd_draw(Kbd *k, Fb *fb) {
    if (!k || !fb || k->sw <= 0) return;
    const KbdSkin *sk = kbd_skin_get(k->skin);
    float yoff = kbd_yoff(k);
    if (k->show.x > 0.002f) {
        kbd_draw_keys(k, fb);
        draw_pad(k, fb, sk, yoff);
        draw_cursor_mode(k, fb, yoff);
        draw_dead_tag(k, fb, yoff);
        draw_preview(k, fb, sk, yoff);
        draw_alts(k, fb, yoff);
    }
    draw_hud(k, fb);
    draw_sheet(k, fb);
}
