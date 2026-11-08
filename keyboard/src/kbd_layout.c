/* Layouts for the three postures.
 *
 * COVER: a phone-width Platinum keyboard with an accessory strip (esc,
 *   control, tab, arrow keys) because Finder and SimpleText users steer with
 *   arrows. Command sits beside the space bar, Option beside Command.
 * OPEN: the inner screen held flat. Split puts two thumb halves at the edges
 *   with a thumb trackpad in the middle, keeping keys off the vertical crease.
 *   Unsplit is the full desk layout.
 * FLEX: the inner screen bent like a laptop. The bottom half becomes a
 *   portable deck: full layout pushed back, trackpad and one wide button
 *   centered in front, everything clear of the horizontal fold.
 */
#include "kbd_int.h"
#include <math.h>
#include <string.h>

typedef struct { uint16_t id; float w; const char *glyph; uint8_t fill; } Item;
#define K(id) { id, 1.0f, NULL, 0 }
#define KW(id, w) { id, w, NULL, 0 }
#define KF(id) { id, 0.0f, NULL, 1 }
#define G(s) { KK_GLYPH, 1.0f, s, 0 }
#define GAP(w) { KK_NONE, w, NULL, 0 }
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

enum { AL_LEFT, AL_CENTER, AL_RIGHT };

/* Lay one row of items into [x, x + units*unit). Gaps are inside each cell so
 * neighbouring hit rects tile exactly when expanded by gap/2. */
static void row(KbdLayout *L, int r, const Item *it, int n, float x, float y,
                float units, int align, float h) {
    float used = 0, fills = 0;
    for (int i = 0; i < n; i++) {
        if (it[i].fill) fills += 1;
        else used += it[i].w;
    }
    float fill_w = fills > 0 && units > used ? (units - used) / fills : 0;
    float total = used + fill_w * fills;
    float cx = x;
    if (align == AL_CENTER) cx += (units - total) * L->unit * 0.5f;
    else if (align == AL_RIGHT) cx += (units - total) * L->unit;
    int col = 0;
    for (int i = 0; i < n && L->n < KBD_MAX_KEYS; i++) {
        float w = (it[i].fill ? fill_w : it[i].w) * L->unit;
        if (it[i].id != KK_NONE) {
            KbdSlot *s = &L->keys[L->n++];
            memset(s, 0, sizeof *s);
            s->id = it[i].id;
            s->row = (uint8_t)r;
            s->col = (uint8_t)col++;
            if (it[i].glyph) {
                strncpy(s->glyph, it[i].glyph, sizeof s->glyph - 1);
            }
            float g = L->gap * 0.5f;
            s->r = (KRect){ cx + g, y, w - L->gap, h };
        }
        cx += w;
    }
}

/* ---- COVER ------------------------------------------------------------ */
static const Item cv_strip[] = { KW(KK_ESC, 1.2f), KW(KK_CTRL, 1.5f), KW(KK_TAB, 1.2f), GAP(1.6f),
                                 K(KK_LEFT), K(KK_UP), K(KK_DOWN), K(KK_RIGHT), GAP(0.5f) };
static const Item cv_r1[] = { K(KK_Q), K(KK_W), K(KK_E), K(KK_R), K(KK_T), K(KK_Y), K(KK_U),
                              K(KK_I), K(KK_O), K(KK_P) };
static const Item cv_r2[] = { K(KK_A), K(KK_S), K(KK_D), K(KK_F), K(KK_G), K(KK_H), K(KK_J),
                              K(KK_K), K(KK_L) };
static const Item cv_r3[] = { KW(KK_SHIFT, 1.35f), GAP(0.15f), K(KK_Z), K(KK_X), K(KK_C), K(KK_V),
                              K(KK_B), K(KK_N), K(KK_M), GAP(0.15f), KW(KK_DELETE, 1.35f) };
