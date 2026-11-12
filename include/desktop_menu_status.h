#ifndef OS999_DESKTOP_MENU_STATUS_H
#define OS999_DESKTOP_MENU_STATUS_H
#include "ospi.h"
/* V08: why a desktop menu command is unavailable in the current context.
 * Returns a short plain sentence, or NULL when the command has no entry here.
 * Only meaningful while the matching menu item is disabled. */
const char *desktop_menu_reason(const Session *s, int command);
/* Handles commands owned by this file (Sleep, the unavailable-key notice).
 * Returns 1 when the command was consumed. */
int desktop_menu_command(Session *s, int command);
/* Session that menu_update last described, so a disabled key equivalent can
 * explain itself. Set by menu_update. */
void desktop_menu_track(Session *s);
Session *desktop_menu_tracked(void);
/* Remember which disabled command a key equivalent chose (0 clears). */
void desktop_menu_note_unavailable(int command);
/* Forget a session that is being destroyed. */
void desktop_menu_untrack(const Session *s);
#endif
