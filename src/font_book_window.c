#include "font_book_window.h"
#include "font_book.h"
#include "desktop_theme.h"
#include "menu_navigation.h"
#include "session_int.h"
#include "text_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FontBookWindowState {
    int collection;
    int selected;
    int scroll;
    char status[FONT_BOOK_ERROR];
} FontBookWindowState;

static const char *k_collections[] = {
    "All Fonts", "System Folder", "OS999 Fonts", "Active Fonts", "Problems", "Host Fonts"
};
enum { COLLECTION_COUNT = 6, COLLECTION_HOST = 5 };

static FontBookWindowState *state(const Window *window) {
    return window && window->kind == WIN_FONT_BOOK ? window->font_book : NULL;
}

static int matches(const FontBookEntry *entry, int collection) {
    if (!entry) return 0;
    if (collection == 1) return entry->source == FONT_BOOK_SOURCE_SYSTEM;
    if (collection == 2) return entry->source == FONT_BOOK_SOURCE_BUNDLED;
    if (collection == 3) return entry->active && entry->valid && entry->source != FONT_BOOK_SOURCE_HOST;
    if (collection == 4) return !entry->valid;
    if (collection == COLLECTION_HOST) return entry->source == FONT_BOOK_SOURCE_HOST;
    return 1;
}

static int visible_count(const FontBookCatalog *catalog, int collection) {
    int count = 0;
    if (catalog) for (int i = 0; i < catalog->count; i++) count += matches(&catalog->fonts[i], collection);
    return count;
}

static int visible_at(const FontBookCatalog *catalog, int collection, int row) {
    if (!catalog || row < 0) return -1;
    for (int i = 0; i < catalog->count; i++)
        if (matches(&catalog->fonts[i], collection) && row-- == 0) return i;
    return -1;
}

static void pane_rects(const Layout *layout, Rect *collections, Rect *families, Rect *preview) {
    Rect content = layout->content;
    int left = content.w < 620 ? 126 : 150;
    int middle = content.w < 620 ? 190 : 230;
    *collections = (Rect){content.x, content.y, left, content.h};
    *families = (Rect){content.x + left, content.y, middle, content.h};
    *preview = (Rect){content.x + left + middle, content.y,
                      content.w - left - middle, content.h};
}

static Rect host_rect(Rect collections) {
    return (Rect){collections.x + 8, collections.y + collections.h - 44, collections.w - 16, 36};
}

static Rect toggle_rect(Rect preview) {
    return (Rect){preview.x + 12, preview.y + preview.h - 38, 112, 25};
}

static Rect validate_rect(Rect preview) {
    return (Rect){preview.x + 132, preview.y + preview.h - 38, 86, 25};
}

int font_book_window_open(Session *session) {
    if (!session) return -1;
    for (int i = 0; i < session->nwin; i++) if (session->win[i].kind == WIN_FONT_BOOK) {
        bring_front(session, i);
        session->desktop_focus = 0;
        return 0;
    }
    if (!font_book_session_catalog(session) || session->nwin >= OSPI_MAX_WIN) return -1;
    FontBookWindowState *book = calloc(1, sizeof *book);
    if (!book) return -1;
    Window *window = &session->win[session->nwin++];
    memset(window, 0, sizeof *window);
    window->kind = WIN_FONT_BOOK;
    window->x = session->sw > 760 ? (session->sw - 760) / 2 : 8;
    window->y = session->sh > 520 ? (session->sh - 520) / 2 : TITLE_H + 8;
    window->w = session->sw > 760 ? 760 : session->sw - 16;
    window->h = session->sh > 520 ? 520 : session->sh - TITLE_H - 16;
    window->font_book = book;
    snprintf(window->title, sizeof window->title, "Font Book 999");
    snprintf(window->rel, sizeof window->rel, ":font-book-999");
    session->desktop_focus = 0;
    session->desk_sel = -1;
    book->selected = visible_at(font_book_session_catalog(session), 0, 0);
    snprintf(book->status, sizeof book->status, "%d fonts indexed",
             font_book_session_catalog(session)->count);
    return 0;
}

void font_book_window_release(Window *window) {
    if (!window || window->kind != WIN_FONT_BOOK) return;
    free(window->font_book);
    window->font_book = NULL;
}

