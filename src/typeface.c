#include "draw.h"
#include "ospi.h"
#include "typeface_discovery.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

enum { GLYPHS = 128 + 384 };   /* ASCII, then a tagged cache for everything else */

typedef struct Glyph {
    int advance, left, top, w, h;
    uint8_t *alpha;
    int ready;
    unsigned cp;   /* which character a non-ASCII slot holds */
} Glyph;

typedef struct FaceSlot {
#ifdef OSPI_FREETYPE
    FT_Face face;
#endif
    Glyph glyph[GLYPHS];
    int ascent, descent, line_h;
    int shared;
    char path[PATH_MAX];
} FaceSlot;

#ifdef OSPI_FREETYPE
static FT_Library g_lib;
#endif
static FaceSlot g_ui;
static FaceSlot g_body;
static FaceSlot g_startup;
static FaceSlot g_startup_small;
static char g_label[64] = "font8x8";
static int g_ready;
static TypefacePathFilter g_filter;
static void *g_filter_context;

static void blit_rows(Fb *fb, int x, int y, uint32_t color, const uint8_t *rows) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (!(rows[row] & (uint8_t)(1u << col))) {
                continue;
            }
            int px = x + col;
            int py = y + row;
            if (px < 0 || py < 0 || px >= fb->w || py >= fb->h) {
                continue;
            }
            if (fb->clip && (px < fb->cx || py < fb->cy || px >= fb->cx + fb->cw || py >= fb->cy + fb->ch)) {
                continue;
            }
            fb->px[py * fb->w + px] = color;
        }
    }
}

#ifdef OSPI_FREETYPE
static int ensure_lib(void) {
    if (g_lib) {
        return 1;
    }
    return FT_Init_FreeType(&g_lib) == 0;
}

static int slot_of(unsigned cp) {
    return cp < 128 ? (int)cp : 128 + (int)(((cp * 2654435761u) >> 20) % (GLYPHS - 128));
}

