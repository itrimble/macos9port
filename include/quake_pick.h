#ifndef OS999_QUAKE_PICK_H
#define OS999_QUAKE_PICK_H
/* "Choose Folder..." for Quake III Arena data: the in-OS999 file dialog in a small window. The user browses
 * the volume, Downloads, Desktop or Documents and presses Use This Folder; OS999 verifies the full game's nine
 * paks (or the 1.11 demo pak0.pk3) by size and SHA-256, installs them all-or-nothing and launches Quake. */
#include "draw.h"
#include "file_dialog.h"
#include "ospi.h"
#include "quake_data.h"

/* Verify and install what is in fd->cwd. 1 full game installed, 2 demo installed, -1 refused (fd->msg says
 * why and nothing is copied). The table and demo hash are parameters so tests can inject a synthetic set; a NULL table means the built-in ones. */
int quake_pick_commit(FileDialog *fd, const char *home, const QuakePak *table, int n,
                      off_t demo_size, const char *demo_sha);
/* Builds the browse roots (volume plus the host's Downloads, Desktop, Documents); returns the count. */
int quake_pick_roots(const char *volume, const char *host_home, FdRoot *roots, char paths[][OSPI_PATH]);

int quake_pick_open(Session *s);
int quake_pick_panel_paint(Fb *fb, const Window *w, const Layout *lay);   /* 1 when w is the chooser */
int quake_pick_panel_click(Session *s, Window *w, int x, int y);          /* 1 when handled */
int quake_pick_panel_key(Session *s, Window *w, int key, int mods);       /* 1 when used */
int quake_pick_panel_wheel(Window *w, int delta);                          /* 1 when used */
#endif
