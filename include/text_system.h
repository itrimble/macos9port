#ifndef OS999_TEXT_SYSTEM_H
#define OS999_TEXT_SYSTEM_H

#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "font_book.h"

int text_system_init(const FontBookCatalog *catalog);
void text_system_shutdown(void);
int text_system_available(void);
unsigned text_system_capabilities(void);
int text_system_width(const char *utf8, size_t length);
int text_system_draw(Fb *fb, int x, int y, uint32_t color,
                     const char *utf8, size_t length);
int text_system_breaks(const char *utf8, size_t length, uint8_t *breaks);

#endif
