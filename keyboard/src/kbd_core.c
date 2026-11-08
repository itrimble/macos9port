/* Keyboard state, event ring, touch routing and the hardware-keyboard path. */
#include "kbd_int.h"
#include "kbd_gfx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

Kbd *kbd_create(float px_per_dp) {
    Kbd *k = calloc(1, sizeof *k);
    if (!k) return NULL;
    k->dp = px_per_dp > 0 ? px_per_dp : 1;
    k->posture = KBD_COVER;
    k->split = 1;
    k->preview = -1;
    k->alt_sel = -1;
    k->rng = 0x9E3779B9u;
    k->set = (KbdSettings){ .repeat_delay_ms = 500, .repeat_rate_ms = 83, .tap_to_click = 1,
                            .sounds = 1, .tracking = 1.0f, .haptics = 1, .haptic_strength = 1.0f,
                            .click_pressure = 1, .force_click = 1 };
    return k;
}

void kbd_destroy(Kbd *k) { free(k); }
KbdSettings *kbd_settings(Kbd *k) { return &k->set; }
KbdPosture kbd_posture(const Kbd *k) { return k->posture; }
int kbd_split(const Kbd *k) { return k->split; }
int kbd_skin(const Kbd *k) { return k->skin; }

uint32_t kbd_rand(Kbd *k) {
    uint32_t x = k->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return k->rng = x;
}

void kbd_emit(Kbd *k, const KbdEvent *e) {
    int next = (k->rhead + 1) % KBD_RING;
    if (next == k->rtail) k->rtail = (k->rtail + 1) % KBD_RING;  /* drop oldest */
    k->ring[k->rhead] = *e;
    k->rhead = next;
}

int kbd_poll_event(Kbd *k, KbdEvent *out) {
    if (k->rtail == k->rhead) return 0;
    *out = k->ring[k->rtail];
    k->rtail = (k->rtail + 1) % KBD_RING;
    return 1;
}

void kbd_emit_key(Kbd *k, int32_t code, int down, int mods) {
    KbdEvent e = { .kind = KBD_EV_KEY, .key = code, .down = down, .mods = mods };
    kbd_emit(k, &e);
}

void kbd_emit_text(Kbd *k, const char *utf8) {
    if (!utf8 || !*utf8) return;
    KbdEvent e = { .kind = KBD_EV_TEXT };
    strncpy(e.text, utf8, sizeof e.text - 1);
    kbd_emit(k, &e);
}

/* Each press gets a slightly different variant, pitch and level so fast
 * typing never sounds like one sample machine-gunned. */
void kbd_emit_sound(Kbd *k, int sound, float x) {
    if (!k->set.sounds) return;
    uint32_t r = kbd_rand(k);
    KbdEvent e = { .kind = KBD_EV_SOUND, .sound = sound, .variant = (int)(r & 3) };
    float w = k->sw > 0 ? (float)k->sw : 1;
    e.pan = fmaxf(-0.7f, fminf(0.7f, (x / w - 0.5f) * 1.2f));
    e.pitch = 1.0f + ((float)((r >> 8) & 255) / 255.0f - 0.5f) * 0.05f;
    e.gain = 0.85f + (float)((r >> 16) & 255) / 255.0f * 0.15f;
    kbd_emit(k, &e);
}


int kbd_mods(const Kbd *k) { return k->latched | k->held | k->hw_mods; }

void kbd_relayout(Kbd *k, int by_position) {
    KbdLayout old = k->lay;
    kbd_layout_build(&k->lay, k->posture, k->split, k->layer, k->sw, k->sh, k->dp);
    if (by_position >= 0) {
        kbd_anim_relayout(k, &old, by_position);
    } else {
        memset(k->anim, 0, sizeof k->anim);
        k->nghost = 0;
        k->morph = (Spring){ 1, 0 };
        k->deck_from = k->lay.deck.y;
        for (int i = 0; i < k->lay.n; i++) k->anim[i].t = 1;
    }
    k->preview = -1;
}

static void set_visible(Kbd *k, int on) {
    if (k->want_show == on) return;
    k->want_show = on;
    KbdEvent e = { .kind = KBD_EV_VISIBILITY, .down = on };
    kbd_emit(k, &e);
}