static void pane(Fb *fb, Rect rect, uint32_t fill) {
    fb_fill(fb, rect.x, rect.y, rect.w, rect.h, fill);
    fb_frame(fb, rect.x, rect.y, rect.w, rect.h, desktop_theme_color(THEME_FRAME));
}

static void button(Fb *fb, Rect rect, const char *label, int enabled) {
    fb_fill(fb, rect.x, rect.y, rect.w, rect.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, rect.x, rect.y, rect.w, rect.h,
             enabled ? desktop_theme_color(THEME_FRAME) : desktop_theme_color(THEME_DISABLED));
    int x = rect.x + (rect.w - fb_text_width(label)) / 2;
    fb_text(fb, x, rect.y + 6, enabled ? desktop_theme_color(THEME_FOREGROUND) :
            desktop_theme_color(THEME_DISABLED), label);
}

static const char *preview_sample(const FontBookEntry *entry) {
    if (strstr(entry->family, "Arabic")) return "سلام بالعالم";
    if (strstr(entry->family, "Hebrew")) return "שלום עולם";
    if (strstr(entry->family, "Devanagari")) return "नमस्ते दुनिया";
    if (strstr(entry->family, "Bengali")) return "নমস্কার বিশ্ব";
    if (strstr(entry->family, "Gujarati")) return "નમસ્તે વિશ્વ";
    if (strstr(entry->family, "Gurmukhi")) return "ਸਤ ਸ੍ਰੀ ਅਕਾਲ";
    if (strstr(entry->family, "Kannada")) return "ನಮಸ್ಕಾರ ಜಗತ್ತು";
    if (strstr(entry->family, "Malayalam")) return "നമസ്കാരം ലോകം";
    if (strstr(entry->family, "Tamil")) return "வணக்கம் உலகம்";
    if (strstr(entry->family, "Telugu")) return "నమస్కారం ప్రపంచం";
    if (strstr(entry->family, "Thai")) return "สวัสดีชาวโลก";
    if (strstr(entry->family, "CJK")) return "日本語 中文 한국어";
    if (strstr(entry->family, "Emoji")) return "☀ ★ ♥ ☕ ☺";
    return "The quick brown fox 0123456789";
}

static void draw_collections(Fb *fb, Rect rect, FontBookWindowState *book, int host_on) {
    fb_text(fb, rect.x + 9, rect.y + 8, desktop_theme_color(THEME_FOREGROUND), "COLLECTIONS");
    for (int i = 0; i < COLLECTION_COUNT; i++) {
        if (i == COLLECTION_HOST && !host_on) continue;
        Rect row = {rect.x + 1, rect.y + 28 + i * 25, rect.w - 2, 25};
        if (i == book->collection) fb_fill(fb, row.x, row.y, row.w, row.h,
                                           desktop_theme_color(THEME_ACCENT));
        fb_text(fb, row.x + 9, row.y + 7,
                i == book->collection ? C_WHITE : desktop_theme_color(THEME_FOREGROUND),
                k_collections[i]);
    }
    Rect box = host_rect(rect);
    uint32_t ink = desktop_theme_color(THEME_FOREGROUND);
    fb_frame(fb, box.x, box.y + 2, 12, 12, desktop_theme_color(THEME_FRAME));
    if (host_on) fb_text(fb, box.x + 2, box.y + 4, ink, "x");
    fb_text(fb, box.x + 18, box.y + 4, ink, "Show host");
    fb_text(fb, box.x + 18, box.y + 18, ink, "fonts");
}

static void draw_families(Fb *fb, Rect rect, FontBookCatalog *catalog,
                          FontBookWindowState *book) {
    fb_text(fb, rect.x + 9, rect.y + 8, desktop_theme_color(THEME_FOREGROUND), "FONT FAMILY");
    int rows = (rect.h - 32) / 24;
    for (int row = 0; row < rows; row++) {
        int index = visible_at(catalog, book->collection, book->scroll + row);
        if (index < 0) break;
        FontBookEntry *entry = &catalog->fonts[index];
        Rect rr = {rect.x + 1, rect.y + 28 + row * 24, rect.w - 2, 24};
        if (index == book->selected) fb_fill(fb, rr.x, rr.y, rr.w, rr.h,
                                             desktop_theme_color(THEME_ACCENT));
        uint32_t ink = index == book->selected ? C_WHITE :
                       entry->valid ? desktop_theme_color(THEME_FOREGROUND) : 0xff9b2424u;
        fb_text(fb, rr.x + 7, rr.y + 6, ink, entry->active ? "*" : "-");
        fb_clip(fb, rr.x + 22, rr.y, rr.w - 25, rr.h);
        fb_text(fb, rr.x + 22, rr.y + 6, ink, entry->family);
        fb_unclip(fb);
    }
}

