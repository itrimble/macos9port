#include "finder_system_icons.h"
#include "system_icon_positions.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "appearance_panel.h"
#include "session_int.h"

void finder_system_icons_move(Session *s, int which, int x, int y) {
    if (!s || which < 0 || which > 1 || finder_apps_modal(s) ||
        finder_apps_occluded(s, 1, s->mx, s->my) || app_switcher_hit(s, s->mx, s->my)) return;
    Rect panels[2]; int count = appearance_panel_regions(s, panels);
    for (int i = 0; i < count; i++) if (rect_has(panels[i], s->mx, s->my)) return;
    if (which == 0 && session_desktop_which(s, s->mx, s->my) == 1) {
        dialog_info(s, "Eject", "OS999 HD is the active desktop volume and cannot be ejected."); return;
    }
    Rect icon, hit; desktop_position_rect(s->sw, s->sh, x, y, &icon, &hit);
    SystemIconPositions draft = s->system_icons;
    draft.mask |= 1 << which; draft.x[which] = icon.x; draft.y[which] = icon.y;
    if (system_icon_positions_save(s->root, &draft)) {
        dialog_info(s, "Move Icon", "The icon position could not be saved. Its previous position was kept."); return;
    }
    s->system_icons = draft;
}
