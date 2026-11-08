#include "desktop_info.h"
#include "session_int.h"
#include "desktop_theme.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
static FT_Library library;
static FT_Face faces[3];
#endif
static const char *names[] = {"ChicagoFLF", "Meslo Nerd Font Regular", "Meslo Nerd Font Bold"};

#ifdef OSPI_FREETYPE
static const char *files[] = {"ChicagoFLF.ttf", "Terminal/MesloLGSNerdFontMono-Regular.ttf", "Terminal/MesloLGSNerdFontMono-Bold.ttf"};

#endif
static const char *markers[] = {"OS999-FONT:chicagoflf:1\n", "OS999-FONT:meslo-regular:1\n", "OS999-FONT:meslo-bold:1\n"};

int desktop_info_font_ready(int index) {
    if (index < 0 || index >= 3) return 0;
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
    const char *roots[] = {"/../Resources/Fonts/", "/../fonts/", "fonts/"};
    for (int i = 0; i < 3; i++) {
        const char *file = files[index];
        char local[128];
        if (i != 0 && index) { snprintf(local, sizeof local, "terminal/%s", strrchr(file, '/') + 1); file = local; }
        int n = snprintf(path, sizeof path, "%s%s%s", i == 2 ? "" : exe, roots[i], file);
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
    for (int i = 0; i < 2; i++) {
        int next = openat(dir, parents[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir); dir = next;
        if (dir < 0) return 0;
    }
    int fd = openat(dir, names[index], O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    close(dir);
    if (fd < 0) return 0;
    struct stat st; char data[64]; size_t size = strlen(markers[index]);
    int match = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size == (off_t)size &&
        read(fd, data, sizeof data) == (ssize_t)size && !memcmp(data, markers[index], size);
    close(fd); return match;
}

int desktop_info_open(Session *s, const char *rel) {
    for (int i = 0; i < 3; i++) {
        char expected[OSPI_PATH], token[32];
        snprintf(expected, sizeof expected, "System Folder/Fonts/%s", names[i]);
        if (strcmp(expected, rel)) continue;
        if (!marker_matches(s->root, i)) return 0;
        snprintf(token, sizeof token, ":font:%d", i);
        if (session_open_text(s, names[i], token, "")) dialog_info(s, "Font Preview", "Close a window first.");
        return 1;
    }
    return 0;
}

static Rect about_link(const Layout *lay) {
    return (Rect){lay->content.x + 8, lay->content.y + lay->content.h - 30, lay->content.w - 16, 22};
}

int desktop_info_click(Session *s, Window *w, int x, int y) {
    if (w->kind != WIN_TEXT || strcmp(w->rel, ":about")) return 0;
    Layout lay; window_layout(w, &lay);
    if (!rect_has(about_link(&lay), x, y)) return 0;
    session_command(s, CMD_BROWSER);
    if (s->browser_request && s->net_connect_via != 2)
        snprintf(s->browser_open_url, sizeof s->browser_open_url, "https://apps.remnantsecurity.com");
    return 1;
}

int desktop_info_paint(Fb *fb, const Window *w, const Layout *lay) {
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
    for (int i = 0; i < 3; i++) { char token[16]; snprintf(token, sizeof token, ":font:%d", i); if (!strcmp(token, w->rel)) index = i; }
    if (index < 0) return 0;
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, C_WHITE);
    int x = lay->content.x + 10, y = lay->content.y + 10;
    fb_text(fb, x, y, C_BLACK, names[index]);
    if (!desktop_info_font_ready(index)) { fb_text(fb, x, y + 30, C_BLACK, "Font preview unavailable in this build."); return 1; }
#ifdef OSPI_FREETYPE
    FT_Face face = faces[index];
    const char *sample = "The quick brown fox 0123456789";
    const int sizes[] = {12, 18, 24};
    y += 36;
    for (int row = 0; row < 3; row++) {
        FT_Set_Pixel_Sizes(face, 0, sizes[row]); int pen = x;
        for (const unsigned char *p = (const unsigned char *)sample; *p; p++) {
            if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) continue;
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
