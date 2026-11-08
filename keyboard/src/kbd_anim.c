/* Motion. Everything is a damped spring stepped at a fixed sub-step, so the
 * feel is identical at 60, 120 or 144 Hz and under a dropped frame.
 *
 * Keys go down fast and come back up with a small overshoot, the way a cap
 * rebounds off its spring. Changing posture morphs every key from where it
 * was to where it belongs, staggered outward from the middle so the
 * keyboard unfolds rather than jumps. A layer change flips each legend in a
 * left-to-right ripple. Reduce Motion settles everything at once. */
#include "kbd_int.h"
#include <math.h>
#include <string.h>

void spring_step(Spring *s, float target, float k, float c, float dt) {
    while (dt > 0) {
        float h = dt > 1.0f / 480.0f ? 1.0f / 480.0f : dt;
        float a = -k * (s->x - target) - c * s->v;
        s->v += a * h;          /* semi-implicit Euler: stable for stiff springs */
        s->x += s->v * h;
        dt -= h;
    }
}

static float damp(float k, float ratio) { return 2.0f * sqrtf(k) * ratio; }

static KRect lerp_rect(KRect a, KRect b, float t) {
    return (KRect){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                    a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t };
}

KRect kbd_anim_rect(const Kbd *k, int slot) {
    const KbdAnim *a = &k->anim[slot];
    KRect r = k->lay.keys[slot].r;
    float t = a->t;
    if (t >= 0.9999f && t <= 1.0001f) return r;
    if (t > 1.06f) t = 1.06f;
    if (t < 0) t = 0;
    return lerp_rect(a->from, r, t);
}

float kbd_anim_alpha(const Kbd *k, int slot) {
    const KbdAnim *a = &k->anim[slot];
    if (!a->born) return 1;
    return fminf(1, fmaxf(0, a->t * 1.4f - 0.2f));
}

static float mt(const Kbd *k) { return fminf(fmaxf(k->morph.x, 0), 1.04f); }

static KRect pad_at(KRect from, KRect to, float t) {
    if (to.w <= 0) return lerp_rect(from, (KRect){ from.x + from.w / 2, from.y + from.h / 2, 0, 0 }, t);
    if (from.w <= 0) from = (KRect){ to.x + to.w / 2, to.y + to.h / 2, 0, 0 };
    return lerp_rect(from, to, t);
}

float kbd_anim_deck_y(const Kbd *k) {
    return k->deck_from + (k->lay.deck.y - k->deck_from) * mt(k);
}

KRect kbd_anim_pad(const Kbd *k, int button, float *alpha) {
    KRect from = button ? k->btn_from : k->pad_from, to = button ? k->lay.pad_button : k->lay.pad;
    if (alpha) *alpha = to.w > 0 ? (from.w > 0 ? 1 : fminf(1, mt(k))) : 1 - fminf(1, mt(k));
    return pad_at(from, to, mt(k));
}

void kbd_anim_relayout(Kbd *k, const KbdLayout *old, int by_position) {
    KRect cur[KBD_MAX_KEYS];
    if (!by_position) {
        KbdLayout now0 = k->lay;
        k->lay = *old;
        float dy = kbd_anim_deck_y(k);
        KRect p = kbd_anim_pad(k, 0, NULL), b = kbd_anim_pad(k, 1, NULL);
        k->lay = now0;
        k->deck_from = old->deck.w > 0 ? dy : k->lay.deck.y;
        k->pad_from = p;
        k->btn_from = b;
        k->morph = (Spring){ 0, 0 };
    }
    KbdAnim prev[KBD_MAX_KEYS];
    memcpy(prev, k->anim, sizeof prev);
    KbdLayout now = k->lay;
    k->lay = *old;                       /* evaluate old rects mid-flight */
    for (int i = 0; i < old->n; i++) cur[i] = kbd_anim_rect(k, i);
    k->lay = now;
    int used[KBD_MAX_KEYS] = {0};
    float cx = k->sw * 0.5f, w = k->sw > 0 ? (float)k->sw : 1;
    for (int i = 0; i < now.n; i++) {
        const KbdSlot *s = &now.keys[i];
        int m = -1;
        for (int j = 0; j < old->n && m < 0; j++) {
            if (used[j]) continue;
            const KbdSlot *o = &old->keys[j];
            int same = by_position ? (o->row == s->row && o->col == s->col &&
                                      fabsf(o->r.x - s->r.x) < now.unit * 0.6f)
                                   : (o->id == s->id && o->id != KK_GLYPH);
            if (same) m = j;
        }
        KbdAnim *a = &k->anim[i];
        memset(a, 0, sizeof *a);
        float dist = fabsf(s->r.x + s->r.w * 0.5f - cx) / w;
        if (m >= 0) {
            used[m] = 1;
            a->from = cur[m];
            a->press = prev[m].press;
            if (by_position) {
                a->t = 1;
                a->flip = 1;
                a->delay = s->col * 0.014f + s->row * 0.01f;
            } else {
                a->delay = dist * 0.14f + s->row * 0.012f;
            }
        } else {
            a->born = 1;
            KRect r = s->r;
            a->from = (KRect){ r.x + r.w * 0.35f, r.y + r.h * 0.35f, r.w * 0.3f, r.h * 0.3f };
            a->delay = 0.06f + dist * 0.14f;
        }
    }
    k->nghost = 0;
    for (int j = 0; j < old->n; j++) {
        if (used[j] || k->nghost >= KBD_MAX_KEYS) continue;
        k->ghost[k->nghost].slot = old->keys[j];
        k->ghost[k->nghost].slot.r = cur[j];
        k->ghost[k->nghost].alpha = 1;
        k->nghost++;
    }
    if (k->set.reduce_motion) {
        for (int i = 0; i < now.n; i++) { k->anim[i].t = 1; k->anim[i].flip = 0; }
        k->nghost = 0;
        k->morph = (Spring){ 1, 0 };
    }
}

