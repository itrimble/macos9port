#ifndef OSPI_SESSION_INT_H
#define OSPI_SESSION_INT_H

#include "ospi.h"

struct Fb;
void session_desktop_icons_paint(struct Fb *fb, const Session *s);
int session_open_text(Session *s, const char *title, const char *rel, const char *body);
void bring_front(Session *s, int idx);
void close_index(Session *s, int idx);
void reload_folders(Session *s);
int window_text_set(Window *w, const char *text);
int reload_folder_window(Session *s, Window *w);
void finder_refresh(Session *s);
int open_folder(Session *s, const char *rel);
int session_is_sit(const char *name);
void session_open_sit(Session *s, const char *rel);
void dialog_info(Session *s, const char *title, const char *body);
int session_open_quake(Session *s);
void dialog_accept(Session *s);
void open_item(Session *s, Window *w, int index);
void select_only(Window *w, int index);
void toggle_shade(Window *w);
void toggle_zoom(Session *s, Window *w);

#endif