void kbd_set_surface(Kbd *k, int w, int h) {
    if (w == k->sw && h == k->sh) return;
    k->sw = w;
    k->sh = h;
    kbd_touch_cancel_all(k);
    kbd_relayout(k, -1);
    if (!k->set.reduce_motion) k->show.x = 0;  /* a new screen: slide in */
}

void kbd_set_posture(Kbd *k, KbdPosture p) {
    if (p < 0 || p >= KBD_POSTURE_COUNT || p == k->posture) return;
    kbd_touch_cancel_all(k);
    k->posture = p;
    k->layer = KBD_LAYER_LETTERS;
    kbd_relayout(k, 0);
}

static KRect turn_cw(KRect r, int h0) {
    return (KRect){ (float)h0 - r.y - r.h, r.x, r.h, r.w };
}

void kbd_rotate(Kbd *k, int w, int h, int turns, KbdPosture p) {
    if (p < 0 || p >= KBD_POSTURE_COUNT) p = k->posture;
    turns = ((turns % 4) + 4) % 4;
    KbdLayout old = k->lay;
    KRect pad = kbd_anim_pad(k, 0, NULL), btn = kbd_anim_pad(k, 1, NULL);
    for (int i = 0; i < old.n; i++) old.keys[i].r = kbd_anim_rect(k, i);
    int w0 = k->sw, h0 = k->sh;
    for (int t = 0; t < turns; t++) {
        for (int i = 0; i < old.n; i++) old.keys[i].r = turn_cw(old.keys[i].r, h0);
        pad = turn_cw(pad, h0);
        btn = turn_cw(btn, h0);
        int tmp = w0; w0 = h0; h0 = tmp;
    }
    kbd_touch_cancel_all(k);
    for (int i = 0; i < KBD_MAX_KEYS; i++) { k->anim[i].t = 1; k->anim[i].tv = 0; k->anim[i].delay = 0; }
    k->nghost = 0;
    old.pad = pad;
    old.pad_button = btn;
    k->pad_from = pad;
    k->btn_from = btn;
    k->morph = (Spring){ 1, 0 };
    k->lay = old;
    k->sw = w;
    k->sh = h;
    k->posture = p;
    k->layer = KBD_LAYER_LETTERS;
    kbd_relayout(k, 0);
    if (k->set.reduce_motion) return;
    /* The deck is there at once, a little low, and settles as the keys land. */
    k->deck_from = k->lay.deck.y + k->lay.deck.h * 0.12f;
}

void kbd_set_split(Kbd *k, int split) {
    split = !!split;
    if (split == k->split) return;
    k->split = split;
    if (k->posture == KBD_OPEN) {
        kbd_touch_cancel_all(k);
        kbd_relayout(k, 0);
    }
}

void kbd_set_layer(Kbd *k, KbdLayer layer) {
    if (layer == k->layer) return;
    k->layer = layer;
    kbd_relayout(k, 1);
}

void kbd_set_skin(Kbd *k, int skin) {
    int n = kbd_skin_count();
    k->skin = ((skin % n) + n) % n;
}

void kbd_show(Kbd *k, int show) { set_visible(k, !!show); }

int kbd_visible(const Kbd *k) { return k->want_show && !k->hw_active; }

int kbd_occluded_height(const Kbd *k) {
    if (k->hw_active) return k->want_show ? (int)(36 * k->dp * k->hud_s.x + 0.5f) : 0;
    return (int)(k->lay.deck.h * fmaxf(0, fminf(1, k->show.x)) + 0.5f);
}

static KbdTouch *find_touch(Kbd *k, int64_t fid) {
    for (int i = 0; i < KBD_MAX_TOUCH; i++)
        if (k->touch[i].active && k->touch[i].fid == fid) return &k->touch[i];
    return NULL;
}

/* Strip shown while a hardware keyboard is in use: a button to bring the
 * on-screen keyboard back. */
KRect kbd_hud_button(const Kbd *k) {
    float h = 36 * k->dp, bw = 110 * k->dp;
    return (KRect){ k->sw - bw - 6 * k->dp, k->sh - h + 5 * k->dp, bw, h - 10 * k->dp };
}

