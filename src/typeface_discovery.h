#ifndef OS999_TYPEFACE_DISCOVERY_H
#define OS999_TYPEFACE_DISCOVERY_H

#include <stddef.h>

typedef int (*TypefacePathFilter)(const char *path, void *context);

int typeface_find_named(const char *token, char *out, size_t size,
                        const char **directories, int directory_count,
                        TypefacePathFilter filter, void *context);
void typeface_executable_fonts(char *out, size_t size);

#endif
