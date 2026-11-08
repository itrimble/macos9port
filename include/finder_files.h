#ifndef OS999_FINDER_FILES_H
#define OS999_FINDER_FILES_H
#include "ospi.h"
/* Returns an owned directory descriptor; walks without following links. */
int finder_directory_open(const char *root, const char *rel);
int finder_is_alias(const char *root, const char *rel, const char *name);
/* Relative paths only. Copies reject links and preserve sidecar resource forks. */
int finder_duplicate(const char *root, const char *rel, const char *name, char out[OSPI_NAME]);
int finder_rename(const char *root, const char *rel, const char *name, const char *next);
int finder_trash(const char *root, const char *rel, const char *name, char out[OSPI_NAME]);
int finder_put_away(const char *root, const char *name, char parent[OSPI_PATH], char out[OSPI_NAME]);
int finder_alias(const char *root, const char *rel, const char *name, int favorite, char out[OSPI_NAME]);
int finder_alias_target(const char *root, const char *rel, const char *name, char target[OSPI_PATH]);
int finder_label(const char *root, const char *rel, const char *name, int value);
int finder_move(const char *root, const char *from, const char *name, const char *to);
int finder_copy_to(const char *root, const char *from, const char *name, const char *to, char out[OSPI_NAME]);
/* Snapshot the item and optional resource sidecar without following links. */
int finder_identity(const char *root, const char *rel, const char *name, FinderUndo *identity);
#endif
