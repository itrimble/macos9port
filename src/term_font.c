#include "term.h"
#include "term_font.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct {
    uint32_t cp;
    int valid, bold;
    unsigned char alpha[TERM_CELL_H][TERM_CELL_W];
} Glyph;
static FT_Library library;
static FT_Face faces[2];
static Glyph cache[512];
static int initialized;

static int load_face(const char *dir, int bold) {
    char path[PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/MesloLGSNerdFontMono-%s.ttf", dir,
                     bold ? "Bold" : "Regular");
    if (n < 0 || n >= (int)sizeof path || FT_New_Face(library, path, 0, &faces[bold])) return 0;
    if (FT_Set_Pixel_Sizes(faces[bold], 0, 15)) {
        FT_Done_Face(faces[bold]); faces[bold] = NULL; return 0;
    }
    return 1;
}

static void init_font(void) {
    if (initialized) return;
    initialized = 1;
    if (FT_Init_FreeType(&library)) return;
    char exe[PATH_MAX], dir[PATH_MAX];
    int have_exe = 0;
#ifdef __APPLE__
    uint32_t size = sizeof exe;
    have_exe = _NSGetExecutablePath(exe, &size) == 0;
#else
    ssize_t size = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (size >= 0) { exe[size] = 0; have_exe = 1; }
#endif
    if (have_exe) {
        char *end = strrchr(exe, '/');
        if (end) {
            *end = 0;
            const char *paths[] = {"/../Resources/Fonts/Terminal", "/../fonts/terminal"};
            for (size_t i = 0; i < sizeof paths / sizeof paths[0] && !faces[0]; i++) {
                int n = snprintf(dir, sizeof dir, "%s%s", exe, paths[i]);
                if (n > 0 && n < (int)sizeof dir && load_face(dir, 0)) load_face(dir, 1);
            }
        }
    }
    if (!faces[0] && load_face("fonts/terminal", 0)) load_face("fonts/terminal", 1);
}

int term_font_has_glyph(uint32_t cp) {
    init_font();
    return faces[0] && FT_Get_Char_Index(faces[0], cp) != 0;
}

static Glyph *get_glyph(uint32_t cp, int bold) {
    init_font();
    if (!faces[0]) return NULL;
    bold = !!bold;
    Glyph *g = &cache[(cp * 33u + (unsigned)bold) % 512];
    if (g->valid && g->cp == cp && g->bold == bold) return g;
    memset(g, 0, sizeof *g); g->cp = cp; g->bold = bold;
    FT_Face face = faces[bold] ? faces[bold] : faces[0];
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) return NULL;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bmp = &slot->bitmap;
    int advance = (int)(slot->advance.x >> 6);
    int left = (TERM_CELL_W - advance) / 2 + slot->bitmap_left;
    int top = 15 - slot->bitmap_top;
    for (unsigned y = 0; y < bmp->rows; y++) {
        int dy = top + (int)y;
        if (dy < 0 || dy >= TERM_CELL_H) continue;
        const unsigned char *row = bmp->buffer + (bmp->pitch >= 0 ? y : bmp->rows - 1 - y) * abs(bmp->pitch);
        for (unsigned x = 0; x < bmp->width; x++) {
            int dx = left + (int)x;
            if (dx < 0 || dx >= TERM_CELL_W) continue;
            g->alpha[dy][dx] = bmp->pixel_mode == FT_PIXEL_MODE_MONO ?
                ((row[x / 8] & (0x80 >> (x % 8))) ? 255 : 0) : row[x];
        }
    }
    g->valid = 1;
    return g;
}
#else
int term_font_has_glyph(uint32_t cp) { (void)cp; return 0; }
#endif

void term_font_draw(Fb *fb, int x, int y, uint32_t cp, uint32_t color, int bold) {
    if (!fb || !fb->px) return;
#ifdef OSPI_FREETYPE
    Glyph *g = get_glyph(cp, bold);
    if (g) {
        for (int gy = 0; gy < TERM_CELL_H; gy++) for (int gx = 0; gx < TERM_CELL_W; gx++) {
            unsigned a = g->alpha[gy][gx];
            int px = x + gx, py = y + gy;
            if (!a || px < 0 || py < 0 || px >= fb->w || py >= fb->h) continue;
            if (fb->clip && (px < fb->cx || py < fb->cy || px >= fb->cx + fb->cw || py >= fb->cy + fb->ch)) continue;
            uint32_t bg = fb->px[(size_t)py * fb->w + px], result = 0xff000000u;
            for (int shift = 0; shift <= 16; shift += 8) {
                unsigned v = (((color >> shift) & 255) * a + ((bg >> shift) & 255) * (255 - a) + 127) / 255;
                result |= v << shift;
            }
            fb->px[(size_t)py * fb->w + px] = result;
        }
        return;
    }
#else
    (void)bold;
#endif
    /* Builds without FreeType remain usable, but do not claim Nerd Font support. */
    char text[2] = {(char)(cp < 128 ? cp : '?'), 0};
    const uint8_t *rows = NULL; int advance = 0;
    if (!font_next(text, &advance, &rows) || !rows) return;
    for (int gy = 0; gy < 8; gy++) for (int gx = 0; gx < 8; gx++)
        if ((rows[gy] >> gx) & 1) fb_fill(fb, x + gx, y + gy + 4, 1, 1, color);
}