static void draw_preview(Fb *fb, Rect rect, FontBookEntry *entry,
                         FontBookWindowState *book) {
    fb_text(fb, rect.x + 12, rect.y + 10, desktop_theme_color(THEME_FOREGROUND), "PREVIEW");
    if (!entry) {
        fb_text(fb, rect.x + 14, rect.y + 42, desktop_theme_color(THEME_DISABLED), "No font selected");
        return;
    }
    fb_text(fb, rect.x + 14, rect.y + 39, desktop_theme_color(THEME_FOREGROUND), entry->family);
    char detail[256];
    snprintf(detail, sizeof detail, "%s - %s - %s", entry->style,
             font_book_format_name(entry->format),
             entry->source == FONT_BOOK_SOURCE_HOST ? "Host font, read-only" :
             entry->active ? "Active" : "Inactive");
    fb_text(fb, rect.x + 14, rect.y + 60, desktop_theme_color(THEME_DISABLED), detail);
    Rect sample = {rect.x + 12, rect.y + 88, rect.w - 24, 82};
    fb_fill(fb, sample.x, sample.y, sample.w, sample.h, C_WHITE);
    fb_frame(fb, sample.x, sample.y, sample.w, sample.h, desktop_theme_color(THEME_FRAME));
    int previewed = entry->valid && entry->previewable &&
                    font_book_preview_draw(fb, entry->path, entry->face_index, sample, C_BLACK,
                                           preview_sample(entry));
    if (!previewed) fb_text(fb, sample.x + 10, sample.y + 30, C_BLACK,
                            entry->valid ? "Preview unavailable for this legacy face." : "Font validation failed.");
    snprintf(detail, sizeof detail, "Faces: %d   Resources: %d", entry->face_count, entry->legacy_resources);
    fb_text(fb, rect.x + 14, rect.y + 184, desktop_theme_color(THEME_FOREGROUND), detail);
    fb_clip(fb, rect.x + 14, rect.y + 210, rect.w - 28, 46);
    fb_text(fb, rect.x + 14, rect.y + 210,
            entry->valid ? desktop_theme_color(THEME_DISABLED) : 0xff9b2424u,
            entry->valid ? entry->path : entry->error);
    fb_unclip(fb);
    button(fb, toggle_rect(rect), entry->active ? "Deactivate" : "Activate",
           entry->valid && entry->source != FONT_BOOK_SOURCE_HOST);
    button(fb, validate_rect(rect), "Validate", 1);
    fb_clip(fb, rect.x + 12, rect.y + rect.h - 68, rect.w - 24, 20);
    fb_text(fb, rect.x + 12, rect.y + rect.h - 64, desktop_theme_color(THEME_DISABLED), book->status);
    fb_unclip(fb);
}

void font_book_window_paint(Fb *fb, const Session *session, const Window *window, const Layout *layout) {
    FontBookWindowState *book = state(window);
    FontBookCatalog *catalog = font_book_session_catalog((Session *)session);
    if (!fb || !book || !catalog || !layout) return;
    Rect collections, families, preview;
    pane_rects(layout, &collections, &families, &preview);
    pane(fb, collections, desktop_theme_color(THEME_MENU));
    pane(fb, families, desktop_theme_color(THEME_PANEL));
    pane(fb, preview, desktop_theme_color(THEME_MENU));
    draw_collections(fb, collections, book, catalog->host_enabled);
    draw_families(fb, families, catalog, book);
    FontBookEntry *entry = book->selected >= 0 && book->selected < catalog->count ?
                           &catalog->fonts[book->selected] : NULL;
    draw_preview(fb, preview, entry, book);
}

static void select_row(FontBookCatalog *catalog, FontBookWindowState *book, int row) {
    int index = visible_at(catalog, book->collection, row);
    if (index >= 0) book->selected = index;
}

