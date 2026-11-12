/* The path ospi-runtime takes for a PowerPC application: 'cfrg' 0 in the
 * resource fork, PEF in the data fork, a report for --inspect and an honest
 * "runtime is incomplete" diagnostic for launch. All files are synthetic and
 * are written to a private temporary directory. */
#include "mac_ppc_probe.h"
#include "pef_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Hand-authored resource fork: one resource (ID 0) per given type. */
static size_t build_fork(uint8_t *p, const char *const *types, const uint8_t *const *datas, const size_t *sizes, int n) {
    size_t data = 256, at = data;
    memset(p, 0, 256);
    uint32_t offsets[4];
    for (int i = 0; i < n; i++) {
        offsets[i] = (uint32_t)(at - data);
        fix32(p + at, (uint32_t)sizes[i]);
        memcpy(p + at + 4, datas[i], sizes[i]);
        at += 4 + sizes[i];
    }
    size_t map = at, type_list = map + 28, refs = type_list + 2 + 8 * (size_t)n, end = refs + 12 * (size_t)n;
    memset(p + map, 0, end - map);
    fix32(p, (uint32_t)data); fix32(p + 4, (uint32_t)map); fix32(p + 8, (uint32_t)(map - data)); fix32(p + 12, (uint32_t)(end - map));
    fix16(p + map + 24, 28);
    fix16(p + map + 26, (uint32_t)(end - map));
    fix16(p + type_list, (uint32_t)(n - 1));
    for (int i = 0; i < n; i++) {
        uint8_t *t = p + type_list + 2 + 8 * i, *r = p + refs + 12 * i;
        memcpy(t, types[i], 4);
        fix16(t + 4, 0);
        fix16(t + 6, (uint32_t)(2 + 8 * n + 12 * i));
        fix16(r, 0);
        fix16(r + 2, 0xffff);
        fix32(r + 4, offsets[i]); /* attribute byte 0, 24-bit data offset */
    }
    return end;
}

static size_t build_cfrg(uint8_t *r, uint32_t offset, uint32_t length, uint8_t where) {
    memset(r, 0, 32 + 48);
    fix16(r + 30, 1);
    uint8_t *m = r + 32;
    memcpy(m, "pwpc", 4);
    m[22] = MAC_CFRG_APPLICATION; m[23] = where;
    fix32(m + 24, offset); fix32(m + 28, length);
    fix16(m + 40, 48); m[42] = 4; memcpy(m + 43, "Demo", 4);
    return 32 + 48;
}

static size_t build_pef(uint8_t *out, size_t cap, int with_toolbox) {
    static const uint8_t code[8] = {0x38, 0x60, 0x00, 0x2a, 0x4e, 0x80, 0x00, 0x20};
    static const uint8_t data[16] = {0};
    static const FixLibrary libraries[] = {{"StdCLib", 0, 1}, {"InterfaceLib", 1, 2}};
    static const FixImport imports[] = {{"strlen", 2}, {"NewWindow", 2}, {"qd", 1}};
    static const uint16_t relocs[] = {0x4600, 0x4a00};
    PefFixture f = {0};
    f.code = code; f.code_size = sizeof code; f.data = data; f.data_packed = f.data_unpacked = f.data_total = sizeof data;
    f.data_kind = 1; f.libraries = libraries; f.library_count = with_toolbox ? 2 : 1;
    f.imports = imports; f.import_count = with_toolbox ? 3 : 1;
    f.reloc = relocs; f.reloc_count = 2; f.reloc_section = 1; f.main_section = 1;
    return pef_fixture_build(&f, out, cap, NULL);
}

static int write_file(const char *path, const uint8_t *prefix, size_t prefix_size, const uint8_t *bytes, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(prefix, 1, prefix_size, f) == prefix_size && fwrite(bytes, 1, size, f) == size;
    return fclose(f) || !ok ? -1 : 0;
}

static char report[4096];
static int inspect(const MacResourceFork *fork, const char *path) {
    FILE *out = tmpfile();
    if (!out) return -1;
    int rc = mac_ppc_inspect(fork, path, out);
    long n = ftell(out);
    rewind(out);
    memset(report, 0, sizeof report);
    if (n > 0 && fread(report, 1, (size_t)n < sizeof report - 1 ? (size_t)n : sizeof report - 1, out) == 0) rc = -1;
    fclose(out);
    return rc;
}

