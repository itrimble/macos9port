#ifndef OS999_FOLDER_VIEW_H
#define OS999_FOLDER_VIEW_H

#include "ospi.h"
#include "draw.h"

enum { FOLDER_LIST_HEADER = 24, FOLDER_LIST_ROW = 24 };
typedef void (*FolderItemPainter)(Fb *, int, int, const Item *, const char *);
int folder_view_command(Session *session, int command);
void folder_view_sort(Window *window);
void folder_view_draw_list(Fb *fb, const Window *window, const Layout *layout,
                           FolderItemPainter paint_icon);

#endif
