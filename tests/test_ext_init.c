#include "ext_init.h"
#include "ext_runtime.h"
#include "mac_init.h"
#include "ext_fixture.h"

#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

enum { PATCHED_KEY = 0x9a0 }; /* trap_key($a9a0) */
static uint32_t stub(unsigned key) { return 0x2000u + key * 8; }

static int host(MacRuntime *rt) {
    char err[160];
    int rc = mac_init_host_create(rt, err, sizeof err);
    if (rc) fprintf(stderr, "host: %s\n", err);
    return rc;
}

static int run_code(MacRuntime *rt, const uint8_t *code, size_t n, size_t budget, MacInitReport *rep) {
    uint8_t buf[1024];
    MacResourceFork fork;
    char err[96];
    size_t size = fx_fork(buf, sizeof buf, code, n);
    CHECK(size && mac_resource_open(&fork, buf, size, err, sizeof err) == 0);
    return mac_init_run(rt, &fork, budget, rep);
}

static void test_runner(void) {
    static MacRuntime rt;
    MacInitReport rep;
    CHECK(host(&rt) == 0);
    uint32_t pc = mac_cpu_get(rt.cpu, MAC_PC), sp = mac_cpu_get(rt.cpu, MAC_A7);

    CHECK(run_code(&rt, FX_OK, sizeof FX_OK, 1000, &rep) == 0 && rep.count == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_PC) == pc && mac_cpu_get(rt.cpu, MAC_A7) == sp); /* registers restored */

    /* A patch takes effect and the original stays reachable via GetTrapAddress. */
    CHECK(rt.trap_addresses[PATCHED_KEY] == stub(PATCHED_KEY));
    CHECK(run_code(&rt, FX_PATCH, sizeof FX_PATCH, 1000, &rep) == 0);
    CHECK(rt.trap_addresses[PATCHED_KEY] != stub(PATCHED_KEY));
    CHECK(rt.trap_addresses[PATCHED_KEY] < mac_cpu_ram_size(rt.cpu) && !(rt.trap_addresses[PATCHED_KEY] & 1));

    /* Gestalt: registered once, duplicates refused, built-ins refused. */
    CHECK(rt.gestalt_count == 0);
    CHECK(run_code(&rt, FX_GESTALT, sizeof FX_GESTALT, 1000, &rep) == 0);
    CHECK(rt.gestalt_count == 1 && rt.gestalt_sel[0] == 0x74657374u);
    CHECK(run_code(&rt, FX_GESTALT, sizeof FX_GESTALT, 1000, &rep) == 0); /* dup selector: error code in D0, nothing added */
    CHECK(rt.gestalt_count == 1);
    CHECK(mac_gestalt_set(&rt, 0x73797376u, 1, 0) == -1); /* 'sysv' is built in */
    CHECK(mac_gestalt_set(&rt, 0x61626364u, 77, 0) == 0);
    mac_runtime_destroy(&rt);
}

static void test_startup_ids(void) {
    static MacRuntime rt;
    MacInitReport rep;
    uint8_t buf[1024];
    MacResourceFork fork;
    char err[96];
    CHECK(host(&rt) == 0);
    /* INIT 0 is startup code; INIT 128 is patch code and must never run (here it would loop forever). */
    const int16_t ids[2] = {0, 128};
    const uint8_t *codes[2] = {FX_OK, FX_LOOP};
    const size_t lens[2] = {sizeof FX_OK, sizeof FX_LOOP};
    size_t size = fx_fork_multi(buf, sizeof buf, 2, ids, codes, lens);
    CHECK(size && mac_resource_open(&fork, buf, size, err, sizeof err) == 0);
    CHECK(mac_init_run(&rt, &fork, 500, &rep) == 0 && rep.count == 1);
    /* A fork with only patch code has no startup INIT. */
    const int16_t only[1] = {128};
    const uint8_t *oc[1] = {FX_OK};
    const size_t ol[1] = {sizeof FX_OK};
    size = fx_fork_multi(buf, sizeof buf, 1, only, oc, ol);
    CHECK(size && mac_resource_open(&fork, buf, size, err, sizeof err) == 0);
    CHECK(mac_init_run(&rt, &fork, 500, &rep) == 0 && rep.count == 0);
    mac_runtime_destroy(&rt);
}