static const Item cv_r4[] = { KW(KK_LAYER_NUM, 1.25f), KW(KK_OPT, 1.15f), KW(KK_CMD, 1.35f),
                              KF(KK_SPACE), KW(KK_RETURN, 2.1f) };
static const Item nm_r1[] = { K(KK_1), K(KK_2), K(KK_3), K(KK_4), K(KK_5), K(KK_6), K(KK_7),
                              K(KK_8), K(KK_9), K(KK_0) };
static const Item nm_r2[] = { G("-"), G("/"), G(":"), G(";"), G("("), G(")"), G("$"), G("&"), G("@") };
static const Item nm_r3[] = { KW(KK_LAYER_SYM, 1.35f), GAP(0.15f), G("."), G(","), G("?"), G("!"),
                              G("'"), G("\""), G("%"), GAP(0.15f), KW(KK_DELETE, 1.35f) };
static const Item nm_r4[] = { KW(KK_LAYER_ABC, 1.25f), KW(KK_OPT, 1.15f), KW(KK_CMD, 1.35f),
                              KF(KK_SPACE), KW(KK_RETURN, 2.1f) };
static const Item sy_r1[] = { G("["), G("]"), G("{"), G("}"), G("#"), G("%"), G("^"), G("*"),
                              G("+"), G("=") };
static const Item sy_r2[] = { G("_"), G("\\"), G("|"), G("~"), G("<"), G(">"), G("`"),
                              G("\xC2\xA3"), G("\xE2\x80\xA2") };
static const Item sy_r3[] = { KW(KK_LAYER_NUM, 1.35f), GAP(0.15f), G("."), G(","), G("?"), G("!"),
                              G("'"), G("\""), G("\xE2\x80\xA6"), GAP(0.15f), KW(KK_DELETE, 1.35f) };

static void build_cover(KbdLayout *L, KbdLayer layer, int W, int H, float dp) {
    float side = 3 * dp;
    L->unit = (W - 2 * side) / 10.0f;
    L->gap = fmaxf(4 * dp, L->unit * 0.14f);
    L->key_h = fminf(L->unit * 1.18f, 48 * dp);
    float vgap = fmaxf(8 * dp, L->unit * 0.26f);
    float strip_h = L->key_h * 0.7f, top = 6 * dp, bottom = 18 * dp;
    float deck_h = top + strip_h + vgap + 4 * (L->key_h + vgap) + bottom;
    float y = H - deck_h;
    L->deck = (KRect){ 0, y, (float)W, deck_h };
    y += top;
    L->strip = (KRect){ 0, y - top * 0.5f, (float)W, strip_h + top };
    row(L, 0, cv_strip, N(cv_strip), side, y, 10, AL_LEFT, strip_h);
    y += strip_h + vgap;
    float p = L->key_h + vgap, h = L->key_h;
    if (layer == KBD_LAYER_LETTERS) {
        row(L, 1, cv_r1, N(cv_r1), side, y, 10, AL_CENTER, h);
        row(L, 2, cv_r2, N(cv_r2), side, y + p, 10, AL_CENTER, h);
        row(L, 3, cv_r3, N(cv_r3), side, y + 2 * p, 10, AL_CENTER, h);
        row(L, 4, cv_r4, N(cv_r4), side, y + 3 * p, 10, AL_CENTER, h);
    } else if (layer == KBD_LAYER_NUMBERS) {
        row(L, 1, nm_r1, N(nm_r1), side, y, 10, AL_CENTER, h);
        row(L, 2, nm_r2, N(nm_r2), side, y + p, 10, AL_CENTER, h);
        row(L, 3, nm_r3, N(nm_r3), side, y + 2 * p, 10, AL_CENTER, h);
        row(L, 4, nm_r4, N(nm_r4), side, y + 3 * p, 10, AL_CENTER, h);
    } else {
        row(L, 1, sy_r1, N(sy_r1), side, y, 10, AL_CENTER, h);
        row(L, 2, sy_r2, N(sy_r2), side, y + p, 10, AL_CENTER, h);
        row(L, 3, sy_r3, N(sy_r3), side, y + 2 * p, 10, AL_CENTER, h);
        row(L, 4, nm_r4, N(nm_r4), side, y + 3 * p, 10, AL_CENTER, h);
    }
}

