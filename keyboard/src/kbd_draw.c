/* Deck and keycaps.
 *
 * A cap is drawn as a skirt (the visible front and side walls) with a top
 * face inset toward the back, so it reads as a sculpted key seen from the
 * typist's seat. Pressing moves the face down into the skirt and tightens
 * the shadow. Desk layouts print legends the way the old boards did: letters
 * top-left, shifted symbol over the digit, words in the lower corner. Phone
 * layouts center a single bold legend for small targets. */
#include "kbd_draw_int.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

float kbd_yoff(const Kbd *k) {
    float s = k->show.x;
    return (1 - s) * (k->lay.deck.h + 12 * k->dp);
}

int kbd_desk(const Kbd *k) {
    return k->posture == KBD_FLEX || (k->posture == KBD_OPEN && !k->split);
}

void kbd_draw_cmd(Fb *fb, float cx, float cy, float s, uint32_t c, float alpha) {
    float a = s * 0.13f, r = s * 0.115f, t = fmaxf(1.0f, s * 0.07f), e = a + r;
    g_line(fb, cx - a, cy - e, cx - a, cy + e, t, c, alpha);
    g_line(fb, cx + a, cy - e, cx + a, cy + e, t, c, alpha);
    g_line(fb, cx - e, cy - a, cx + e, cy - a, t, c, alpha);
    g_line(fb, cx - e, cy + a, cx + e, cy + a, t, c, alpha);
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
        float th = atan2f(-sy, -sx);
        g_arc(fb, cx + sx * e, cy + sy * e, r, t, th + PI * 0.25f, th + 2 * PI - PI * 0.25f, c, alpha);
    }
}

static void draw_arrow(Fb *fb, int32_t code, float cx, float cy, float s, uint32_t c, float alpha) {
    float h = s * 0.5f, w = s * 0.42f;
    if (code == KBD_K_LEFT) g_triangle(fb, cx - h * 0.6f, cy, cx + h * 0.5f, cy - w, cx + h * 0.5f, cy + w, c, alpha);
    else if (code == KBD_K_RIGHT) g_triangle(fb, cx + h * 0.6f, cy, cx - h * 0.5f, cy - w, cx - h * 0.5f, cy + w, c, alpha);
    else if (code == KBD_K_UP) g_triangle(fb, cx, cy - h * 0.6f, cx - w, cy + h * 0.5f, cx + w, cy + h * 0.5f, c, alpha);
    else g_triangle(fb, cx, cy + h * 0.6f, cx - w, cy - h * 0.5f, cx + w, cy - h * 0.5f, c, alpha);
}

static uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

static void draw_deck(Kbd *k, Fb *fb, const KbdSkin *sk, float yoff) {
    KRect d = k->lay.deck;
    float y = kbd_anim_deck_y(k) + yoff, h = k->sh - y + 2;
    if (y >= k->sh) return;
    float dp = k->dp;
    g_rrect(fb, 0, y - 8 * dp, (float)k->sw, 8 * dp, 0, 0x00000000u, 0x55000000u, 1);
    g_rrect(fb, d.x, y, d.w, h, 0, sk->deck_top, sk->deck_bot, 1);
    g_rrect(fb, d.x, y, d.w, fmaxf(1, dp), 0, g_shade(sk->deck_top, 1.25f), g_shade(sk->deck_top, 1.25f), 0.9f);
    g_rrect(fb, d.x, y + fmaxf(1, dp), d.w, fmaxf(1, dp * 0.6f), 0, sk->deck_edge, sk->deck_edge, 0.35f);
    if (sk->texture == TEX_PINSTRIPE) {
        for (float yy = y + 3 * dp; yy < k->sh; yy += 3 * dp)
            g_rrect(fb, 0, yy, (float)k->sw, fmaxf(1, dp * 0.5f), 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0.07f);
    } else if (sk->texture == TEX_SPECKLE) {
        int n = (int)(k->sw * h / (40 * dp * dp));
        for (int i = 0; i < n && i < 6000; i++) {
            uint32_t r = hash((uint32_t)i * 2654435761u + 7);
            float px = (float)(r % (uint32_t)k->sw), py = y + (float)((r >> 12) % (uint32_t)(h > 1 ? h : 1));
            g_disc(fb, px, py, 0.5f * dp, (r & 0x100000) ? 0xFFFFFFFFu : 0xFF000000u, 0.06f);
        }
    }
    if (k->lay.strip.w > 0) {
        KRect s = k->lay.strip;
        g_rrect(fb, s.x, s.y + yoff, s.w, s.h, 0, g_shade(sk->deck_top, 0.94f), g_shade(sk->deck_top, 0.97f), 1);
        g_rrect(fb, s.x, s.y + s.h + yoff, s.w, fmaxf(1, dp * 0.6f), 0, sk->deck_edge, sk->deck_edge, 0.25f);
    }
    if (k->posture == KBD_FLEX && k->lay.crease.w > 0) {   /* the fold, seen as a hinge seam */
        KRect c = k->lay.crease;
        g_rrect(fb, c.x, c.y + c.h * 0.5f - 3 * dp, c.w, 6 * dp, 0, 0xFF101012u, 0xFF2A2A2Eu, 0.85f);
        g_rrect(fb, c.x, c.y + c.h * 0.5f - 3 * dp, c.w, fmaxf(1, dp * 0.6f), 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0.18f);
    }
}

