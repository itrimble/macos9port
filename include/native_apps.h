#ifndef OS999_NATIVE_APPS_H
#define OS999_NATIVE_APPS_H
#include "ospi.h"
enum { NATIVE_NONE, NATIVE_TERMINAL, NATIVE_TAILSCALE, NATIVE_CONSOLE,
       NATIVE_BROWSER, NATIVE_MUSIC, NATIVE_MOVIE, NATIVE_NOTES, NATIVE_CONTROLS };
int native_catalog_seed(const char *root);
int native_catalog_id(const char *root, const char *rel);
const char *native_catalog_path(int app);
int native_catalog_placeholder(const char *root, const char *rel);
void native_app_note(Session *session, int app);
int native_app_open(Session *session, int app);
#endif
