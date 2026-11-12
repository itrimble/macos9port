#ifndef OS999_FILE_DIALOG_H
#define OS999_FILE_DIALOG_H
/* One file browser drawn inside OS999 (no NSOpenPanel, osascript or host dialog).
 *
 * The dialog is a plain state struct. A caller owns one FileDialog, draws it into
 * any Rect of the framebuffer and feeds it clicks, wheel, keys and typed text.
 * When state leaves FD_RUNNING, `result` holds the absolute, symlink-resolved path.
 *
 * Containment: the caller names the allowed roots (the OS999 volume, and for the
 * Movie chooser three home media folders). Every root is resolved with realpath
 * when the dialog opens. Every folder the dialog enters, every file it returns
 * and every entry it lists is resolved with realpath and must lie inside one
 * root (component-aware: /a/b does not contain /a/bc). Aliases (symlinks) that
 * lead outside the roots are hidden and refused. A new file in Save mode is
 * only ever created in the already-contained current folder, and an existing
 * symlink at that name is refused rather than replaced. */
#include "draw.h"
#include "ospi.h"

enum { FD_ROOTS = 6, FD_ENT_MAX = 512, FD_LABEL = 24 };
typedef enum { FD_OPEN = 0, FD_SAVE = 1 } FdMode;
typedef enum { FD_RUNNING = 0, FD_ACCEPTED = 1, FD_CANCELLED = 2 } FdState;
typedef struct { const char *label; const char *path; } FdRoot;
/* Return nonzero for file names the Open dialog may pick (folders always show). */
typedef int (*FdFilter)(const char *name);
typedef struct { char name[OSPI_NAME]; unsigned char dir; } FdEntry;

typedef struct FileDialog {
    FdMode mode;
    FdState state;
    char title[48];
    char root_label[FD_ROOTS][FD_LABEL];
    char root_path[FD_ROOTS][OSPI_PATH];   /* realpath'd */
    int nroots, root;                      /* root holding cwd */
    char cwd[OSPI_PATH];                   /* realpath'd, inside root_path[root] */
    FdFilter filter;
    char filter_label[FD_LABEL];
    int filter_on;
    FdEntry ent[FD_ENT_MAX];
    int nent, sel, top, rows, truncated;   /* sel -1: none; rows: visible list rows */
    char name[OSPI_NAME];                  /* Save mode name field */
    int confirm;                           /* overwrite question is showing */
    char msg[120];                         /* last problem, shown under the list */
    char result[OSPI_PATH];
} FileDialog;

/* roots: 1..FD_ROOTS entries; unusable ones (missing, not a folder, "/") are skipped.
 * start: preferred folder (must lie inside a root, else the first root's top).
 * default_name: Save mode initial name (may be NULL). Returns -1 when no root is usable. */
int fd_open(FileDialog *fd, FdMode mode, const char *title, const FdRoot *roots, int nroots,
            const char *start, const char *default_name);
void fd_set_filter(FileDialog *fd, FdFilter filter, const char *label);   /* label e.g. "Movies" */

/* Navigation. Each returns 0 on success; on failure the dialog stays where it was and fd->msg says why. */
int fd_goto(FileDialog *fd, const char *path);        /* absolute or relative to cwd */
int fd_up(FileDialog *fd);                            /* refuses above a root */
int fd_select_root(FileDialog *fd, int root);
int fd_contains(const FileDialog *fd, const char *real_path);   /* root index or -1 */

void fd_select(FileDialog *fd, int index);
void fd_move(FileDialog *fd, int delta);
int fd_activate(FileDialog *fd);                      /* Return / Open / Save button: 1 when something changed */
void fd_cancel(FileDialog *fd);                       /* Escape / Cancel; first closes an overwrite question */
void fd_toggle_filter(FileDialog *fd);

/* Save mode name field. */
int fd_name_check(const char *name, const char **why);   /* 0 valid */
void fd_type(FileDialog *fd, const char *utf8);       /* ASCII printable only; resets any overwrite question */
void fd_backspace(FileDialog *fd);

/* SDL key codes as session_key receives them. Returns 1 when used. */
int fd_key(FileDialog *fd, int key, int mods);

/* drawing and pointer (file_dialog_draw.c) */
void fd_paint(Fb *fb, FileDialog *fd, Rect area);
int fd_click(FileDialog *fd, Rect area, int x, int y);   /* 1 when the press was inside the dialog */
void fd_wheel(FileDialog *fd, int delta);
#endif