static int is_letter(const KbdKeyDef *d) {
    return d->kind == KIND_CHAR && d->label[0] >= 'a' && d->label[0] <= 'z' && !d->label[1];
}

static void legend_word(Fb *fb, const Kbd *k, const KbdSlot *s, KRect f, const char *word,
                        uint32_t col, float alpha) {
    float px = g_fit(word, f.h * (kbd_desk(k) ? 0.24f : 0.27f), f.w * 0.82f);
    if (!kbd_desk(k)) { g_text_center(fb, f.x + f.w * 0.5f, f.y + f.h * 0.5f, px, col, alpha, word); return; }
    float w = g_text_width(word, px), pad = f.h * 0.14f;
    int right = s->r.x + s->r.w * 0.5f > k->sw * 0.5f && s->id != KK_SPACE;
    float x = right ? f.x + f.w - pad - w : f.x + pad;
    g_text(fb, x, f.y + f.h - pad - px * 0.95f, px, col, alpha, word);
}

static void draw_legend(Fb *fb, Kbd *k, const KbdSkin *sk, const KbdSlot *s, KRect f, float alpha) {
    const KbdKeyDef *d = &kbd_keydefs[s->id];
    int mods = kbd_mods(k) & ~KBD_MOD_OPT;
    uint32_t col = d->kind == KIND_CHAR || d->kind == KIND_SPACE ? sk->legend : sk->legend_mod;
    float cx = f.x + f.w * 0.5f, cy = f.y + f.h * 0.5f, cur = k->cursor_s.x;
    alpha *= 1 - cur;
    if (alpha <= 0.01f) return;
    if (s->id == KK_CMD || s->id == KK_RCMD) {
        float sz = f.h * (kbd_desk(k) ? 0.42f : 0.5f);
        if (kbd_desk(k)) kbd_draw_cmd(fb, f.x + f.h * 0.14f + sz * 0.42f, f.y + f.h - f.h * 0.14f - sz * 0.42f, sz, col, alpha);
        else kbd_draw_cmd(fb, cx, cy, sz, col, alpha);
        return;
    }
    if (d->kind == KIND_ACTION && (d->code == KBD_K_LEFT || d->code == KBD_K_RIGHT ||
                                   d->code == KBD_K_UP || d->code == KBD_K_DOWN)) {
        draw_arrow(fb, d->code, cx, cy, fminf(f.w, f.h) * 0.5f, col, alpha);
        return;
    }
    if (d->kind != KIND_CHAR || s->id == KK_GLYPH) {
        const char *w = s->id == KK_GLYPH ? s->glyph : d->label;
        if (!w[0]) return;
        if (s->id == KK_GLYPH) g_text_center(fb, cx, cy, g_fit(w, f.h * 0.48f, f.w * 0.8f), col, alpha, w);
        else if (d->kind == KIND_LAYER) g_text_center(fb, cx, cy, g_fit(w, f.h * 0.3f, f.w * 0.8f), col, alpha, w);
        else legend_word(fb, k, s, f, w, col, alpha);
        return;
    }
    float opt = k->optfade.x;
    const char *base = kbd_slot_legend(s, mods, k->caps);
    if (kbd_desk(k)) {
        float pad = f.h * 0.15f, px = f.h * 0.34f;
        if (!is_letter(d) && d->shift) {
            g_text(fb, f.x + pad, f.y + pad * 0.7f, px * 0.86f, col, alpha * (1 - opt), d->shift);
            g_text(fb, f.x + pad, f.y + f.h * 0.52f, px * 0.86f, col, alpha * (1 - opt), d->label);
        } else {
            g_text(fb, f.x + pad, f.y + pad * 0.8f, px, col, alpha * (1 - opt), base);
        }
    } else {
        g_text_center(fb, cx, cy, g_fit(base, f.h * 0.5f, f.w * 0.86f), col, alpha * (1 - opt), base);
    }
    if (opt > 0.01f) {
        int shift = (kbd_mods(k) & KBD_MOD_SHIFT) != 0;
        const char *o = kbd_slot_legend(s, (shift ? KBD_MOD_SHIFT : 0) | KBD_MOD_OPT, k->caps);
        if (o && *o) g_text_center(fb, cx, cy, g_fit(o, f.h * 0.48f, f.w * 0.86f), col, alpha * opt, o);
        if (d->dead && !shift)
            g_rrect_stroke(fb, f.x + 1.5f * k->dp, f.y + 1.5f * k->dp, f.w - 3 * k->dp, f.h - 3 * k->dp,
                           f.h * sk->radius, fmaxf(1, 1.6f * k->dp), sk->accent == 0xFFFFFFFFu ? 0xFF0D3A40u : sk->accent,
                           alpha * opt);
    }
}

