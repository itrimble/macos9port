#include "draw.h"
#include "ospi.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
#endif

enum { GLYPHS = 130 };

typedef struct Glyph {
    int advance, left, top, w, h;
    uint8_t *alpha;
    int ready;
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
static char g_label[64] = "font8x8";
static int g_ready;

static int font_file(const char *name) {
    static const char *exts[] = {".ttf", ".otf", ".ttc", ".pfa", ".pfb", ".dfont", NULL};
    size_t n = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && strcasecmp(name + n - e, exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int name_has(const char *file, const char *token) {
    size_t n = strlen(file);
    size_t m = strlen(token);
    if (m == 0 || m > n) {
        return 0;
    }
    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;
        while (j < m && tolower((unsigned char)file[i + j]) == tolower((unsigned char)token[j])) {
            j++;
        }
        if (j == m) {
            return 1;
        }
    }
    return 0;
}

static int scan_dir(const char *dir, const char *token, char *out, size_t n) {
    if (!dir || !dir[0]) {
        return 0;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || !font_file(de->d_name)) {
            continue;
        }
        if (token && !name_has(de->d_name, token)) {
            continue;
        }
        if (strchr(de->d_name, '/')) {
            continue;
        }
        if (snprintf(out, n, "%s/%s", dir, de->d_name) >= (int)n) {
            continue;
        }
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

static int find_named(const char *token, char *out, size_t n, const char **dirs, int nd) {
    for (int i = 0; i < nd; i++) {
        if (scan_dir(dirs[i], token, out, n)) {
            return 1;
        }
    }
    return 0;
}

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

static void exe_fonts(char *out) {
    out[0] = 0;
    char buf[PATH_MAX];
#if defined(__APPLE__)
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) != 0) {
        return;
    }
#else
    ssize_t k = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (k < 0) {
        return;
    }
    buf[k] = 0;
#endif
    char real[PATH_MAX];
    if (!realpath(buf, real)) {
        return;
    }
    char *slash = strrchr(real, '/');
    if (!slash) {
        return;
    }
    *slash = 0;
    char rel[PATH_MAX];
    snprintf(rel, sizeof rel, "%s/../Resources/Fonts", real);
    if (!realpath(rel, out)) {
        out[0] = 0;
    }
}

static int slot_of(unsigned cp) {
    if (cp < 128) {
        return (int)cp;
    }
    if (cp == 0x03C0) {
        return 128;
    }
    return 129;
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
    if (g->ready) {
        return g;
    }
    g->ready = 1;
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
#endif

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
    exe_fonts(bundled);
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
        find_named(prefer[i], ui_path, sizeof ui_path, dirs, nd);
    }
    if (!ui_path[0]) {
        const char *local[] = {vol_sys, vol_fonts, bundled, "fonts"};
        find_named(NULL, ui_path, sizeof ui_path, local, 4);
    }
    if (!load_face(&g_ui, ui_path, 12, 1)) {
        return 0;
    }
    char body_path[PATH_MAX];
    body_path[0] = 0;
    find_named("Geneva", body_path, sizeof body_path, dirs, nd);
    if (!(body_path[0] && strcmp(body_path, g_ui.path) != 0 && load_face(&g_body, body_path, 12, 0))) {
        g_body.shared = 1;
    } else if (g_body.face && g_body.face->family_name && g_body.face->family_name[0]) {
        char both[64];
        snprintf(both, sizeof both, "%s, %s", g_label, g_body.face->family_name);
        snprintf(g_label, sizeof g_label, "%s", both);
    }
    g_ready = 1;
#else
    (void)root;
#endif
    return 0;
}

int typeface_ready(void) { return g_ready; }
const char *typeface_label(void) { return g_label; }

static FaceSlot *slot_for(int body) {
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

int typeface_width(int body, const char *s, int nbytes) {
    if (!s) {
        return 0;
    }
    int used = 0;
    int px = 0;
    while (*s && (nbytes < 0 || used < nbytes)) {
        int b = 0;
        int w = 0;
        if (!typeface_next(body, s, &b, &w) || b <= 0) {
            break;
        }
        if (nbytes >= 0 && used + b > nbytes) {
            break;
        }
        used += b;
        s += b;
        px += w;
    }
    return px;
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
