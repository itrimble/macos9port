/* Text: case mapping, classic Option dead-key composition, long-press
 * alternates, and the legend/output a slot has under the current modifiers. */
#include "kbd_int.h"
#include <string.h>

static int utf8_put(char *out, int cap, unsigned cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else {
        b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63));
        b[2] = (char)(0x80 | (cp & 63)); n = 3;
    }
    if (n >= cap) return 0;
    memcpy(out, b, (size_t)n);
    out[n] = 0;
    return n;
}

static unsigned utf8_get(const char *s, int *len) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *len = u[0] ? 1 : 0; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && u[1]) { *len = 2; return (unsigned)(u[0] & 31) << 6 | (u[1] & 63); }
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) {
        *len = 3;
        return (unsigned)(u[0] & 15) << 12 | (unsigned)(u[1] & 63) << 6 | (u[2] & 63);
    }
    *len = 1;
    return 0xFFFD;
}

static unsigned cp_upper(unsigned c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0x153) return 0x152;   /* oe */
    if (c == 0xFF) return 0x178;
    return c;
}

int kbd_utf8_upper(const char *in, char *out, int cap) {
    int used = 0;
    out[0] = 0;
    while (*in) {
        int len = 0;
        unsigned c = cp_upper(utf8_get(in, &len));
        int n = utf8_put(out + used, cap - used, c);
        if (!n || !len) break;
        used += n;
        in += len;
    }
    return used;
}

/* dead, base, result */
static const unsigned compose_tab[][3] = {
    { DEAD_ACUTE, 'a', 0xE1 }, { DEAD_ACUTE, 'e', 0xE9 }, { DEAD_ACUTE, 'i', 0xED },
    { DEAD_ACUTE, 'o', 0xF3 }, { DEAD_ACUTE, 'u', 0xFA }, { DEAD_ACUTE, 'y', 0xFD },
    { DEAD_GRAVE, 'a', 0xE0 }, { DEAD_GRAVE, 'e', 0xE8 }, { DEAD_GRAVE, 'i', 0xEC },
    { DEAD_GRAVE, 'o', 0xF2 }, { DEAD_GRAVE, 'u', 0xF9 },
    { DEAD_CIRC, 'a', 0xE2 }, { DEAD_CIRC, 'e', 0xEA }, { DEAD_CIRC, 'i', 0xEE },
    { DEAD_CIRC, 'o', 0xF4 }, { DEAD_CIRC, 'u', 0xFB },
    { DEAD_UML, 'a', 0xE4 }, { DEAD_UML, 'e', 0xEB }, { DEAD_UML, 'i', 0xEF },
    { DEAD_UML, 'o', 0xF6 }, { DEAD_UML, 'u', 0xFC }, { DEAD_UML, 'y', 0xFF },
    { DEAD_TILDE, 'a', 0xE3 }, { DEAD_TILDE, 'n', 0xF1 }, { DEAD_TILDE, 'o', 0xF5 },
};

const char *kbd_dead_mark(int dead) {
    switch (dead) {
    case DEAD_ACUTE: return "\xC2\xB4";
    case DEAD_GRAVE: return "`";
    case DEAD_CIRC: return "\xCB\x86";
    case DEAD_UML: return "\xC2\xA8";
    case DEAD_TILDE: return "\xCB\x9C";
    default: return "";
    }
}

/* Classic behaviour: a composable letter becomes the accented letter; space
 * gives the spacing accent; anything else gives the accent then the key. */
int kbd_compose(int dead, const char *base, char *out, int cap) {
    out[0] = 0;
    int len = 0;
    unsigned c = utf8_get(base, &len);
    int upper = c >= 'A' && c <= 'Z';
    unsigned lc = upper ? c + 32 : c;
    for (size_t i = 0; i < sizeof compose_tab / sizeof compose_tab[0]; i++) {
        if (compose_tab[i][0] == (unsigned)dead && compose_tab[i][1] == lc && base[len] == 0) {
            unsigned r = compose_tab[i][2];
            return utf8_put(out, cap, upper ? cp_upper(r) : r);
        }
    }
    const char *mark = kbd_dead_mark(dead);
    if (c == ' ') {
        if ((int)strlen(mark) >= cap) return 0;
        strcpy(out, mark);
        return (int)strlen(out);
    }
    if ((int)(strlen(mark) + strlen(base)) >= cap) return 0;
    strcpy(out, mark);
    strcat(out, base);
    return (int)strlen(out);
}