void kbd_draw_cap(Kbd *k, Fb *fb, const KbdSkin *sk, const KbdSlot *s, KRect r, float press,
                  float alpha, float glow, float flip) {
    if (alpha <= 0.01f || r.w < 2 || r.h < 2) return;
    const KbdKeyDef *d = &kbd_keydefs[s->id];
    float h = r.h, rad = h * sk->radius, travel = h * 0.075f * press;
    int modkey = d->kind != KIND_CHAR && d->kind != KIND_SPACE;
    uint32_t top = modkey ? sk->mod_top : sk->cap_top, bot = modkey ? sk->mod_bot : sk->cap_bot;
    if (sk->row_tint[0] && s->row < 6) {
        top = g_mix(top, sk->row_tint[s->row], 0.42f);
        bot = g_mix(bot, sk->row_tint[s->row], 0.55f);
    }
    float ca = sk->cap_alpha * alpha;
    if (sk->texture == TEX_DOMES)
        g_disc(fb, r.x + r.w * 0.5f, r.y + h * 0.55f, fminf(r.w, h) * 0.3f, g_shade(sk->deck_bot, 0.72f), 0.5f * alpha);
    g_shadow(fb, r.x, r.y + h * 0.06f, r.w, h, rad, h * (0.1f - 0.06f * press), 0xFF000000u, 0.32f * alpha);
    g_rrect(fb, r.x, r.y, r.w, h, rad, sk->skirt, sk->skirt_dark, ca);
    float ins = h * 0.065f, it = h * 0.035f, ib = h * 0.14f;
    KRect f = { r.x + ins, r.y + it + travel, r.w - 2 * ins, h - it - ib + travel * 0.4f };
    if (press > 0.02f) { top = g_shade(top, 1 - 0.05f * press); bot = g_shade(bot, 1 - 0.06f * press); }
    g_rrect(fb, f.x, f.y, f.w, f.h, rad * 0.85f, top, bot, ca);
    g_rrect(fb, f.x + f.w * 0.08f, f.y + f.h * 0.06f, f.w * 0.84f, f.h * 0.22f, f.h * 0.1f,
            0xFFFFFFFFu, top, 0.16f * alpha);
    g_rrect_stroke(fb, f.x, f.y, f.w, f.h, rad * 0.85f, fmaxf(1, k->dp * 0.5f), 0xFFFFFFFFu, 0.22f * alpha);
    if (glow > 0) g_rrect(fb, f.x, f.y, f.w, f.h, rad * 0.85f, 0xFFFFFFFFu, 0xFFFFFFFFu, 0.22f * glow * alpha);
    int lit = d->kind == KIND_MOD && ((k->latched & d->mod) ||
                                      (d->mod == KBD_MOD_CAPS && (k->caps || k->hw_caps)) ||
                                      (d->mod == KBD_MOD_SHIFT && k->caps));
    if (d->kind == KIND_MOD) {
        float lr = fmaxf(1.6f * k->dp, h * 0.045f);
        g_disc(fb, f.x + f.w - lr * 2.6f, f.y + lr * 2.6f, lr, lit ? sk->accent : g_shade(top, 0.8f), alpha);
        if (lit) g_disc(fb, f.x + f.w - lr * 2.6f, f.y + lr * 2.6f, lr * 2.4f, sk->accent, 0.18f * alpha);
    }
    float la = alpha * (1 - flip);
    if (flip > 0) f.y += flip * h * 0.18f;
    draw_legend(fb, k, sk, s, f, la);
}

void kbd_draw_keys(Kbd *k, Fb *fb) {
    const KbdSkin *sk = kbd_skin_get(k->skin);
    float yoff = kbd_yoff(k);
    draw_deck(k, fb, sk, yoff);
    for (int g = 0; g < k->nghost; g++) {
        KRect r = k->ghost[g].slot.r;
        r.y += yoff;
        kbd_draw_cap(k, fb, sk, &k->ghost[g].slot, r, 0, fmaxf(0, k->ghost[g].alpha), 0, 0);
    }
    for (int i = 0; i < k->lay.n; i++) {
        KRect r = kbd_anim_rect(k, i);
        r.y += yoff;
        if (r.y > k->sh) continue;
        const KbdAnim *a = &k->anim[i];
        float flip = a->delay > 0 && a->flip > 0 ? 1 : a->flip;
        kbd_draw_cap(k, fb, sk, &k->lay.keys[i], r, a->press, kbd_anim_alpha(k, i), a->glow, flip);
    }
}
