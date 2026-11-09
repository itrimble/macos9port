#include "font_book.h"
#include "draw.h"
#include "typeface_discovery.h"
#include "text_system.h"

#include <stdlib.h>
#include <sys/stat.h>

static int active_filter(const char *path, void *context) {
    return font_book_path_active(context, path);
}

FontBookCatalog *font_book_session_catalog(Session *session) {
    return session ? (FontBookCatalog *)session->font_book_catalog : NULL;
}

int font_book_session_init(Session *session, const char *bundled_fonts,
                           const char *extra_fonts) {
    if (!session) return -1;
    char packaged[OSPI_PATH] = {0};
    struct stat st;
    if (!bundled_fonts || stat(bundled_fonts, &st) != 0 || !S_ISDIR(st.st_mode)) {
        typeface_executable_fonts(packaged, sizeof packaged);
        if (packaged[0]) bundled_fonts = packaged;
    }
    FontBookCatalog *catalog = calloc(1, sizeof *catalog);
    if (!catalog || font_book_catalog_init(catalog, session->root, bundled_fonts, extra_fonts) < 0) {
        free(catalog);
        return -1;
    }
    session->font_book_catalog = catalog;
    typeface_set_path_filter(active_filter, catalog);
    (void)text_system_init(catalog);
    return 0;
}

void font_book_session_shutdown(Session *session) {
    if (!session || !session->font_book_catalog) return;
    typeface_set_path_filter(NULL, NULL);
    text_system_shutdown();
    free(session->font_book_catalog);
    session->font_book_catalog = NULL;
}