static const char *const alt_tab[][2] = {
    { "a", "\xC3\xA0 \xC3\xA1 \xC3\xA2 \xC3\xA4 \xC3\xA6 \xC3\xA3 \xC3\xA5" },
    { "e", "\xC3\xA8 \xC3\xA9 \xC3\xAA \xC3\xAB" },
    { "i", "\xC3\xAC \xC3\xAD \xC3\xAE \xC3\xAF" },
    { "o", "\xC3\xB2 \xC3\xB3 \xC3\xB4 \xC3\xB6 \xC3\xB5 \xC3\xB8 \xC5\x93" },
    { "u", "\xC3\xB9 \xC3\xBA \xC3\xBB \xC3\xBC" },
    { "c", "\xC3\xA7" }, { "n", "\xC3\xB1" }, { "s", "\xC3\x9F" }, { "y", "\xC3\xBF" },
    { ".", "\xE2\x80\xA6" }, { "-", "\xE2\x80\x93 \xE2\x80\x94 \xE2\x80\xA2" },
    { "?", "\xC2\xBF" }, { "!", "\xC2\xA1" },
    { "$", "\xC2\xA2 \xC2\xA3 \xE2\x82\xAC \xC2\xA5" },
    { "'", "\xE2\x80\x98 \xE2\x80\x99" }, { "\"", "\xE2\x80\x9C \xE2\x80\x9D \xC2\xAB \xC2\xBB" },
    { "0", "\xC2\xBA \xC2\xB0" }, { "/", "\xC3\xB7 \\" },
};

int kbd_alternates(const char *base, char out[][8], int max) {
    char low[8];
    int upper = base[0] >= 'A' && base[0] <= 'Z' && base[1] == 0;
    strncpy(low, base, sizeof low - 1);
    low[7] = 0;
    if (upper) low[0] = (char)(low[0] + 32);
    for (size_t i = 0; i < sizeof alt_tab / sizeof alt_tab[0]; i++) {
        if (strcmp(alt_tab[i][0], low)) continue;
        int n = 0;
        const char *p = alt_tab[i][1];
        while (*p && n < max) {
            const char *sp = strchr(p, ' ');
            size_t len = sp ? (size_t)(sp - p) : strlen(p);
            char one[8] = {0};
            if (len < sizeof one) memcpy(one, p, len);
            if (upper) kbd_utf8_upper(one, out[n], 8);
            else memcpy(out[n], one, 8);
            n++;
            p += len + (sp ? 1 : 0);
        }
        return n;
    }
    return 0;
}

static int is_letter(const KbdKeyDef *d) {
    return d->kind == KIND_CHAR && d->label[0] >= 'a' && d->label[0] <= 'z' && !d->label[1];
}

/* Rotating buffers: legends are drawn immediately after this returns. */
static char *scratch(void) {
    static char buf[8][16];
    static int at;
    at = (at + 1) & 7;
    return buf[at];
}

const char *kbd_slot_legend(const KbdSlot *s, int mods, int caps) {
    const KbdKeyDef *d = &kbd_keydefs[s->id];
    if (s->id == KK_GLYPH) return s->glyph;
    if (d->kind != KIND_CHAR) return d->label;
    int shift = (mods & KBD_MOD_SHIFT) != 0;
    if (mods & KBD_MOD_OPT) {
        if (d->dead && !shift) return kbd_dead_mark(d->dead);
        const char *o = shift ? d->optshift : d->opt;
        return o ? o : "";
    }
    if (is_letter(d)) {
        (void)caps;  /* classic caps print capitals regardless of shift */
        char *b = scratch();
        kbd_utf8_upper(d->label, b, 16);
        return b;
    }
    return shift && d->shift ? d->shift : d->label;
}

const char *kbd_slot_output(const KbdSlot *s, int mods, int caps, int *dead) {
    const KbdKeyDef *d = &kbd_keydefs[s->id];
    *dead = DEAD_NONE;
    if (mods & (KBD_MOD_CMD | KBD_MOD_CTRL)) return NULL;
    if (s->id == KK_GLYPH) return s->glyph;
    if (d->kind == KIND_SPACE) return (mods & KBD_MOD_OPT) ? d->opt : " ";
    if (d->kind != KIND_CHAR) return NULL;
    int shift = (mods & KBD_MOD_SHIFT) != 0;
    if (mods & KBD_MOD_OPT) {
        if (d->dead && !shift) { *dead = d->dead; return NULL; }
        return shift ? d->optshift : d->opt;
    }
    if (is_letter(d)) {
        if (shift ^ (caps != 0)) {
            char *b = scratch();
            kbd_utf8_upper(d->label, b, 16);
            return b;
        }
        return d->label;
    }
    return shift && d->shift ? d->shift : d->label;
}
