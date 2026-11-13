#ifndef OS999_INSTALL_INT_H
#define OS999_INSTALL_INT_H
#include <stddef.h>
#include <stdatomic.h>
int install_expand_archive(const char *root, const char *archive, const char *dest,
                            char *err, size_t n);
int install_tempfile(char *path, size_t n, const char *tag);
/* Secure Macintosh Garden transport, shared with classic_apps.c (hosts allow-listed, HTTPS only). */
void install_fetch_set_cancel(const atomic_bool *cancel);
int install_fetch_page(const char *url, const char *dest, long timeout_ms);
int install_fetch_archive(const char *page, const char *file_url, const char *dest);
int install_download_url_ok(const char *url);
#endif