int kbd_touch_down(Kbd *k, int64_t fid, float x, float y, uint32_t ms) {
    k->now = ms;
    if (k->hw_active && k->want_show) {
        if (krect_in(kbd_hud_button(k), x, y)) {
            k->hw_active = 0;
            kbd_emit_sound(k, KBD_SND_PRESS, x);
            kbd_emit_haptic(k, KBD_HAPTIC_TAP);
            return 1;
        }
        return y >= k->sh - 36 * k->dp;
    }
    if (!k->want_show || k->show.x < 0.85f) return 0;
    if (!krect_in(k->lay.deck, x, y)) return 0;
    KbdTouch *t = NULL;
    for (int i = 0; i < KBD_MAX_TOUCH; i++)
        if (!k->touch[i].active) { t = &k->touch[i]; break; }
    if (!t) return 1;
    *t = (KbdTouch){ .active = 1, .fid = fid, .x = x, .y = y, .x0 = x, .y0 = y, .lx = x,
                     .ly = y, .t0 = ms, .slot = -1 };
    if (k->alt_open) { t->role = ROLE_NONE; return 1; }
    if (k->cursor_mode) {             /* second finger while space-tracking: click */
        t->role = ROLE_PAD_BUTTON;
        KbdEvent e = { .kind = KBD_EV_BUTTON, .button = 1, .down = 1, .clicks = 1,
                       .mods = kbd_mods(k) };
        kbd_emit(k, &e);
        kbd_emit_sound(k, KBD_SND_PAD_CLICK, x);
        kbd_emit_haptic(k, KBD_HAPTIC_CLICK);
        return 1;
    }
    if (k->lay.pad.w > 0 && (krect_in(k->lay.pad, x, y) ||
        (k->lay.pad_button.w > 0 && krect_in(k->lay.pad_button, x, y))))
        return kbd_pad_down(k, t);
    return kbd_keys_down(k, t);
}

int kbd_touch_move(Kbd *k, int64_t fid, float x, float y, uint32_t ms) {
    k->now = ms;
    KbdTouch *t = find_touch(k, fid);
    if (!t) return 0;
    float dx = x - t->lx, dy = y - t->ly;
    t->x = x; t->y = y; t->lx = x; t->ly = y;
    if (fabsf(x - t->x0) + fabsf(y - t->y0) > 6 * k->dp) t->moved = 1;
    (void)dx; (void)dy;
    if (t->role == ROLE_PAD || t->role == ROLE_CURSOR) kbd_pad_move(k, t);
    else if (t->role == ROLE_KEY) kbd_keys_move(k, t);
    return 1;
}

int kbd_touch_up(Kbd *k, int64_t fid, float x, float y, uint32_t ms) {
    k->now = ms;
    KbdTouch *t = find_touch(k, fid);
    if (!t) return 0;
    t->x = x; t->y = y;
    if (t->role == ROLE_KEY) kbd_keys_up(k, t);
    else if (t->role == ROLE_ALT) kbd_alt_release(k);
    else if (t->role == ROLE_PAD || t->role == ROLE_PAD_BUTTON || t->role == ROLE_CURSOR)
        kbd_pad_up(k, t);
    t->active = 0;
    return 1;
}

void kbd_touch_force(Kbd *k, int64_t fid, float pressure, uint32_t ms) {
    k->now = ms;
    KbdTouch *t = find_touch(k, fid);
    if (!t) return;
    if (pressure < 0) pressure = 0;
    if (pressure > 1) pressure = 1;
    /* Panels without pressure report a constant 1.0 (or 0): only a value in
     * between proves the hardware measures force. */
    if (pressure > 0.02f && pressure < 0.98f) k->pad.pressure_seen = 1;
    t->pressure = pressure;
    t->ps += (pressure - t->ps) * 0.55f;
    if (t->role == ROLE_PAD || t->role == ROLE_CURSOR) kbd_pad_force(k, t);
}