static void test_rollback(void) {
    static MacRuntime rt;
    MacInitReport rep;
    CHECK(host(&rt) == 0);
    CHECK(run_code(&rt, FX_OK, sizeof FX_OK, 1000, &rep) == 0); /* first run prepares graphics; measure after it */
    uint32_t heap = rt.heap_next;

    /* Patches one trap, then touches a protected one: both are undone. */
    CHECK(run_code(&rt, FX_PATCH_PROTECTED, sizeof FX_PATCH_PROTECTED, 1000, &rep) == -1);
    CHECK(strstr(rep.error, "protected") != NULL);
    CHECK(rt.trap_addresses[PATCHED_KEY] == stub(PATCHED_KEY)); /* first patch rolled back */
    CHECK(rt.trap_addresses[0x9f0] == stub(0x9f0)); /* LoadSeg untouched */
    CHECK(rt.heap_next == heap);

    /* D0 is not a status: real INITs leave junk there. */
    CHECK(run_code(&rt, FX_JUNK_D0, sizeof FX_JUNK_D0, 1000, &rep) == 0 && rep.count == 1);

    /* FSpDispatch pops the documented argument bytes (the runner checks stack balance). */
    CHECK(run_code(&rt, FX_FSP_DELETE, sizeof FX_FSP_DELETE, 1000, &rep) == 0);
    CHECK(run_code(&rt, FX_FSP_OPENRES, sizeof FX_FSP_OPENRES, 1000, &rep) == 0);
    CHECK(run_code(&rt, FX_FSP_CREATERES, sizeof FX_FSP_CREATERES, 1000, &rep) == 0);

    CHECK(run_code(&rt, FX_LOOP, sizeof FX_LOOP, 500, &rep) == -1);
    CHECK(strstr(rep.error, "budget") != NULL);
    CHECK(!mac_cpu_halted(rt.cpu));
    CHECK(run_code(&rt, FX_OK, sizeof FX_OK, 1000, &rep) == 0); /* runtime still usable */

    CHECK(run_code(&rt, FX_HALT, sizeof FX_HALT, 1000, &rep) == -2); /* unsupported trap halts the CPU */
    CHECK(mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);

    /* LoadSeg is host-only: an INIT calling it fails instead of reading the host's CODE 0. */
    CHECK(host(&rt) == 0);
    CHECK(run_code(&rt, FX_LOADSEG, sizeof FX_LOADSEG, 1000, &rep) == -2);
    CHECK(strstr(rep.error, "LoadSeg") != NULL);
    mac_runtime_destroy(&rt);

    /* A selector nobody has confirmed stays unhandled instead of being guessed. */
    CHECK(host(&rt) == 0);
    CHECK(run_code(&rt, FX_FSP_UNKNOWN, sizeof FX_FSP_UNKNOWN, 1000, &rep) == -2);
    mac_runtime_destroy(&rt);
}

static char root[64];
static void put(const char *name, const uint8_t *code, size_t n, const char *info) {
    char path[300];
    snprintf(path, sizeof path, "%s/System Folder/Extensions/%s", root, name);
    CHECK(fx_write(path, code, n) == 0);
    if (info) {
        strcat(path, ".extinfo");
        CHECK(fx_write_text(path, info) == 0);
    }
}

