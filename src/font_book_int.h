#ifndef OS999_FONT_BOOK_INT_H
#define OS999_FONT_BOOK_INT_H

#include "font_book.h"

int font_book_registry_load(FontBookCatalog *catalog);
int font_book_registry_save(const FontBookCatalog *catalog,
                            char *error, size_t error_size);
int font_book_registry_disabled(const FontBookCatalog *catalog, const char *key);
int font_book_registry_set(FontBookCatalog *catalog, const char *key, int disabled);
int font_book_validate_face(const char *path, int face_index, FontBookEntry *entry);
int font_book_copy_validated(const char *source, const char *destination,
                             char *error, size_t error_size);

#endif
