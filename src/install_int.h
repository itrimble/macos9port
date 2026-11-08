#ifndef OS999_INSTALL_INT_H
#define OS999_INSTALL_INT_H
#include <stddef.h>
int install_expand_archive(const char *root, const char *archive, const char *dest,
                            char *err, size_t n);
int install_tempfile(char *path, size_t n, const char *tag);
#endif