void kbd_touch_cancel_all(Kbd *k) {
    for (int i = 0; i < KBD_MAX_TOUCH; i++) k->touch[i].active = 0;
    if (k->pad.button_down || k->pad.dragging) {
        KbdEvent e = { .kind = KBD_EV_BUTTON, .button = 1, .down = 0 };
        kbd_emit(k, &e);
    }
    int seen = k->pad.pressure_seen;
    k->pad = (KbdPad){0};
    k->pad.pressure_seen = seen;
    k->held = 0;
    k->alt_open = 0;
    k->cursor_mode = 0;
    k->preview = -1;
}

/* ---- Hardware keyboard ----------------------------------------------- */
void kbd_hw_attached(Kbd *k, int attached) {
    k->hw_attached = !!attached;
    if (!attached) { k->hw_active = 0; k->hw_mods = 0; k->sheet = 0; }
}

int kbd_hw_active(const Kbd *k) { return k->hw_active; }

void kbd_set_shortcuts(Kbd *k, const char *const (*pairs)[2], int n) {
    k->nshort = 0;
    for (int i = 0; i < n && i < KBD_MAX_SHORTCUTS; i++) {
        k->shortcuts[i][0] = pairs[i][0];
        k->shortcuts[i][1] = pairs[i][1];
        k->nshort++;
    }
}

static int hw_modifier(const Kbd *k, int usage) {
    int pc = k->set.pc_modifiers;
    switch (usage) {
    case 224: case 228: return KBD_MOD_CTRL;
    case 225: case 229: return KBD_MOD_SHIFT;
    case 226: case 230: return pc ? KBD_MOD_CMD : KBD_MOD_OPT;   /* Alt */
    case 227: case 231: return pc ? KBD_MOD_OPT : KBD_MOD_CMD;   /* GUI / Windows */
    default: return 0;
    }
}

/* Observe a physical key. The host still delivers the native event; this
 * drives auto-hide, the shortcut sheet, Caps Lock and optional click-along. */
int kbd_hw_key(Kbd *k, int usage, int down, uint32_t ms) {
    k->now = ms;
    int mod = hw_modifier(k, usage);
    if (mod) {
        if (down) k->hw_mods |= mod; else k->hw_mods &= ~mod;
        if (mod == KBD_MOD_CMD) k->hw_cmd_since = down ? (ms ? ms : 1) : 0;
        if (!down && mod == KBD_MOD_CMD) k->sheet = 0;
    } else if (down) {
        k->hw_cmd_since = 0;      /* a chord, not a peek at the shortcut sheet */
        k->sheet = 0;
    }
    if (usage == 57 && down) k->hw_caps = !k->hw_caps;
    int fkey = usage >= 58 && usage <= 69;
    if (down && !fkey && !mod) {         /* a modifier alone is not typing */
        if (!k->hw_active) kbd_touch_cancel_all(k);
        k->hw_active = 1;
        k->hw_attached = 1;
        k->hw_last = ms;
    }
    if (k->set.hw_click_along && !fkey) {
        int wide = usage == 44 || usage == 40 || usage == 42 || usage == 225 || usage == 229;
        int s = wide ? (down ? KBD_SND_WIDE_PRESS : KBD_SND_WIDE_RELEASE)
                     : (down ? KBD_SND_PRESS : KBD_SND_RELEASE);
        kbd_emit_sound(k, s, k->sw * 0.5f);
    }
    return 1;
}

/* ---- Frame tick ------------------------------------------------------- */
void kbd_tick(Kbd *k, uint32_t ms) {
    float dt = k->started ? (float)(uint32_t)(ms - k->now) / 1000.0f : 0;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0) dt = 0;
    k->started = 1;
    k->now = ms;
    if (k->hw_cmd_since && !k->sheet && ms - k->hw_cmd_since >= 700 && k->nshort > 0) k->sheet = 1;
    kbd_keys_tick(k);
    kbd_pad_tick(k, dt);
    kbd_anim_step(k, dt);
}

int kbd_load_fonts(const char *legend_ttf) {
    static const char *const fallback[] = {
        "/System/Library/Fonts/Geneva.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", NULL };
    const char *fb = NULL;
    for (int i = 0; fallback[i] && !fb; i++) {
        FILE *f = fopen(fallback[i], "rb");
        if (f) { fclose(f); fb = fallback[i]; }
    }
    return g_font_init(legend_ttf, fb);
}
