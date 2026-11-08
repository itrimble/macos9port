/* Original pixel-art Granny Smith fruit for the OS999 boot screen.
 *
 * The artwork is authored here as indexed ASCII rows so it stays reviewable
 * in a diff. Every palette entry is a colour that already exists in the
 * project 256-colour icon cube (src/mac_icon_palette.c): cube slot
 * 36*r + 6*g + b over {255,204,153,102,51,0}.
 *
 * Nothing here is derived from Apple artwork. The supplied OS999 mockups are
 * layout references only.
 */
#include "os999_startup_sprite.h"

#include <math.h>
#include <stddef.h>

enum {
    APPLE_NONE = -1,
    APPLE_OUTLINE = 0,
    APPLE_DARK,
    APPLE_MID,
    APPLE_LIGHT,
    APPLE_HILITE,
    APPLE_STEM,
    APPLE_STEM_DARK,
    APPLE_LEAF,
    APPLE_LEAF_DARK
};

static const uint32_t fruit_palette[] = {
    0xFF000000u, /* outline     */
    0xFF336600u, /* dark green  */
    0xFF669900u, /* mid green   */
    0xFF99CC33u, /* light green */
    0xFFCCFF99u, /* highlight   */
    0xFF663300u, /* stem        */
    0xFF333300u, /* stem dark   */
    0xFF33CC00u, /* leaf        */
    0xFF339900u  /* leaf dark   */
};

#define FRUIT_WIDTH 24
#define TOP_HEIGHT 12
#define BODY_HEIGHT 22
#define FRUIT_HEIGHT (TOP_HEIGHT + BODY_HEIGHT - 2)

/* Stem and leaf. The stem leans right as it rises and the leaf sits on its
   left, the way the supplied OS999 reference fruit does. */
static const char *const fruit_top[TOP_HEIGHT] = {
    "........................",
    ".............tT.........",
    ".........ff..tT.........",
    "........Fff.tT..........",
    "........FffftT..........",
    "........FffftT..........",
    "..........fFtT..........",
    "...........tT...........",
    "...........tT...........",
    "...........tT...........",
    "...........tT...........",
    "...........tT..........."
};

/* Body: bright green with a darker rim down the right side and around the
   bottom. The pale highlight is added by the renderer as the fruit turns. */
static const char *const fruit_body[BODY_HEIGHT] = {
    "........OOOOOOOO........",
    ".......OllllllllO.......",
    "......OllllllllllO......",
    ".....OllllllllllllO.....",
    "....OllllllllllllllO....",
    "...OllllllllllllllllO...",
    "..OllllllllllllllllllO..",
    ".OllllllllllllllllllllO.",
    "OllllllllllllllllllllllO",
    "OlllllllllllllllllllllmO",
    "OlllllllllllllllllllllmO",
    "OlllllllllllllllllllllmO",
    "OlllllllllllllllllllllmO",
    "OllllllllllllllllllllmdO",
    "OllllllllllllllllllllmdO",
    ".OllllllllllllllllllmdO.",
    ".OllllllllllllllllllmdO.",
    "..OllllllllllllllllmdO..",
    "...OllllllllllllllmdO...",
    "....OllllllllllllmdO....",
    "......OdmmmOOmmmdO......",
    ".......OOOO..OOOO......."
};

static int char_index(char c) {
    switch (c) {
    case 'O': return APPLE_OUTLINE;
    case 'd': return APPLE_DARK;
    case 'm': return APPLE_MID;
    case 'l': return APPLE_LIGHT;
    case 'h': return APPLE_HILITE;
    case 't': return APPLE_STEM;
    case 'T': return APPLE_STEM_DARK;
    case 'f': return APPLE_LEAF;
    case 'F': return APPLE_LEAF_DARK;
    default: return APPLE_NONE;
    }
}

/* The fruit turns about its vertical axis. The hand-drawn silhouette stays
   put; the shadow rim and the gloss spot travel across the skin, which is
   how a classic sprite sheet fakes a spinning object. */
static int body_index(int x, int y, double angle) {
    const double cx = 11.5, cy = 10.5, rx = 12.0, ry = 11.0;
    double u = (x - cx) / rx, v = (y - cy) / ry;
    double d2 = u * u + v * v;
    if (d2 > 1.0) return APPLE_DARK;
    double nz = sqrt(1.0 - d2);
    double c = cos(angle), s = sin(angle);
    double turned_x = u * c + nz * s;
    double turned_z = -u * s + nz * c;
    const double lx = -0.42, ly = 0.50, lz = 0.76;
    double diff = turned_x * lx + (-v) * ly + turned_z * lz;
    if (diff < 0.0) diff = 0.0;
    if (diff > 0.88) return APPLE_HILITE;
    /* The rim on the far side from the light falls into shadow. */
    if (d2 > 0.55 && diff < 0.30) return APPLE_DARK;
    return APPLE_LIGHT;
}

static void draw_body(Fb *fb, const char *const rows[], int count, int width,
                      int x0, int y0, int scale, double angle) {
    for (int r = 0; r < count; r++) {
        for (int c = 0; c < width; c++) {
            char ch = rows[r][c];
            int index;
            if (ch == 'O') index = APPLE_OUTLINE;
            else if (ch == 'l' || ch == 'm' || ch == 'd') index = body_index(c, r, angle);
            else index = APPLE_NONE;
            if (index == APPLE_NONE) continue;
            fb_fill(fb, x0 + c * scale, y0 + r * scale, scale, scale,
                    fruit_palette[index]);
        }
    }
}

static void draw_top(Fb *fb, const char *const rows[], int count, int width,
                      int x0, int y0, int scale, double angle) {
    /* The lit side of the stem and leaf swaps as the fruit turns. */
    int lit = cos(angle) >= 0.0;
    for (int r = 0; r < count; r++) {
        for (int c = 0; c < width; c++) {
            char ch = rows[r][c];
            if (ch == 't') ch = lit ? 't' : 'T';
            else if (ch == 'T') ch = lit ? 'T' : 't';
            else if (ch == 'f') ch = lit ? 'f' : 'F';
            else if (ch == 'F') ch = lit ? 'F' : 'f';
            int index = char_index(ch);
            if (index == APPLE_NONE) continue;
            fb_fill(fb, x0 + c * scale, y0 + r * scale, scale, scale,
                    fruit_palette[index]);
        }
    }
}

void os999_startup_draw_fruit(Fb *fb, int cx, int cy, int scale, double angle) {
    if (!fb || scale < 1) return;
    int x0 = cx - (FRUIT_WIDTH * scale) / 2;
    int y0 = cy - (FRUIT_HEIGHT * scale) / 2;
    /* The body first, so the stem sits on the fruit instead of behind it. */
    draw_body(fb, fruit_body, BODY_HEIGHT, FRUIT_WIDTH, x0,
              y0 + (TOP_HEIGHT - 2) * scale, scale, angle);
    draw_top(fb, fruit_top, TOP_HEIGHT, FRUIT_WIDTH, x0, y0, scale, angle);
}

int os999_startup_fruit_width(int scale) {
    return FRUIT_WIDTH * (scale < 1 ? 1 : scale);
}

int os999_startup_fruit_height(int scale) {
    return FRUIT_HEIGHT * (scale < 1 ? 1 : scale);
}
