#ifndef OS999_FINDER_SEARCH_H
#define OS999_FINDER_SEARCH_H
#include "ospi.h"
int finder_search(const char *root, const char *query, Item *items, int capacity, int *limited);
int finder_search_command(Session *s, int command);
#endif