int main(void) {
    char dir[256], data_path[320], fork_path[320], other[320], message[400], derived[320];
    const char *tmp = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/os999-ppc-probe-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    snprintf(data_path, sizeof data_path, "%s/Demo", dir);
    snprintf(fork_path, sizeof fork_path, "%s/Demo.rsrc", dir);
    snprintf(other, sizeof other, "%s/Other.rsrc", dir);

    uint8_t pef[1024], cfrg[96], fork_bytes[1024], prefix[16];
    const uint8_t code0[20] = {0};
    memset(prefix, 0xaa, sizeof prefix);
    size_t pef_size = build_pef(pef, sizeof pef, 1);
    size_t cfrg_size = build_cfrg(cfrg, sizeof prefix, (uint32_t)pef_size, MAC_CFRG_IN_DATA_FORK);
    const char *types[] = {"cfrg", "CODE"};
    const uint8_t *datas[] = {cfrg, code0};
    size_t sizes[] = {cfrg_size, sizeof code0};
    MacResourceFork fork;
    MacCfrgMember member;
    char err[200];

    CHECK(pef_size && write_file(data_path, prefix, sizeof prefix, pef, pef_size) == 0);
    size_t fork_size = build_fork(fork_bytes, types, datas, sizes, 1);
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_fork_member(&fork, &member) == 1 && member.usage == MAC_CFRG_APPLICATION && member.offset == 16 &&
          member.length == pef_size && !strcmp(member.name, "Demo"));
    CHECK(mac_ppc_data_fork_path(fork_path, derived, sizeof derived) == 0 && !strcmp(derived, data_path));
    CHECK(mac_ppc_data_fork_path("/a/Demo/..namedfork/rsrc", derived, sizeof derived) == 0 && !strcmp(derived, "/a/Demo"));
    CHECK(mac_ppc_data_fork_path("/a/Demo", derived, sizeof derived) == -1 && mac_ppc_data_fork_path(".rsrc", derived, sizeof derived) == -1);
    CHECK(mac_ppc_data_fork_path(fork_path, derived, 8) == -1);

    /* --inspect: the fragment is described, each import says whether the host implements it. */
    CHECK(inspect(&fork, fork_path) == 1);
    CHECK(strstr(report, "cfrg architecture=pwpc usage=application where=data-fork offset=16"));
    CHECK(strstr(report, "pef architecture=pwpc sections=3 instantiated=2"));
    CHECK(strstr(report, "pef section=0 kind=code total=8") && strstr(report, "pef section=1 kind=data total=16") &&
          strstr(report, "pef section=2 kind=loader"));
    CHECK(strstr(report, "pef main=1+0 init=-1+0") && strstr(report, "pef library=InterfaceLib imports=2"));
    CHECK(strstr(report, "pef import=StdCLib::strlen class=tvector binding=host"));
    CHECK(strstr(report, "pef import=InterfaceLib::NewWindow class=tvector binding=missing"));
    CHECK(strstr(report, "pef import=InterfaceLib::qd class=data binding=missing"));
    CHECK(strstr(report, "powerpc PowerPC runtime is incomplete: 2 of 3 imports have no host implementation, "
                         "first InterfaceLib::NewWindow."));

    /* Launch: a PowerPC-only application gets the diagnostic; one that also carries 68K CODE keeps the 68K path. */
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 1);
    CHECK(!strncmp(message, "PowerPC runtime is incomplete: 2 of 3 imports", 45) && strstr(message, "InterfaceLib::NewWindow"));
    fork_size = build_fork(fork_bytes, types, datas, sizes, 2);
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 0 && inspect(&fork, fork_path) == 1);
    fork_size = build_fork(fork_bytes, types + 1, datas + 1, sizes + 1, 1); /* a plain 68K application */
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_fork_member(&fork, &member) == 0 && inspect(&fork, fork_path) == 0 && !report[0]);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 0);

    /* When every import is bound the probe says so and nothing more: it loads, it does not run. */
    pef_size = build_pef(pef, sizeof pef, 0);
    cfrg_size = build_cfrg(cfrg, 0, 0, MAC_CFRG_IN_DATA_FORK); /* length 0: to the end of the fork */
    sizes[0] = cfrg_size;
    CHECK(write_file(data_path, prefix, 0, pef, pef_size) == 0);
    fork_size = build_fork(fork_bytes, types, datas, sizes, 1);
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 1 &&
          strstr(message, "Every import of the PowerPC fragment is bound or weak (0 weak imports resolve to null)."));
    CHECK(mac_ppc_probe(pef, pef_size, NULL, message, sizeof message) == 0);

    /* Damaged or missing fragments are reported, not loaded. */
    pef[13] ^= 0xff;
    CHECK(mac_ppc_probe(pef, pef_size, NULL, message, sizeof message) == -1 && strstr(message, "cannot be loaded"));
    CHECK(write_file(data_path, prefix, 0, pef, pef_size) == 0);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 1 && strstr(message, "cannot be loaded"));
    CHECK(mac_ppc_launch_diagnostic(&fork, other, message, sizeof message) == 1 && strstr(message, "data fork is missing"));
    CHECK(mac_ppc_launch_diagnostic(&fork, data_path, message, sizeof message) == 1 && strstr(message, "cannot derive"));
    cfrg_size = build_cfrg(cfrg, 4096, 64, MAC_CFRG_IN_DATA_FORK); /* names bytes past the end of the data fork */
    fork_size = build_fork(fork_bytes, types, datas, sizes, 1);
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 1 && strstr(message, "does not contain"));
    cfrg_size = build_cfrg(cfrg, 0, 0, MAC_CFRG_IN_RESOURCE);
    fork_size = build_fork(fork_bytes, types, datas, sizes, 1);
    CHECK(mac_resource_open(&fork, fork_bytes, fork_size, err, sizeof err) == 0);
    CHECK(mac_ppc_launch_diagnostic(&fork, fork_path, message, sizeof message) == 1 && strstr(message, "not stored in the data fork"));

    remove(data_path);
    rmdir(dir);
    (void)cfrg_size;
    if (failures) { fprintf(stderr, "test_mac_ppc_probe: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_ppc_probe: all %d checks passed\n", checks);
    return 0;
}
