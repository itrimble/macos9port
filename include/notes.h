#ifndef OS999_NOTES_H
#define OS999_NOTES_H
#include "ospi.h"
struct Fb;
enum { CMD_NEW_NOTE = 702 };
void notes_new(Session *s);
/* 0 opened, 1 not a note, -1 handled read/open failure. */
int notes_open_document(Session *s, const char *title, const char *rel);
int notes_active(const Session *s);
int notes_input(Session *s, const char *text);
int notes_key(Session *s, int key, int mods);
int notes_down(Session *s, int x, int y);
void notes_paint(struct Fb *fb, const Session *s, const Window *w, const Layout *lay);
void text_caret(const char *text, int width, size_t offset, int *x, int *y);
size_t text_offset(const char *text, int width, int x, int y);
#endif
