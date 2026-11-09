#include "ext_runtime.h"
#include "ext_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ext_runtime_boot(MacRuntime *rt) {
    const char *root = getenv("OSPI_EXT_ROOT"), *mode = getenv("OSPI_EXT_MODE");
    if (!rt || !root || !root[0]) return 0;
    static ExtList list;
    if (ext_scan(&list, root) <= 0) return 0;
    ext_state_load(&list, root);
    if (mode && !strcmp(mode, "none")) {
        fprintf(stderr, "Extensions: skipped for this startup (Shift held).\n");
        return 0;
    }
    if (list.crash_pending) {
        fprintf(stderr, "Extensions: the previous startup did not finish; loading none. "
                        "Open the Extensions Manager to review them.\n");
        return 0;
    }
    ext_order(&list);
    ext_state_begin_boot(&list, root); /* stays set if a load takes the process down */
    int loaded = ext_init_load_all(&list);
    char err[160] = "";
    int applied = loaded > 0 ? ext_init_apply(&list, rt, err, sizeof err) : 0;
    ext_state_end_boot(&list, root);
    for (int i = 0; i < list.count; i++)
        fprintf(stderr, "Extension %s: %s%s%s\n", list.items[i].name, ext_status_label(list.items[i].status),
                list.items[i].error[0] ? " - " : "", list.items[i].error);
    if (applied < 0) {
        fprintf(stderr, "Extensions: %s\n", err);
        return -1;
    }
    return applied;
}
