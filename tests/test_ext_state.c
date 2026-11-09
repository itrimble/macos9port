#include "ext_registry.h"
#include "ext_fixture.h"

#include <stdlib.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char root[64];

static void fresh(ExtList *l) {
    memset(l, 0, sizeof *l);
    CHECK(ext_scan(l, root) >= 0);
    CHECK(ext_state_load(l, root) == 0);
    ext_order(l);
}

static char *slurp(char *buf, size_t n) {
    char path[160];
    snprintf(path, sizeof path, "%s/.os999-extensions", root);
    FILE *f = fopen(path, "r");
    size_t got = f ? fread(buf, 1, n - 1, f) : 0;
    if (f) fclose(f);
    buf[got] = 0;
    return buf;
}

int main(void) {
    snprintf(root, sizeof root, "/tmp/os999-extstate-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    char cmd[200], dir[160], path[300], text[1024];
    snprintf(dir, sizeof dir, "%s/System Folder/Extensions", root);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    CHECK(system(cmd) == 0);
    const char *names[] = {"One", "Two", "Three"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        CHECK(fx_write(path, FX_OK, sizeof FX_OK) == 0);
    }
    static ExtList a, b;

    /* Toggle persistence round trip. */
    fresh(&a);
    CHECK(a.count == 3 && a.load_count == 3);
    CHECK(ext_set_enabled(&a, "Two", 0) == 0);
    CHECK(ext_state_save(&a, root) == 0);
    fresh(&b);
    CHECK(!ext_find(&b, "Two")->enabled && ext_find(&b, "One")->enabled);
    CHECK(ext_find(&b, "Two")->status == EXT_DISABLED && b.load_count == 2);

    /* A disabled extension that is removed and restored stays disabled. */
    snprintf(path, sizeof path, "%s/Two", dir);
    CHECK(unlink(path) == 0);
    fresh(&b);
    CHECK(b.count == 2 && b.disabled_count == 1);
    CHECK(ext_state_save(&b, root) == 0);
    CHECK(fx_write(path, FX_OK, sizeof FX_OK) == 0);
    fresh(&b);
    CHECK(!ext_find(&b, "Two")->enabled);

    /* Saved sets. */
    CHECK(ext_set_save(&b, "Minimal") == 0);
    CHECK(ext_set_enabled(&b, "Two", 1) == 0 && ext_set_enabled(&b, "One", 0) == 0);
    CHECK(ext_set_save(&b, "Everything but One") == 0);
    CHECK(ext_state_save(&b, root) == 0);
    fresh(&a);
    CHECK(a.set_count == 2);
    CHECK(ext_set_apply(&a, "Minimal") == 0);
    CHECK(ext_find(&a, "One")->enabled && !ext_find(&a, "Two")->enabled && ext_find(&a, "Three")->enabled);
    CHECK(ext_set_apply(&a, "Nope") == -1);

    /* Atomic write leaves no temp file, and the header is versioned. */
    CHECK(strncmp(slurp(text, sizeof text), "OS999-EXT-1\n", 12) == 0);
    snprintf(cmd, sizeof cmd, "ls -a '%s' | grep -c 'tmp$' >/dev/null", root);
    CHECK(system(cmd) != 0); /* grep -c exits 1 when there are none */

    /* Corrupt or foreign state files fall back to everything enabled. */
    snprintf(path, sizeof path, "%s/.os999-extensions", root);
    CHECK(fx_write_text(path, "not our format\nD One\n") == 0);
    fresh(&a);
    CHECK(ext_find(&a, "One")->enabled && a.disabled_count == 0 && !a.crash_pending);

    /* Crash-loop marker: begin sets it, end clears it. */
    CHECK(ext_state_begin_boot(&a, root) == 0);
    fresh(&b);
    CHECK(b.crash_pending == 1);
    CHECK(ext_state_end_boot(&b, root) == 0);
    fresh(&b);
    CHECK(b.crash_pending == 0);

    /* Boot override decisions. */
    CHECK(ext_boot_decide(0, 0, 0, 0) == EXT_BOOT_NORMAL);
    CHECK(ext_boot_decide(1, 0, 0, 0) == EXT_BOOT_MANAGER);
    CHECK(ext_boot_decide(0, 0, 1, 0) == EXT_BOOT_MANAGER);
    CHECK(ext_boot_decide(0, 0, 0, 1) == EXT_BOOT_MANAGER);
    CHECK(ext_boot_decide(0, 1, 0, 0) == EXT_BOOT_NONE);
    CHECK(ext_boot_decide(1, 1, 0, 0) == EXT_BOOT_MANAGER); /* Space wins over Shift */
    ext_boot_apply(&b, EXT_BOOT_NORMAL);
    CHECK(b.load_count == 3);
    ext_boot_apply(&b, EXT_BOOT_NONE);
    CHECK(b.load_count == 0 && ext_find(&b, "One")->status == EXT_DISABLED);
    ext_state_save(&b, root); /* NONE is a one-time override: persisted state is untouched */
    fresh(&a);
    CHECK(a.load_count == 3);

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    if (failures) return 1;
    puts("test_ext_state: ok");
    return 0;
}
