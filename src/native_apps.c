#include "native_apps.h"
#include "notes.h"
#include "finder_recent.h"
#include "movie_window.h"
#include "session_int.h"
#include "term.h"

void native_app_note(Session *s, int app) {
    const char *path = native_catalog_path(app);
    if (s && path && native_catalog_id(s->root, path) == app) finder_recent_note(s, path, 1);
}

int native_app_open(Session *s, int app) {
    if (!s) return -1;
    switch (app) {
    case NATIVE_TERMINAL: return terminal_open(s, TERM_MODE_SHELL);
    case NATIVE_TAILSCALE: return terminal_tailnet_open(s);
    case NATIVE_CONSOLE: return terminal_open(s, TERM_MODE_CONSOLE);
    case NATIVE_BROWSER: session_command(s, CMD_BROWSER); return 0;
    case NATIVE_MUSIC:
        if (!session_media_hooks() || !session_media_hooks()->open_panel) {
            dialog_info(s, "OS999 Amp", "The audio player is unavailable in this build.");
            return -1;
        }
        session_command(s, CMD_MEDIA_PLAYER);
        return 0;
    case NATIVE_MOVIE: return movie_window_launcher(s);
    case NATIVE_NOTES: {
        if (notes_open_document(s, "Notes", "Documents/Notes") != 0) return -1;
        native_app_note(s, app);
        return 0;
    }
    case NATIVE_CONTROLS: session_command(s, CMD_CONTROL_PANELS); return 0;
    case NATIVE_QUAKE:
        if (session_open_quake_app(s)) return -1;
        native_app_note(s, app);
        return 0;
    default: return -1;
    }
}