static void test_loader(void) {
    snprintf(root, sizeof root, "/tmp/os999-extinit-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    char cmd[200];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/System Folder/Extensions'", root);
    CHECK(system(cmd) == 0);
    put("A Patcher", FX_PATCH, sizeof FX_PATCH, NULL);
    put("B Bad", FX_LOOP, sizeof FX_LOOP, NULL);
    put("C Crasher", FX_HALT, sizeof FX_HALT, NULL);
    put("D Gestalt", FX_GESTALT, sizeof FX_GESTALT, "requires A Patcher\n");
    put("E Off", FX_GESTALT, sizeof FX_GESTALT, NULL);
    put("F Empty", FX_OK, sizeof FX_OK, NULL);
    { uint8_t b[512]; const int16_t id = 128; const uint8_t *c = FX_OK; const size_t l = sizeof FX_OK;
      char path[300]; size_t sz = fx_fork_multi(b, sizeof b, 1, &id, &c, &l);
      snprintf(path, sizeof path, "%s/System Folder/Extensions/G Patchfile", root);
      FILE *pf = fopen(path, "wb"); CHECK(pf && fwrite(b, 1, sz, pf) == sz); if (pf) fclose(pf); }
    snprintf(cmd, sizeof cmd, "%s/System Folder/Extensions/F Empty", root);
    CHECK(fx_write_text(cmd, "not a resource fork") == 0);

    static ExtList list;
    CHECK(ext_scan(&list, root) == 7);
    CHECK(ext_set_enabled(&list, "E Off", 0) == 0);
    ext_order(&list);
    CHECK(ext_init_load_all(&list) == 2);
    CHECK(ext_find(&list, "A Patcher")->status == EXT_LOADED);
    CHECK(ext_find(&list, "B Bad")->status == EXT_FAILED && strstr(ext_find(&list, "B Bad")->error, "budget"));
    CHECK(ext_find(&list, "C Crasher")->status == EXT_FAILED);
    CHECK(ext_find(&list, "D Gestalt")->status == EXT_LOADED); /* still loads after the host was rebuilt */
    CHECK(ext_find(&list, "E Off")->status == EXT_DISABLED);
    CHECK(ext_find(&list, "F Empty")->status == EXT_FAILED);
    CHECK(ext_find(&list, "G Patchfile")->status == EXT_NO_INIT); /* only INIT 128: patch code, not startup */

    /* Apply the survivors to a live guest: only LOADED extensions run, in order. */
    static MacRuntime rt;
    char err[160];
    CHECK(host(&rt) == 0);
    CHECK(ext_init_apply(&list, &rt, err, sizeof err) == 2);
    CHECK(rt.trap_addresses[PATCHED_KEY] != stub(PATCHED_KEY));
    CHECK(rt.gestalt_count == 1 && rt.gestalt_sel[0] == 0x74657374u);
    mac_runtime_destroy(&rt);

    /* Runtime boot hook: the path ospi-runtime takes before an application starts. */
    CHECK(ext_state_save(&list, root) == 0);
    setenv("OSPI_EXT_ROOT", root, 1);
    unsetenv("OSPI_EXT_MODE");
    CHECK(host(&rt) == 0);
    CHECK(ext_runtime_boot(&rt) == 2);
    CHECK(rt.gestalt_count == 1 && rt.trap_addresses[PATCHED_KEY] != stub(PATCHED_KEY));
    static ExtList check;
    CHECK(ext_scan(&check, root) == 7 && ext_state_load(&check, root) == 0);
    CHECK(!check.crash_pending && !ext_find(&check, "E Off")->enabled); /* marker cleared, choice kept */
    mac_runtime_destroy(&rt);

    setenv("OSPI_EXT_MODE", "none", 1); /* Shift held at startup */
    CHECK(host(&rt) == 0 && ext_runtime_boot(&rt) == 0 && rt.gestalt_count == 0);
    mac_runtime_destroy(&rt);

    unsetenv("OSPI_EXT_MODE"); /* an earlier startup never finished */
    CHECK(ext_state_begin_boot(&check, root) == 0);
    CHECK(host(&rt) == 0 && ext_runtime_boot(&rt) == 0 && rt.gestalt_count == 0);
    mac_runtime_destroy(&rt);
    unsetenv("OSPI_EXT_ROOT");

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

int main(void) {
    test_runner();
    test_startup_ids();
    test_rollback();
    test_loader();
    if (failures) return 1;
    puts("test_ext_init: ok");
    return 0;
}