static int utf8_one(const char *s, unsigned *cp, int *n) {
    const unsigned char *u = (const unsigned char *)s;
    if (!u || u[0] == 0) {
        return 0;
    }
    if (u[0] < 0x80) {
        *cp = u[0];
        *n = 1;
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *cp = ((unsigned)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        *n = 2;
        return 1;
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((unsigned)(u[0] & 0x0F) << 12) | ((unsigned)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        *n = 3;
        return 1;
    }
    *cp = '?';
    *n = 1;
    return 1;
}

static Glyph *render_glyph(FaceSlot *slot, unsigned cp) {
    int idx = slot_of(cp);
    Glyph *g = &slot->glyph[idx];
    if (g->ready && g->cp == cp) return g;
    if (g->ready) { free(g->alpha); memset(g, 0, sizeof *g); }   /* the slot held another character */
    g->ready = 1;
    g->cp = cp;
    g->advance = 8;
    if (cp == 0x03C0 && FT_Get_Char_Index(slot->face, cp) == 0) {
        return g;
    }
    if (FT_Get_Char_Index(slot->face, cp) == 0 && cp != '?') {
        g->ready = 0;
        return render_glyph(slot, '?');
    }
    if (FT_Load_Char(slot->face, cp, FT_LOAD_RENDER) != 0) {
        return g;
    }
    FT_GlyphSlot gs = slot->face->glyph;
    g->advance = (int)(gs->advance.x >> 6);
    if (g->advance < 1) {
        g->advance = 4;
    }
    g->left = gs->bitmap_left;
    g->top = gs->bitmap_top;
    g->w = (int)gs->bitmap.width;
    g->h = (int)gs->bitmap.rows;
    if (g->w > 0 && g->h > 0 && gs->bitmap.buffer) {
        size_t bytes = (size_t)g->w * (size_t)g->h;
        g->alpha = malloc(bytes);
        if (g->alpha) {
            if (gs->bitmap.pitch == g->w) {
                memcpy(g->alpha, gs->bitmap.buffer, bytes);
            } else {
                for (int row = 0; row < g->h; row++) {
                    memcpy(g->alpha + (size_t)row * (size_t)g->w,
                           gs->bitmap.buffer + (size_t)row * (size_t)gs->bitmap.pitch, (size_t)g->w);
                }
            }
        }
    }
    return g;
}

static void blend(Fb *fb, int x, int y, uint32_t color, uint8_t cover) {
    if (!fb || !fb->px || cover == 0 || x < 0 || y < 0 || x >= fb->w || y >= fb->h) {
        return;
    }
    if (fb->clip && (x < fb->cx || y < fb->cy || x >= fb->cx + fb->cw || y >= fb->cy + fb->ch)) {
        return;
    }
    uint32_t dst = fb->px[y * fb->w + x];
    if (cover == 255) {
        fb->px[y * fb->w + x] = color;
        return;
    }
    int inv = 255 - cover;
    uint32_t r = (((color >> 16) & 255) * cover + ((dst >> 16) & 255) * inv) / 255;
    uint32_t g = (((color >> 8) & 255) * cover + ((dst >> 8) & 255) * inv) / 255;
    uint32_t b = ((color & 255) * cover + (dst & 255) * inv) / 255;
    fb->px[y * fb->w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
}

static int load_face(FaceSlot *slot, const char *path, int px, int primary) {
    FT_Face face = NULL;
    if (!path || !path[0] || FT_New_Face(g_lib, path, 0, &face) != 0) {
        return 0;
    }
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0) {
        FT_Done_Face(face);
        return 0;
    }
    slot->face = face;
    snprintf(slot->path, sizeof slot->path, "%s", path);
    slot->ascent = (int)(face->size->metrics.ascender >> 6);
    slot->descent = (int)(-(face->size->metrics.descender >> 6));
    if (slot->ascent < 8) {
        slot->ascent = 8;
    }
    if (slot->descent < 2) {
        slot->descent = 2;
    }
    slot->line_h = slot->ascent + slot->descent;
    if (slot->line_h < 10) {
        slot->line_h = 10;
    }
    if (primary && face->family_name && face->family_name[0]) {
        snprintf(g_label, sizeof g_label, "%s", face->family_name);
    }
    return 1;
}

static void release_face(FaceSlot *slot) {
    if (!slot) return;
    for (int i = 0; i < GLYPHS; i++) free(slot->glyph[i].alpha);
    if (slot->face) FT_Done_Face(slot->face);
    memset(slot, 0, sizeof *slot);
}
#endif

void typeface_set_path_filter(int (*filter)(const char *, void *), void *context) {
    g_filter = filter;
    g_filter_context = context;
}

int typeface_probe(const char *path) {
#ifdef OSPI_FREETYPE
    FT_Face face = NULL;
    int ok = ensure_lib() && path && FT_New_Face(g_lib, path, 0, &face) == 0 &&
             FT_Set_Pixel_Sizes(face, 0, 12) == 0;
    if (face) {
        FT_Done_Face(face);
    }
    return ok;
#else
    (void)path;
    return 0;
#endif
}

int typeface_init(const char *root) {
    if (g_ready) {
        return 0;
    }
#ifdef OSPI_FREETYPE
    if (!ensure_lib()) {
        return 0;
    }
    char bundled[PATH_MAX];
    char vol_sys[OSPI_PATH];
    char vol_fonts[OSPI_PATH];
    char home_fonts[PATH_MAX];
    typeface_executable_fonts(bundled, sizeof bundled);
    vol_sys[0] = 0;
    vol_fonts[0] = 0;
    home_fonts[0] = 0;
    if (root && root[0]) {
        snprintf(vol_sys, sizeof vol_sys, "%s/System Folder/Fonts", root);
        snprintf(vol_fonts, sizeof vol_fonts, "%s/Fonts", root);
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(home_fonts, sizeof home_fonts, "%s/Library/Fonts", home);
    }
    const char *dirs[] = {vol_sys,
                          vol_fonts,
                          home_fonts,
                          "/Library/Fonts",
                          "/System/Library/Fonts",
                          "/System/Library/Fonts/Supplemental",
                          bundled,
                          "fonts"};
    int nd = (int)(sizeof dirs / sizeof dirs[0]);
    static const char *prefer[] = {"Charcoal", "Chicago", "Geneva", "Monaco"};
    char ui_path[PATH_MAX];
    ui_path[0] = 0;
    for (int i = 0; i < 4 && !ui_path[0]; i++) {
        typeface_find_named(prefer[i], ui_path, sizeof ui_path, dirs, nd, g_filter, g_filter_context);
    }
    if (!ui_path[0]) {
        const char *local[] = {vol_sys, vol_fonts, bundled, "fonts"};
        typeface_find_named(NULL, ui_path, sizeof ui_path, local, 4, g_filter, g_filter_context);
    }
    if (!load_face(&g_ui, ui_path, 12, 1)) {
        return 0;
    }
    char body_path[PATH_MAX];
    body_path[0] = 0;
    typeface_find_named("Geneva", body_path, sizeof body_path, dirs, nd, g_filter, g_filter_context);
    if (!(body_path[0] && strcmp(body_path, g_ui.path) != 0 && load_face(&g_body, body_path, 12, 0))) {
        g_body.shared = 1;
    } else if (g_body.face && g_body.face->family_name && g_body.face->family_name[0]) {
        char both[64];
        snprintf(both, sizeof both, "%s, %s", g_label, g_body.face->family_name);
        snprintf(g_label, sizeof g_label, "%s", both);
    }
    /* Startup branding uses only the bundled public-domain font, never a
     * system-provided proprietary face. Separate sizes keep desktop metrics. */
    const char *startup_dirs[] = {bundled, "fonts"};
    char startup_path[PATH_MAX] = {0};
    if (typeface_find_named("ChicagoFLF", startup_path, sizeof startup_path,
                            startup_dirs, 2, NULL, NULL)) {
        load_face(&g_startup, startup_path, 38, 0);
        load_face(&g_startup_small, startup_path, 18, 0);
    }
    g_ready = 1;
#else
    (void)root;
#endif
    return 0;
}

int typeface_reload(const char *root) {
#ifdef OSPI_FREETYPE
    if (!g_ready) { typeface_init(root); return g_ready; }
    FaceSlot old_ui = g_ui, old_body = g_body, old_startup = g_startup;
    FaceSlot old_startup_small = g_startup_small;
    char old_label[sizeof g_label];
    snprintf(old_label, sizeof old_label, "%s", g_label);
    memset(&g_ui, 0, sizeof g_ui); memset(&g_body, 0, sizeof g_body);
    memset(&g_startup, 0, sizeof g_startup); memset(&g_startup_small, 0, sizeof g_startup_small);
    snprintf(g_label, sizeof g_label, "font8x8");
    g_ready = 0;
    typeface_init(root);
    if (!g_ready) {
        release_face(&g_ui); release_face(&g_body);
        release_face(&g_startup); release_face(&g_startup_small);
        g_ui = old_ui; g_body = old_body; g_startup = old_startup;
        g_startup_small = old_startup_small; g_ready = 1;
        snprintf(g_label, sizeof g_label, "%s", old_label);
        return 0;
    }
    release_face(&old_ui); release_face(&old_body);
    release_face(&old_startup); release_face(&old_startup_small);
    return 1;
#else
    (void)root;
    return 0;
#endif
}

int typeface_ready(void) { return g_ready; }
int typeface_startup_ready(void) {
#ifdef OSPI_FREETYPE
    return g_ready && g_startup.face && g_startup_small.face;
#else
    return 0;
#endif
}
const char *typeface_label(void) { return g_label; }

static FaceSlot *slot_for(int body) {
#ifdef OSPI_FREETYPE
    if (body == 2 && g_startup.face) return &g_startup;
    if (body == 3 && g_startup_small.face) return &g_startup_small;
#endif
    if (body && !g_body.shared && g_body.path[0]) {
        return &g_body;
    }
    return &g_ui;
}
int typeface_line_height(int body) {
    FaceSlot *slot = slot_for(body);
#ifdef OSPI_FREETYPE
    if (g_ready && slot->line_h > 0) {
        return slot->line_h;
    }
#else
    (void)slot;
#endif
    return 10;
}
int typeface_next(int body, const char *s, int *byte_adv, int *px) {
    if (!s || !s[0] || !byte_adv || !px) {
        return 0;
    }
#ifdef OSPI_FREETYPE
    FaceSlot *slot = slot_for(body);
    if (g_ready && slot->face) {
        unsigned cp = 0;
        int n = 0;
        if (!utf8_one(s, &cp, &n)) {
            return 0;
        }
        Glyph *g = render_glyph(slot, cp);
        *byte_adv = n;
        *px = g && g->advance > 0 ? g->advance : 8;
        return 1;
    }
#else
    (void)body;
#endif
    const uint8_t *rows = NULL;
    if (!font_next(s, byte_adv, &rows) || *byte_adv <= 0) {
        return 0;
    }
    *px = 8;
    return 1;
}

void typeface_draw(void *fb_ptr, int body, int x, int y, uint32_t color, const char *s, int nbytes) {
    Fb *fb = fb_ptr;
    if (!fb || !s) {
        return;
    }
    int used = 0;
    int cx = x;
#ifdef OSPI_FREETYPE
    FaceSlot *slot = slot_for(body);
    int outlined = g_ready && slot->face;
#endif
    while (*s && (nbytes < 0 || used < nbytes)) {
        int b = 0;
        int w = 0;
        if (!typeface_next(body, s, &b, &w) || b <= 0) {
            break;
        }
        if (nbytes >= 0 && used + b > nbytes) {
            break;
        }
        int drew = 0;
#ifdef OSPI_FREETYPE
        if (outlined) {
            unsigned cp = 0;
            int n = 0;
            utf8_one(s, &cp, &n);
            Glyph *g = render_glyph(slot, cp);
            if (g && g->alpha && g->w > 0) {
                int top = y + slot->ascent - g->top;
                for (int row = 0; row < g->h; row++) {
                    for (int col = 0; col < g->w; col++) {
                        blend(fb, cx + g->left + col, top + row, color, g->alpha[row * g->w + col]);
                    }
                }
                drew = 1;
            }
        }
#endif
        if (!drew) {
            const uint8_t *rows = NULL;
            int adv = 0;
            if (font_next(s, &adv, &rows) && rows) {
                blit_rows(fb, cx, y, color, rows);
            }
        }
        s += b;
        used += b;
        cx += w;
    }
}
