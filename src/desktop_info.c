#include "desktop_info.h"
#include "classic_apps.h"
#include "session_int.h"
#include "desktop_theme.h"
#include "font_book_window.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "desktop_font_table.h"
#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
static FT_Library library;
static FT_Face faces[DESKTOP_FONT_COUNT];
#endif

int desktop_info_font_count(void) { return DESKTOP_FONT_COUNT; }
const char *desktop_info_font_name(int index) {
    return index >= 0 && index < DESKTOP_FONT_COUNT ? k_desktop_fonts[index].name : NULL;
}
int desktop_info_font_marker(int index, char *out, size_t size) {
    if (index < 0 || index >= DESKTOP_FONT_COUNT || !out) return -1;
    int n = snprintf(out, size, "OS999-FONT:%s:1\n", k_desktop_fonts[index].id);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int desktop_info_font_ready(int index) {
    if (index < 0 || index >= DESKTOP_FONT_COUNT) return 0;
#ifdef OSPI_FREETYPE
    if (faces[index]) return 1;
    if (!library && FT_Init_FreeType(&library)) return 0;
    char exe[OSPI_PATH], path[OSPI_PATH];
#ifdef __APPLE__
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size)) exe[0] = 0;
#else
    ssize_t size = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (size < 0) exe[0] = 0; else exe[size] = 0;
#endif
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0; else exe[0] = 0;
    const DesktopFontEntry *e = &k_desktop_fonts[index];
    for (int i = 0; i < 3; i++) {
        int n = i == 0 ? snprintf(path, sizeof path, "%s/../Resources/Fonts/%s", exe, e->bundle)
              : i == 1 ? snprintf(path, sizeof path, "%s/../%s", exe, e->dev)
                       : snprintf(path, sizeof path, "%s", e->dev);
        if (n > 0 && n < (int)sizeof path && !FT_New_Face(library, path, 0, &faces[index])) return 1;
    }
#endif
    return 0;
}

/* Only our small regular marker files activate bundled font previews. */
static int marker_matches(const char *root, int index) {
    int dir = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) return 0;
    const char *parents[] = {"System Folder", "Fonts"};
    char marker[64];
    if (desktop_info_font_marker(index, marker, sizeof marker)) { close(dir); return 0; }
    for (int i = 0; i < 2; i++) {
        int next = openat(dir, parents[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir); dir = next;
        if (dir < 0) return 0;
    }
    int fd = openat(dir, k_desktop_fonts[index].name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    close(dir);
    if (fd < 0) return 0;
    struct stat st; char data[64]; size_t size = strlen(marker);
    int match = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size == (off_t)size &&
        read(fd, data, sizeof data) == (ssize_t)size && !memcmp(data, marker, size);
    close(fd); return match;
}

int desktop_info_open(Session *s, const char *rel) {
    for (int i = 0; i < DESKTOP_FONT_COUNT; i++) {
        char expected[OSPI_PATH], token[32];
        snprintf(expected, sizeof expected, "System Folder/Fonts/%s", k_desktop_fonts[i].name);
        if (strcmp(expected, rel)) continue;
        if (!marker_matches(s->root, i)) return 0;
        snprintf(token, sizeof token, ":font:%d", i);
        if (session_open_text(s, k_desktop_fonts[i].name, token, "")) dialog_info(s, "Font Preview", "Close a window first.");
        return 1;
    }
    /* A real font the user dropped into the Fonts folder opens in Font Book. */
    static const char prefix[] = "System Folder/Fonts/";
    static const char *exts[] = {".ttf", ".otf", ".ttc", ".dfont", ".pfa", ".pfb", NULL};
    size_t n = strlen(rel);
    if (strncmp(rel, prefix, sizeof prefix - 1) || n <= sizeof prefix - 1) return 0;
    for (int i = 0; exts[i]; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && !strcasecmp(rel + n - e, exts[i])) return font_book_window_open(s) == 0;
    }
    return 0;
}

static Rect about_link(const Layout *lay) {
    return (Rect){lay->content.x + 8, lay->content.y + lay->content.h - 30, lay->content.w - 16, 22};
}

int desktop_info_click(Session *s, Window *w, int x, int y) {
    if (classic_apps_panel_click(s, w, x, y)) return 1;
    if (w->kind != WIN_TEXT || strcmp(w->rel, ":about")) return 0;
    Layout lay; window_layout(w, &lay);
    if (!rect_has(about_link(&lay), x, y)) return 0;
    session_command(s, CMD_BROWSER);
    if (s->browser_request && s->net_connect_via != 2)
        snprintf(s->browser_open_url, sizeof s->browser_open_url, "https://apps.remnantsecurity.com");
    return 1;
}

int desktop_info_paint(Fb *fb, const Window *w, const Layout *lay) {
    if (classic_apps_panel_paint(fb, w, lay)) return 1;
    if (!strcmp(w->rel, ":about")) {
        text_draw(fb, lay->content.x + 8, lay->content.y + 6 - w->scroll,
                  lay->content.w - 12, desktop_theme_color(THEME_FOREGROUND), window_text(w));
        Rect r = about_link(lay);
        fb_fill(fb, r.x, r.y, r.w, r.h, C_MENU);
        fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
        fb_text(fb, r.x + 6, r.y + 3, 0xff154f9cu, "apps.remnantsecurity.com");
        return 1;
    }
    int index = -1;
    for (int i = 0; i < DESKTOP_FONT_COUNT; i++) { char token[16]; snprintf(token, sizeof token, ":font:%d", i); if (!strcmp(token, w->rel)) index = i; }
    if (index < 0) return 0;
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, C_WHITE);
    int x = lay->content.x + 10, y = lay->content.y + 10;
    fb_text(fb, x, y, C_BLACK, k_desktop_fonts[index].name);
    if (!desktop_info_font_ready(index)) { fb_text(fb, x, y + 30, C_BLACK, "Font preview unavailable in this build."); return 1; }
#ifdef OSPI_FREETYPE
    FT_Face face = faces[index];
    const char *sample = k_desktop_fonts[index].sample;
    const int sizes[] = {12, 18, 24};
    y += 36;
    for (int row = 0; row < 3; row++) {
        FT_Set_Pixel_Sizes(face, 0, sizes[row]); int pen = x;
        for (const unsigned char *p = (const unsigned char *)sample; *p;) {
            unsigned long cp = *p++;
            int extra = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
            cp &= extra == 3 ? 0x07 : extra == 2 ? 0x0f : extra == 1 ? 0x1f : 0x7f;
            while (extra-- > 0 && (*p & 0xc0) == 0x80) cp = (cp << 6) | (*p++ & 0x3f);
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = face->glyph;
            if (g->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || g->bitmap.pitch < 0) continue;
            for (unsigned gy = 0; gy < g->bitmap.rows; gy++) for (unsigned gx = 0; gx < g->bitmap.width; gx++) {
                unsigned a = g->bitmap.buffer[gy * g->bitmap.pitch + gx];
                if (a) { unsigned c = 255 - a; fb_fill(fb, pen + g->bitmap_left + (int)gx, y - g->bitmap_top + (int)gy, 1, 1, 0xff000000u | c * 0x010101u); }
            }
            pen += (int)(g->advance.x >> 6);
        }
        y += sizes[row] + 26;
    }
#endif
    fb_text(fb, x, y + 10, C_BLACK, index ? "Bundled open font; not an Apple system font." : "Public-domain font by Robin Casady.");
    return 1;
}