static int slot_pressed(const Kbd *k, int slot) {
    for (int i = 0; i < KBD_MAX_TOUCH; i++) {
        const KbdTouch *t = &k->touch[i];
        if (t->active && t->slot == slot && (t->role == ROLE_KEY || t->role == ROLE_ALT ||
            (t->role == ROLE_CURSOR && kbd_keydefs[k->lay.keys[slot].id].kind == KIND_SPACE)))
            return 1;
    }
    return 0;
}

static float press_target(const Kbd *k, int slot) {
    const KbdKeyDef *d = &kbd_keydefs[k->lay.keys[slot].id];
    if (slot_pressed(k, slot)) return 1;
    if (d->kind == KIND_MOD) {
        if (d->mod == KBD_MOD_CAPS) return k->caps || k->hw_caps ? 0.62f : 0;
        if (d->mod == KBD_MOD_SHIFT && k->caps) return 0.62f;
        if (k->latched & d->mod) return 0.45f;
    }
    return 0;
}

static void settle(Spring *s, float target, float k, float ratio, float dt, int instant) {
    if (instant) { s->x = target; s->v = 0; return; }
    spring_step(s, target, k, damp(k, ratio), dt);
}

void kbd_anim_step(Kbd *k, float dt) {
    int rm = k->set.reduce_motion;
    for (int i = 0; i < k->lay.n; i++) {
        KbdAnim *a = &k->anim[i];
        float tgt = press_target(k, i);
        Spring p = { a->press, a->pv };
        if (rm) { p.x = tgt; p.v = 0; }
        else if (tgt > p.x) spring_step(&p, tgt, 9000, damp(9000, 1.0f), dt);
        else spring_step(&p, tgt, 1100, damp(1100, 0.38f), dt);   /* rebound */
        a->press = p.x; a->pv = p.v;
        if (a->delay > 0) { a->delay -= dt; if (a->delay > 0) continue; }
        if (a->t < 1 || fabsf(a->tv) > 0.001f) {
            Spring m = { a->t, a->tv };
            settle(&m, 1, 210, 0.72f, dt, rm);
            a->t = m.x; a->tv = m.v;
            if (fabsf(a->t - 1) < 0.0005f && fabsf(a->tv) < 0.002f) { a->t = 1; a->tv = 0; }
        }
        if (a->flip > 0) a->flip = rm ? 0 : fmaxf(0, a->flip - dt / 0.24f);
        if (a->glow > 0) a->glow = fmaxf(0, a->glow - dt / 0.18f);
    }
    for (int g = 0; g < k->nghost; g++) k->ghost[g].alpha -= dt * 7;
    while (k->nghost > 0 && k->ghost[k->nghost - 1].alpha <= 0) k->nghost--;
    settle(&k->morph, 1, 190, 0.82f, dt, rm);
    if (fabsf(k->morph.x - 1) < 0.0005f && fabsf(k->morph.v) < 0.002f) k->morph = (Spring){ 1, 0 };
    int show = k->want_show && !k->hw_active;
    settle(&k->show, show ? 1.0f : 0.0f, 260, 0.86f, dt, rm);
    settle(&k->hud_s, k->want_show && k->hw_active ? 1.0f : 0.0f, 300, 0.9f, dt, rm);
    settle(&k->sheet_s, k->sheet ? 1.0f : 0.0f, 420, 0.8f, dt, rm);
    settle(&k->preview_s, k->preview >= 0 ? 1.0f : 0.0f, 1400, 0.55f, dt, rm);
    settle(&k->alt_s, k->alt_open ? 1.0f : 0.0f, 900, 0.62f, dt, rm);
    settle(&k->cursor_s, k->cursor_mode ? 1.0f : 0.0f, 300, 1.0f, dt, rm);
    settle(&k->optfade, (kbd_mods(k) & KBD_MOD_OPT) ? 1.0f : 0.0f, 500, 1.0f, dt, rm);
}

int kbd_animating(const Kbd *k) {
    for (int i = 0; i < k->lay.n; i++) {
        const KbdAnim *a = &k->anim[i];
        if (a->t != 1 || a->flip > 0 || a->glow > 0 || fabsf(a->press - press_target(k, i)) > 0.002f ||
            fabsf(a->pv) > 0.01f)
            return 1;
    }
    const Spring *s[] = { &k->show, &k->hud_s, &k->sheet_s, &k->preview_s, &k->alt_s,
                          &k->cursor_s, &k->optfade };
    float tg[] = { (float)(k->want_show && !k->hw_active), (float)(k->want_show && k->hw_active),
                   (float)k->sheet, (float)(k->preview >= 0), (float)k->alt_open,
                   (float)k->cursor_mode, (float)((kbd_mods(k) & KBD_MOD_OPT) != 0) };
    for (int i = 0; i < 7; i++)
        if (fabsf(s[i]->x - tg[i]) > 0.002f || fabsf(s[i]->v) > 0.01f) return 1;
    return k->nghost > 0 || k->pad.vx != 0 || k->pad.vy != 0 || k->morph.x != 1;
}
