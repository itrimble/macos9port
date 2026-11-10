#include "control_panels.h"
#include "control_panels_int.h"
#include "appearance_panel.h"
#include "desktop_assets.h"
#include "ext_fixture.h"
#include "ext_ui.h"
#include "os999_cdev.h"
#include "ospi.h"

#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void click_at(Session *s, int x, int y) { session_down(s, x, y, 1); session_up(s, x, y); }

int main(int argc, char **argv) {
    CHECK(desktop_assets_load(argc > 1 ? argv[1] : "assets/icons/ospi-icons.icl8"));
    char root[] = "/tmp/os999-cdevext-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    char dir[OSPI_PATH], path[OSPI_PATH + 64];
    snprintf(dir, sizeof dir, "%s/System Folder/Extensions", root);
    struct stat st;
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode)); /* seeded */
    /* A new volume carries four OS999 sample extensions; this test wants its own three. */
    const char *samples[] = {"Sample Hello", "Sample Gestalt", "Sample Gestalt.extinfo", "Sample Needs Hello",
                             "Sample Needs Hello.extinfo", "Sample Faulty", "Sample Faulty.extinfo"};
    ExtList seeded;
    CHECK(ext_scan(&seeded, root) == 4);
    for (size_t i = 0; i < sizeof samples / sizeof *samples; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, samples[i]);
        CHECK(unlink(path) == 0);
    }
    CHECK(volume_seed(root) == 0 && ext_scan(&seeded, root) == 0); /* removed samples do not come back */
    snprintf(path, sizeof path, "%s/System Folder/Control Panels", root);
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    const char *names[] = {"Alpha", "Bravo", "Charlie"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        CHECK(fx_write(path, FX_OK, sizeof FX_OK) == 0);
    }

    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return 1;
    CHECK(os999_cdev_find("extensions") != NULL);
    uint32_t *px = calloc((size_t)(OSPI_W * OSPI_H), sizeof *px);
    CHECK(px != NULL);

    /* Reachable from the Apple-style Control Panels menu, enabled, and opens the manager. */
    menu_update(s);
    int found = 0;
    for (int i = 0; i < menu_nitems(MENU_PANELS); i++) {
        const MenuItem *mi = menu_at(MENU_PANELS, i);
        if (mi && mi->cmd == CMD_CP_EXTENSIONS) {
            found = 1;
            CHECK(!(mi->flags & MI_DISABLED) && mi->label && !strcmp(mi->label, "Extensions Manager"));
        }
    }
    CHECK(found);
    session_command(s, CMD_CP_EXTENSIONS);
    CHECK(control_panels_active_module_id() && !strcmp(control_panels_active_module_id(), "extensions"));
    control_panels_close();
    CHECK(control_panels_open_module(s, "extensions") == 0);
    CHECK(control_panels_active_module_id() && !strcmp(control_panels_active_module_id(), "extensions"));
    session_paint(s, px, OSPI_W, OSPI_H);
    Rect regions[2];
    CHECK(appearance_panel_regions(s, regions) == 1);
    Rect r = {regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    Rect c = cp_content_rect(r);

    /* Clicking Bravo's checkbox turns it off and persists immediately. */
    Rect box = ext_ui_checkbox_rect(c, 1);
    click_at(s, box.x + 6, box.y + box.h / 2);
    session_paint(s, px, OSPI_W, OSPI_H);
    static ExtList on_disk;
    CHECK(ext_scan(&on_disk, root) == 3 && ext_state_load(&on_disk, root) == 0);
    CHECK(!ext_find(&on_disk, "Bravo")->enabled);
    CHECK(ext_find(&on_disk, "Alpha")->enabled && ext_find(&on_disk, "Charlie")->enabled);

    /* Disable All, then Enable All. */
    Rect b1 = ext_ui_button_rect(c, 1), b0 = ext_ui_button_rect(c, 0);
    click_at(s, b1.x + 4, b1.y + 4);
    ext_state_load(&on_disk, root);
    CHECK(on_disk.disabled_count == 3);
    click_at(s, b0.x + 4, b0.y + 4);
    ext_state_load(&on_disk, root);
    CHECK(on_disk.disabled_count == 0);
    session_key(s, 'w', MOD_CMD);
    CHECK(!control_panels_active());
    session_destroy(s);

    /* Boot overlay model: same state, Continue buttons. */
    static ExtManager m;
    CHECK(ext_manager_open(&m, root, 1) == 3);
    Rect oc = {0, 0, 600, 400};
    Rect cont = ext_ui_button_rect(oc, 4), dis = ext_ui_button_rect(oc, 5);
    CHECK(!m.done && ext_manager_click(&m, oc, cont.x + 2, cont.y + 2) && m.done && !m.disable_all_and_continue);
    CHECK(ext_manager_open(&m, root, 1) == 3);
    CHECK(ext_manager_click(&m, oc, dis.x + 2, dis.y + 2) && m.done && m.disable_all_and_continue);
    ext_state_load(&on_disk, root);
    CHECK(on_disk.disabled_count == 3);
    CHECK(ext_manager_open(&m, root, 1) == 3 && ext_manager_key(&m, '\r') && m.done);
    /* The non-overlay panel ignores Return and has no Continue buttons. */
    CHECK(ext_manager_open(&m, root, 0) == 3 && !ext_manager_key(&m, '\r') && !m.done);
    CHECK(!ext_manager_click(&m, oc, cont.x + 2, cont.y + 2));

    free(px);
    desktop_assets_reset();
    char cmd[200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    printf("cdev_extensions: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
