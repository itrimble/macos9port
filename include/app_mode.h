#ifndef OS999_APP_MODE_H
#define OS999_APP_MODE_H

#include "ospi.h"
#include "draw.h"
#include "app_classify.h"
#include "launch_profile.h"
#include "compat_known.h"

/* The "Memory mode" choice of an application's Get Info window: Automatic (the launch policy decides), 24-bit (16 MB)
 * or 32-bit. It writes or clears the per-application override (launch_profile.h), which is keyed on the executable's
 * identity, and shows what the classifier and the built-in list say. The model below has no UI state, so it is tested
 * headless; the Session glue only routes the dialog's rows to it. */

enum { APP_MODE_AUTOMATIC = 0, APP_MODE_24BIT = 1, APP_MODE_32BIT = 2, APP_MODE_COUNT = 3 };
#define APP_MODE_ROWS_TOP 104   /* y of the first row inside the dialog box (the Get Info lines sit above it) */

typedef struct {
    AppClassification c;
    LaunchOverride ov;
    const CompatKnown *known;
    char root[1024];            /* canonical volume root (PATH_MAX) */
    uint64_t slot;
    int has_override;
    int mode;                   /* the choice currently saved (APP_MODE_*) */
    char summary[APP_TEXT * 3]; /* profile, source and reason, for the panel */
} AppMode;

/* 0 loaded; -1 not an application the policy knows (no executable code, unreadable). `full` is a path inside `root`. */
int app_mode_load(AppMode *m, const char *root, const char *full);
/* Saves `mode`: Automatic clears the override, the others set it. 0 ok, -1 failed. Reloads nothing. */
int app_mode_save(const AppMode *m, int mode);
const char *app_mode_label(int mode);

/* Dialog glue (the dialog is the Finder settings dialog, kind CMD_APP_MODE). */
int app_mode_open(Session *s, const char *rel, const char *info);   /* 0 opened, -1 not applicable */
int app_mode_active(const Session *s);
void app_mode_choose(Session *s, int row);
int app_mode_apply(Session *s);                                     /* the OK button; 0 saved */
void app_mode_paint(Fb *fb, const Session *s, Rect box);

#endif