static void toggle_selected(Session *session, FontBookCatalog *catalog, FontBookWindowState *book) {
    if (book->selected < 0 || book->selected >= catalog->count) return;
    FontBookEntry *entry = &catalog->fonts[book->selected];
    int prior = entry->active;
    char error[FONT_BOOK_ERROR] = {0};
    if (font_book_set_active(catalog, book->selected, !prior, error, sizeof error) != 0) {
        snprintf(book->status, sizeof book->status, "%s", error[0] ? error : "Activation change failed.");
        return;
    }
    if (!typeface_reload(session->root)) {
        font_book_set_active(catalog, book->selected, prior, NULL, 0);
        (void)text_system_init(catalog);
        snprintf(book->status, sizeof book->status, "No safe replacement face; activation was restored.");
        return;
    }
    (void)text_system_init(catalog);
    snprintf(book->status, sizeof book->status, "%s is now %s.", entry->family,
             entry->active ? "active" : "inactive");
}

int font_book_window_down(Session *session, Window *window, const Layout *layout, int x, int y) {
    FontBookWindowState *book = state(window);
    FontBookCatalog *catalog = font_book_session_catalog(session);
    if (!book || !catalog || !layout || !rect_has(layout->content, x, y)) return 0;
    Rect collections, families, preview;
    pane_rects(layout, &collections, &families, &preview);
    if (rect_has(host_rect(collections), x, y)) {
        char error[FONT_BOOK_ERROR] = {0};
        int turning_on = !catalog->host_enabled;
        if (font_book_host_set_enabled(catalog, turning_on, error, sizeof error) != 0)
            snprintf(book->status, sizeof book->status, "%s", error[0] ? error : "Host fonts could not be scanned.");
        else {
            if (!turning_on && book->collection == COLLECTION_HOST) book->collection = 0;
            book->scroll = 0;
            book->selected = visible_at(catalog, book->collection, 0);
            if (turning_on) snprintf(book->status, sizeof book->status,
                                     "%d host fonts listed in place; none copied.", visible_count(catalog, COLLECTION_HOST));
            else snprintf(book->status, sizeof book->status, "Host fonts hidden.");
        }
    } else if (rect_has(collections, x, y)) {
        int collection = (y - collections.y - 28) / 25;
        if (collection >= 0 && collection < COLLECTION_COUNT &&
            (collection != COLLECTION_HOST || catalog->host_enabled)) {
            book->collection = collection; book->scroll = 0;
            book->selected = visible_at(catalog, collection, 0);
        }
    } else if (rect_has(families, x, y) && y >= families.y + 28) {
        select_row(catalog, book, book->scroll + (y - families.y - 28) / 24);
    } else if (rect_has(toggle_rect(preview), x, y)) {
        toggle_selected(session, catalog, book);
    } else if (rect_has(validate_rect(preview), x, y) && book->selected >= 0) {
        FontBookEntry checked;
        char error[FONT_BOOK_ERROR] = {0};
        int ok = font_book_validate(catalog->fonts[book->selected].path, &checked, error, sizeof error) == 0;
        snprintf(book->status, sizeof book->status, "%s", ok ? "Validation passed without modifying the font." : error);
    }
    return 1;
}

int font_book_window_key(Session *session, Window *window, int key, int mods) {
    FontBookWindowState *book = state(window);
    FontBookCatalog *catalog = font_book_session_catalog(session);
    if (!book || !catalog) return 0;
    int count = visible_count(catalog, book->collection), row = 0;
    for (int i = 0; i < count; i++) if (visible_at(catalog, book->collection, i) == book->selected) row = i;
    if (key == MENU_KEY_UP && row > 0) row--;
    else if (key == MENU_KEY_DOWN && row + 1 < count) row++;
    else if (key == ' ' && !mods) { toggle_selected(session, catalog, book); return 1; }
    else return 0;
    select_row(catalog, book, row);
    int rows = (window->h - TITLE_H - INFO_H - 32) / 24;
    if (row < book->scroll) book->scroll = row;
    if (row >= book->scroll + rows) book->scroll = row - rows + 1;
    return 1;
}

int font_book_window_wheel(Window *window, int delta) {
    FontBookWindowState *book = state(window);
    if (!book) return 0;
    book->scroll -= delta * 2;
    if (book->scroll < 0) book->scroll = 0;
    return 1;
}