/* ---- Full desk layout (OPEN unsplit, FLEX) ----------------------------- */
static const Item fu_r1[] = { K(KK_ESC), K(KK_1), K(KK_2), K(KK_3), K(KK_4), K(KK_5), K(KK_6),
                              K(KK_7), K(KK_8), K(KK_9), K(KK_0), K(KK_MINUS), K(KK_EQUAL),
                              KF(KK_DELETE) };
static const Item fu_r2[] = { KW(KK_TAB, 1.5f), K(KK_Q), K(KK_W), K(KK_E), K(KK_R), K(KK_T),
                              K(KK_Y), K(KK_U), K(KK_I), K(KK_O), K(KK_P), K(KK_LBRACKET),
                              K(KK_RBRACKET), KF(KK_BACKSLASH) };
static const Item fu_r3[] = { KW(KK_CAPS, 1.75f), K(KK_A), K(KK_S), K(KK_D), K(KK_F), K(KK_G),
                              K(KK_H), K(KK_J), K(KK_K), K(KK_L), K(KK_SEMI), K(KK_QUOTE),
                              KF(KK_RETURN) };
static const Item fu_r4[] = { KW(KK_SHIFT, 2.25f), K(KK_Z), K(KK_X), K(KK_C), K(KK_V), K(KK_B),
                              K(KK_N), K(KK_M), K(KK_COMMA), K(KK_PERIOD), K(KK_SLASH),
                              KF(KK_RSHIFT) };
static const Item fu_r5[] = { K(KK_CTRL), K(KK_OPT), KW(KK_CMD, 1.25f),
                              K(KK_GRAVE), KF(KK_SPACE), KW(KK_RCMD, 1.25f), K(KK_ROPT),
                              KW(KK_LEFT, 0.875f), KW(KK_DOWN, 0.875f), KW(KK_UP, 0.875f),
                              KW(KK_RIGHT, 0.875f) };

static void build_full(KbdLayout *L, float x, float y, float h_row, float vgap) {
    float p = h_row + vgap;
    row(L, 1, fu_r1, N(fu_r1), x, y, 15, AL_LEFT, h_row);
    row(L, 2, fu_r2, N(fu_r2), x, y + p, 15, AL_LEFT, h_row);
    row(L, 3, fu_r3, N(fu_r3), x, y + 2 * p, 15, AL_LEFT, h_row);
    row(L, 4, fu_r4, N(fu_r4), x, y + 3 * p, 15, AL_LEFT, h_row);
    row(L, 5, fu_r5, N(fu_r5), x, y + 4 * p, 15, AL_LEFT, h_row);
}

static void build_open_full(KbdLayout *L, int W, int H, float dp) {
    float side = 8 * dp;
    L->unit = fminf((W - 2 * side) / 15.0f, 66 * dp);
    L->gap = fmaxf(4 * dp, L->unit * 0.1f);
    L->key_h = L->unit * 0.9f;
    float vgap = L->unit * 0.12f, top = 10 * dp, bottom = 16 * dp;
    float deck_h = top + 5 * (L->key_h + vgap) + bottom;
    L->deck = (KRect){ 0, H - deck_h, (float)W, deck_h };
    float x = (W - 15 * L->unit) * 0.5f;
    build_full(L, x, H - deck_h + top, L->key_h, vgap);
}

