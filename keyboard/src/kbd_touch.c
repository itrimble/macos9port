/* Touch typing.
 *
 * Character keys commit on lift, so a finger can slide to the neighbour it
 * meant (the preview follows). A second finger landing commits the first at
 * once, so fast two-thumb typing never drops letters. Modifiers work like a
 * real keyboard when held, and latch for one keystroke when tapped. Shift
 * tapped twice is Caps Lock; the full layouts' caps lock key stays down
 * while engaged, like the latching key on the old desk keyboards.
 * Option shows the Option layer on every cap, as Key Caps did, and its dead
 * keys compose with the next letter. Long-press offers alternates. Delete
 * repeats at the Keyboard control panel rate and, after a dozen repeats,
 * deletes by word (Option-Delete). Holding the space bar turns the whole
 * keyboard into a trackpad.
 */
#include "kbd_int.h"
#include <math.h>
#include <string.h>

enum { LONG_PRESS_MS = 420, CURSOR_HOLD_MS = 380, DOUBLE_TAP_MS = 320, WORD_DELETE_AFTER = 12 };

static const KbdKeyDef *def_of(const Kbd *k, int slot) { return &kbd_keydefs[k->lay.keys[slot].id]; }

static int is_wide(const Kbd *k, int slot) {
    const KbdKeyDef *d = def_of(k, slot);
    return d->kind == KIND_SPACE || k->lay.keys[slot].r.w > k->lay.unit * 1.6f ||
           d->code == KBD_K_RETURN;
}

static float slot_cx(const Kbd *k, int slot) {
    KRect r = k->lay.keys[slot].r;
    return r.x + r.w * 0.5f;
}

static int repeats(const KbdKeyDef *d) {
    return d->code == KBD_K_BACKSPACE || d->code == KBD_K_LEFT || d->code == KBD_K_RIGHT ||
           d->code == KBD_K_UP || d->code == KBD_K_DOWN;
}

static void clear_one_shot(Kbd *k) {
    k->latched = 0;
    k->shift_tap = 0;     /* a used latch never counts toward a double-tap */
}

static int32_t glyph_code(const char *g) {
    return (g[0] && !g[1] && (unsigned char)g[0] < 0x80) ? (unsigned char)g[0] : 0;
}

static void commit(Kbd *k, int slot) {
    const KbdSlot *s = &k->lay.keys[slot];
    const KbdKeyDef *d = def_of(k, slot);
    int mods = kbd_mods(k);
    k->anim[slot].glow = 1;
    if (d->kind == KIND_LAYER) {
        KbdLayer to = s->id == KK_LAYER_NUM ? KBD_LAYER_NUMBERS :
                      s->id == KK_LAYER_SYM ? KBD_LAYER_SYMBOLS : KBD_LAYER_LETTERS;
        kbd_set_layer(k, to);
        kbd_emit_haptic(k, KBD_HAPTIC_SELECTION);
        return;
    }
    if (d->kind == KIND_ACTION) {
        if (s->id == KK_HIDE) { kbd_show(k, 0); return; }
        if (k->dead && d->code == KBD_K_BACKSPACE) { k->dead = 0; return; }
        if (k->dead) { kbd_emit_text(k, kbd_dead_mark(k->dead)); k->dead = 0; }
        kbd_emit_key(k, d->code, 1, mods);
        kbd_emit_key(k, d->code, 0, mods);
        clear_one_shot(k);
        return;
    }
    if (d->kind == KIND_MOD) return;
    int dead = 0;
    const char *out = kbd_slot_output(s, mods, k->caps, &dead);
    if (dead) {                      /* Option-e and friends: wait for the letter */
        k->dead = dead;
        clear_one_shot(k);
        return;
    }
    int32_t code = s->id == KK_GLYPH ? glyph_code(s->glyph) : d->code;
    /* Double space after a word types a period, the one phone habit worth keeping. */
    if (d->kind == KIND_SPACE && !mods && k->now - k->last_space < 400 &&
        k->last_char && k->last_space > k->last_char) {
        kbd_emit_key(k, KBD_K_BACKSPACE, 1, 0);
        kbd_emit_key(k, KBD_K_BACKSPACE, 0, 0);
        kbd_emit_key(k, '.', 1, 0);
        kbd_emit_key(k, '.', 0, 0);
        kbd_emit_text(k, ".");
        k->last_char = 0;
    }
    int key_mods = mods & ~KBD_MOD_CAPS;
    if (code) kbd_emit_key(k, code, 1, key_mods);
    if (out) {
        if (k->dead) {
            char composed[16];
            kbd_compose(k->dead, out, composed, sizeof composed);
            kbd_emit_text(k, composed);
        } else {
            kbd_emit_text(k, out);
        }
    }
    k->dead = 0;
    if (code) kbd_emit_key(k, code, 0, key_mods);
    if (d->kind == KIND_SPACE) {
        k->last_space = k->now ? k->now : 1;
        if (k->layer != KBD_LAYER_LETTERS && k->posture == KBD_COVER)
            kbd_set_layer(k, KBD_LAYER_LETTERS);
    } else {
        k->last_char = k->now ? k->now : 1;
    }
    if (k->held) k->mod_used = 1;
    clear_one_shot(k);
}

