#include "session_int.h"

#include <string.h>
#include <strings.h>

int session_is_sit(const char *name) {
    size_t n = name ? strlen(name) : 0;
    return n > 4 && strcasecmp(name + n - 4, ".sit") == 0;
}

void session_open_sit(Session *s, const char *rel) {
    char expanded[OSPI_PATH] = {0};
    char err[240] = {0};
    if (install_sit(s->root, rel, expanded, sizeof expanded, err, sizeof err) != 0) {
        if (expanded[0]) open_folder(s, expanded);
        dialog_info(s, "StuffIt", err[0] ? err : "Could not expand this .sit archive.");
        return;
    }
    if (open_folder(s, expanded) != 0)
        dialog_info(s, "StuffIt", "The archive was expanded; close a window to open its folder.");
}