/* ---- OPEN split ------------------------------------------------------- */
static const Item sl_r1[] = { KF(KK_ESC), K(KK_1), K(KK_2), K(KK_3), K(KK_4), K(KK_5) };
static const Item sl_r2[] = { KF(KK_TAB), K(KK_Q), K(KK_W), K(KK_E), K(KK_R), K(KK_T) };
static const Item sl_r3[] = { KF(KK_CAPS), K(KK_A), K(KK_S), K(KK_D), K(KK_F), K(KK_G) };
static const Item sl_r4[] = { KF(KK_SHIFT), K(KK_Z), K(KK_X), K(KK_C), K(KK_V), K(KK_B) };
static const Item sl_r5[] = { K(KK_LAYER_NUM), K(KK_OPT), KW(KK_CMD, 1.25f), KF(KK_SPACE) };
static const Item sr_r1[] = { K(KK_6), K(KK_7), K(KK_8), K(KK_9), K(KK_0), KF(KK_DELETE) };
static const Item sr_r2[] = { K(KK_Y), K(KK_U), K(KK_I), K(KK_O), K(KK_P), KF(KK_MINUS) };
static const Item sr_r3[] = { K(KK_H), K(KK_J), K(KK_K), K(KK_L), K(KK_SEMI), KF(KK_RETURN) };
static const Item sr_r4[] = { K(KK_N), K(KK_M), K(KK_COMMA), K(KK_PERIOD), K(KK_SLASH), KF(KK_RSHIFT) };
static const Item sr_r5[] = { KF(KK_SPACE2), KW(KK_RCMD, 1.25f), KW(KK_LEFT, 0.75f),
                              KW(KK_DOWN, 0.75f), KW(KK_UP, 0.75f), KW(KK_RIGHT, 0.75f) };

/* Numbers layer for the split halves: the letter slots carry the symbols a
 * full layout has but the halves lack. */
static const char *const split_num[][2] = {
    { "q", "`" }, { "w", "[" }, { "e", "]" }, { "r", "{" }, { "t", "}" },
    { "y", "=" }, { "u", "\\" }, { "i", "|" }, { "o", "'" }, { "p", "\"" },
    { "a", "(" }, { "s", ")" }, { "d", "<" }, { "f", ">" }, { "g", "~" },
    { "h", "+" }, { "j", "*" }, { "k", "&" }, { "l", "\xE2\x80\xA2" },
    { "z", "!" }, { "x", "@" }, { "c", "#" }, { "v", "$" }, { "b", "%" },
    { "n", "^" }, { "m", "_" },
};

static void build_split(KbdLayout *L, KbdLayer layer, int W, int H, float dp) {
    float side = 6 * dp;
    L->unit = fminf((W * 0.38f) / 6.5f, 54 * dp);
    L->gap = fmaxf(4 * dp, L->unit * 0.12f);
    L->key_h = L->unit * 0.95f;
    float vgap = L->unit * 0.16f, top = 10 * dp, bottom = 18 * dp;
    float p = L->key_h + vgap, h = L->key_h;
    float deck_h = top + 5 * p + bottom;
    float y = H - deck_h + top;
    L->deck = (KRect){ 0, H - deck_h, (float)W, deck_h };
    const Item *lr[] = { sl_r1, sl_r2, sl_r3, sl_r4, sl_r5 };
    const Item *rr[] = { sr_r1, sr_r2, sr_r3, sr_r4, sr_r5 };
    int ln[] = { N(sl_r1), N(sl_r2), N(sl_r3), N(sl_r4), N(sl_r5) };
    int rn[] = { N(sr_r1), N(sr_r2), N(sr_r3), N(sr_r4), N(sr_r5) };
    float rx = W - side - 6.5f * L->unit;
    for (int r = 0; r < 5; r++) {
        row(L, r + 1, lr[r], ln[r], side, y + r * p, 6.5f, AL_LEFT, h);
        row(L, r + 1, rr[r], rn[r], rx, y + r * p, 6.5f, AL_RIGHT, h);
    }
    if (layer != KBD_LAYER_LETTERS) {
        for (int i = 0; i < L->n; i++) {
            KbdSlot *s = &L->keys[i];
            if (s->id == KK_LAYER_NUM) s->id = KK_LAYER_ABC;
            const char *lab = kbd_keydefs[s->id].label;
            for (int j = 0; j < N(split_num); j++) {
                if (strcmp(lab, split_num[j][0]) == 0) {
                    strncpy(s->glyph, split_num[j][1], sizeof s->glyph - 1);
                    s->id = KK_GLYPH;
                    break;
                }
            }
        }
    }
    /* Thumb trackpad in the gutter, bottom three rows, clear of the crease
     * visually by a hairline the renderer draws down its center. */
    float gx0 = side + 6.5f * L->unit + 3 * L->gap, gx1 = rx - 3 * L->gap;
    if (gx1 - gx0 > 2.2f * L->unit) {
        float pw = fminf(gx1 - gx0, 4.5f * L->unit);
        L->pad = (KRect){ (W - pw) * 0.5f, y + 2 * p, pw, 3 * p - vgap };
    }
}