static void press_sound(Kbd *k, int slot, int down) {
    int wide = is_wide(k, slot);
    int s = wide ? (down ? KBD_SND_WIDE_PRESS : KBD_SND_WIDE_RELEASE)
                 : (down ? KBD_SND_PRESS : KBD_SND_RELEASE);
    kbd_emit_sound(k, s, slot_cx(k, slot));
}

/* Fingers land a little below what they aim at. */
static int hit(const Kbd *k, float x, float y) {
    return kbd_layout_hit(&k->lay, x, y - k->lay.key_h * 0.08f, k->lay.unit * 0.5f);
}

int kbd_keys_down(Kbd *k, KbdTouch *t) {
    t->role = ROLE_KEY;
    t->slot = hit(k, t->x, t->y);
    if (t->slot < 0) { t->role = ROLE_NONE; return 1; }
    for (int i = 0; i < KBD_MAX_TOUCH; i++) {   /* rollover */
        KbdTouch *o = &k->touch[i];
        if (o == t || !o->active || o->role != ROLE_KEY || o->committed || o->slot < 0) continue;
        const KbdKeyDef *od = def_of(k, o->slot);
        if (od->kind == KIND_CHAR || od->kind == KIND_SPACE) {
            commit(k, o->slot);
            o->committed = 1;
            press_sound(k, o->slot, 0);
        }
    }
    const KbdKeyDef *d = def_of(k, t->slot);
    press_sound(k, t->slot, 1);
    kbd_emit_haptic(k, is_wide(k, t->slot) ? KBD_HAPTIC_KEY_WIDE : KBD_HAPTIC_KEY);
    if (d->kind == KIND_MOD) {
        if (d->mod == KBD_MOD_CAPS) {
            k->caps = !k->caps;
            kbd_emit_haptic(k, KBD_HAPTIC_MODIFIER);
            kbd_emit_key(k, KBD_K_CAPSLOCK, 1, 0);
            kbd_emit_key(k, KBD_K_CAPSLOCK, 0, 0);
            t->committed = 1;
            return 1;
        }
        k->held |= d->mod;
        k->mod_used = 0;
        return 1;
    }
    if (d->kind == KIND_ACTION && repeats(d)) {
        commit(k, t->slot);
        t->committed = 1;
        t->next_repeat = k->now + (uint32_t)k->set.repeat_delay_ms;
        return 1;
    }
    if (d->kind == KIND_CHAR) k->preview = t->slot;
    return 1;
}

static void open_alternates(Kbd *k, KbdTouch *t) {
    const KbdSlot *s = &k->lay.keys[t->slot];
    int dead = 0;
    int mods = kbd_mods(k) & ~(KBD_MOD_OPT | KBD_MOD_CMD | KBD_MOD_CTRL);
    const char *base = kbd_slot_output(s, mods, k->caps, &dead);
    if (!base) return;
    int n = kbd_alternates(base, k->alts, KBD_MAX_ALTS);
    if (n <= 0) return;
    k->alt_n = n;
    k->alt_open = 1;
    k->alt_slot = t->slot;
    float cw = k->lay.unit * 0.92f, w = cw * n, ch = k->lay.key_h * 1.05f;
    KRect r = s->r;
    float x = r.x + r.w * 0.5f - cw * 0.5f;
    if (x + w > k->sw - 4 * k->dp) x = k->sw - 4 * k->dp - w;
    if (x < 4 * k->dp) x = 4 * k->dp;
    k->alt_rect = (KRect){ x, r.y - ch - k->lay.key_h * 0.95f, w, ch };
    k->alt_sel = 0;
    k->alt_s.x = k->set.reduce_motion ? 1 : 0;
    k->preview = -1;
    t->role = ROLE_ALT;
    kbd_emit_haptic(k, KBD_HAPTIC_SELECTION);
}

static void update_alt(Kbd *k, KbdTouch *t) {
    float cw = k->alt_rect.w / (float)k->alt_n;
    int sel = (int)floorf((t->x - k->alt_rect.x) / cw);
    if (t->y > k->alt_rect.y + k->alt_rect.h + k->lay.key_h * 2.2f) sel = -1;
    sel = sel < 0 ? (t->x < k->alt_rect.x ? 0 : -1) : (sel >= k->alt_n ? k->alt_n - 1 : sel);
    if (sel != k->alt_sel && sel >= 0) kbd_emit_haptic(k, KBD_HAPTIC_SELECTION);   /* a detent per choice */
    k->alt_sel = sel;
}

