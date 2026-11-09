#ifndef MOVIE_PICKER_H
#define MOVIE_PICKER_H
#include "draw.h"
#include "ospi.h"

/* The empty Movie Player window: lists movies found on the volume and has a
 * Choose Movie... button that copies a file from the Mac into <volume>/Movies. */
void movie_picker_scan(Session *s);                 /* refresh the list */
int movie_picker_count(void);
const char *movie_picker_rel(int index);            /* volume-relative path */
void movie_picker_paint(Fb *fb, const Layout *lay);
int movie_picker_click(Session *s, Window *w, int x, int y);   /* 1 when handled */
/* Copy a host file into <root>/Movies (never overwrites); rel is filled on success. */
int movie_picker_import(const char *root, const char *host_path, char *rel, size_t n);
#endif