/* ---- FLEX ------------------------------------------------------------- */
static void build_flex(KbdLayout *L, int W, int H, float dp) {
    float fold = H * 0.5f, margin = 10 * dp, side = 8 * dp;
    float deck_y = fold + margin, deck_h = H - deck_y;
    L->unit = fminf((W - 2 * side) / 15.0f, deck_h / 8.7f);
    L->unit = fminf(L->unit, 60 * dp);
    L->gap = fmaxf(3 * dp, L->unit * 0.1f);
    L->key_h = L->unit * 0.88f;
    float vgap = L->unit * 0.12f;
    L->deck = (KRect){ 0, fold, (float)W, H - fold };
    L->crease = (KRect){ 0, fold - margin, (float)W, 2 * margin };
    float x = (W - 15 * L->unit) * 0.5f, y = deck_y + 0.4f * L->unit;
    build_full(L, x, y, L->key_h, vgap);
    float pad_top = y + 5 * L->unit + 0.35f * L->unit;
    float pw = 5.0f * L->unit, room = H - pad_top - 0.3f * L->unit;
    float bh = fminf(0.75f * L->unit, room * 0.22f);
    float ph = fminf(2.6f * L->unit, room - bh - 0.15f * L->unit);
    if (ph > L->unit) {
        L->pad = (KRect){ (W - pw) * 0.5f, pad_top, pw, ph };
        L->pad_button = (KRect){ L->pad.x, pad_top + ph + 0.15f * L->unit, pw, bh };
    }
}

void kbd_layout_build(KbdLayout *L, KbdPosture p, int split, KbdLayer layer, int w, int h, float dp) {
    memset(L, 0, sizeof *L);
    if (w <= 0 || h <= 0) return;
    if (dp <= 0) dp = 1;
    if (p == KBD_COVER) build_cover(L, layer, w, h, dp);
    else if (p == KBD_OPEN && split) build_split(L, layer, w, h, dp);
    else if (p == KBD_OPEN) build_open_full(L, w, h, dp);
    else build_flex(L, w, h, dp);
    if (p == KBD_OPEN && split) L->crease = (KRect){ w * 0.5f - 6 * dp, 0, 12 * dp, (float)h };
}

/* Expanded rects tile the deck; outside them, take the nearest key center in
 * key-size-normalized distance, up to slop. */
int kbd_layout_hit(const KbdLayout *L, float x, float y, float slop) {
    float g = L->gap * 0.5f;
    for (int i = 0; i < L->n; i++) {
        KRect r = L->keys[i].r;
        KRect e = { r.x - g, r.y - g * 1.6f, r.w + 2 * g, r.h + 3.2f * g };
        if (krect_in(e, x, y)) return i;
    }
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < L->n; i++) {
        KRect r = L->keys[i].r;
        float dx = fmaxf(fmaxf(r.x - x, x - (r.x + r.w)), 0);
        float dy = fmaxf(fmaxf(r.y - y, y - (r.y + r.h)), 0);
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best >= 0 && bd <= slop * slop ? best : -1;
}