void kbd_keys_move(Kbd *k, KbdTouch *t) {
    if (t->slot < 0) return;
    const KbdKeyDef *d = def_of(k, t->slot);
    float dx = t->x - t->x0, dy = t->y - t->y0;
    if (dy > k->lay.key_h * 1.6f && fabsf(dx) < k->lay.key_h && k->lay.keys[t->slot].row <= 1 &&
        !t->committed) {                      /* swipe down from the top: dismiss */
        t->committed = 1;
        k->preview = -1;
        kbd_show(k, 0);
        return;
    }
    if (d->kind == KIND_SPACE && fabsf(dx) > k->lay.unit * 0.6f && !t->committed) {
        kbd_cursor_begin(k, t);
        return;
    }
    if (d->kind != KIND_CHAR || t->committed) return;
    int s = hit(k, t->x, t->y);
    if (s >= 0 && s != t->slot && def_of(k, s)->kind == KIND_CHAR) {
        t->slot = s;                         /* slide to correct */
        k->preview = s;
        t->t0 = k->now;                      /* restart long-press on the new key */
    }
}

void kbd_keys_up(Kbd *k, KbdTouch *t) {
    if (t->role == ROLE_ALT) return;
    if (t->slot < 0) return;
    const KbdKeyDef *d = def_of(k, t->slot);
    if (k->preview == t->slot) k->preview = -1;
    press_sound(k, t->slot, 0);
    if (d->kind == KIND_MOD) {
        if (d->mod == KBD_MOD_CAPS) return;
        k->held &= ~d->mod;
        if (k->mod_used) return;             /* it was a chord: no latch */
        kbd_emit_haptic(k, KBD_HAPTIC_MODIFIER);
        if (d->mod == KBD_MOD_SHIFT) {
            if (k->caps) { k->caps = 0; k->latched &= ~KBD_MOD_SHIFT; return; }
            if (k->now - k->shift_tap < DOUBLE_TAP_MS && k->shift_tap) {
                k->caps = 1;
                k->latched &= ~KBD_MOD_SHIFT;
                k->shift_tap = 0;
                return;
            }
            k->shift_tap = k->now ? k->now : 1;
        }
        k->latched ^= d->mod;
        return;
    }
    if (!t->committed) commit(k, t->slot);
}

void kbd_alt_release(Kbd *k) {
    if (k->alt_sel >= 0 && k->alt_sel < k->alt_n) {
        const char *s = k->alts[k->alt_sel];
        if (k->dead) k->dead = 0;
        kbd_emit_text(k, s);
        kbd_emit_sound(k, KBD_SND_RELEASE, k->alt_rect.x + k->alt_rect.w * 0.5f);
        k->last_char = k->now ? k->now : 1;
        clear_one_shot(k);
    }
    k->alt_open = 0;
    k->alt_sel = -1;
}

void kbd_keys_tick(Kbd *k) {
    for (int i = 0; i < KBD_MAX_TOUCH; i++) {
        KbdTouch *t = &k->touch[i];
        if (!t->active) continue;
        if (t->role == ROLE_ALT) { update_alt(k, t); continue; }
        if (t->role != ROLE_KEY || t->slot < 0) continue;
        const KbdKeyDef *d = def_of(k, t->slot);
        uint32_t held = k->now - t->t0;
        if (repeats(d) && t->committed && (int32_t)(k->now - t->next_repeat) >= 0) {
            t->repeats++;
            int word = d->code == KBD_K_BACKSPACE && t->repeats > WORD_DELETE_AFTER;
            if (word && t->repeats == WORD_DELETE_AFTER + 1) kbd_emit_haptic(k, KBD_HAPTIC_SELECTION);
            int mods = kbd_mods(k) | (word ? KBD_MOD_OPT : 0);
            kbd_emit_key(k, d->code, 1, mods);
            kbd_emit_key(k, d->code, 0, mods);
            k->anim[t->slot].press = 0.7f;   /* each repeat nudges the cap */
            t->next_repeat = k->now + (uint32_t)(word ? k->set.repeat_rate_ms * 2 : k->set.repeat_rate_ms);
        } else if (d->kind == KIND_CHAR && !t->committed && held >= LONG_PRESS_MS && !k->alt_open &&
                   !(kbd_mods(k) & KBD_MOD_OPT)) {
            open_alternates(k, t);
            if (t->role != ROLE_ALT) t->t0 = k->now + 100000u;  /* nothing to offer */
        } else if (d->kind == KIND_SPACE && !t->committed && held >= CURSOR_HOLD_MS && !t->moved) {
            kbd_cursor_begin(k, t);
        }
    }
}
