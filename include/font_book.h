#ifndef OS999_FONT_BOOK_H
#define OS999_FONT_BOOK_H

#include <stddef.h>
#include <stdint.h>
#include "ospi.h"

enum {
    FONT_BOOK_MAX_FONTS = 256,
    FONT_BOOK_MAX_DISABLED = 256,
    FONT_BOOK_META = 96,
    FONT_BOOK_ERROR = 192
};

typedef enum {
    FONT_BOOK_FORMAT_UNKNOWN = 0,
    FONT_BOOK_FORMAT_TRUETYPE,
    FONT_BOOK_FORMAT_OPENTYPE,
    FONT_BOOK_FORMAT_COLLECTION,
    FONT_BOOK_FORMAT_TYPE1,
    FONT_BOOK_FORMAT_RESOURCE
} FontBookFormat;

typedef enum {
    FONT_BOOK_SOURCE_SYSTEM = 1,
    FONT_BOOK_SOURCE_BUNDLED = 2
} FontBookSource;

typedef struct {
    char path[OSPI_PATH];
    char key[OSPI_PATH];
    char family[FONT_BOOK_META];
    char style[FONT_BOOK_META];
    char error[FONT_BOOK_ERROR];
    FontBookFormat format;
    FontBookSource source;
    int active;
    int valid;
    int previewable;
    int face_count;
    int face_index;
    int legacy_resources;
} FontBookEntry;

typedef struct {
    char root[OSPI_PATH];
    char bundled[OSPI_PATH];
    char extra[OSPI_PATH];
    FontBookEntry fonts[FONT_BOOK_MAX_FONTS];
    char disabled[FONT_BOOK_MAX_DISABLED][OSPI_PATH];
    int count;
    int disabled_count;
    unsigned generation;
} FontBookCatalog;

int font_book_catalog_init(FontBookCatalog *catalog, const char *volume_root,
                           const char *bundled_fonts, const char *extra_fonts);
int font_book_catalog_scan(FontBookCatalog *catalog);
int font_book_validate(const char *path, FontBookEntry *entry,
                       char *error, size_t error_size);
int font_book_set_active(FontBookCatalog *catalog, int index, int active,
                         char *error, size_t error_size);
int font_book_path_active(const FontBookCatalog *catalog, const char *path);
int font_book_install(FontBookCatalog *catalog, const char *source,
                      char *installed_path, size_t installed_size,
                      char *error, size_t error_size);
const char *font_book_format_name(FontBookFormat format);

int font_book_session_init(Session *session, const char *bundled_fonts,
                           const char *extra_fonts);
void font_book_session_shutdown(Session *session);
FontBookCatalog *font_book_session_catalog(Session *session);

#endif
