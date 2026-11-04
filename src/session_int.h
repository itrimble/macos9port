#ifndef OSPI_SESSION_INT_H
#define OSPI_SESSION_INT_H

#include "ospi.h"

void bring_front(Session *s, int idx);
void close_index(Session *s, int idx);
void reload_folders(Session *s);
int open_folder(Session *s, const char *rel);
void dialog_info(Session *s, const char *title, const char *body);
void dialog_accept(Session *s);
void open_item(Session *s, Window *w, int index);
void select_only(Window *w, int index);
void toggle_shade(Window *w);
void toggle_zoom(Session *s, Window *w);

#endif
