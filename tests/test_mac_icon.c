#include "mac_icon.h"
#include "mac_resource.h"
#include "ospi.h"
#include "volume_directory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;

#define CHECK_COLOR(index, expected) do { \
    uint32_t actual = mac_icon_color(index); \
    if (actual != (expected)) { \
        fprintf(stderr, "palette[%u] expected %08x, got %08x\n", \
                (unsigned)(index), (unsigned)(expected), (unsigned)actual); \
        fails++; \
    } \
} while (0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fails++; \
    } \
} while (0)

typedef struct {
    const char *type;
    int16_t id;
    const uint8_t *data;
    uint32_t size;
} SynthRes;

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, (uint16_t)(v >> 16));
    put16(p + 2, (uint16_t)v);
}

static size_t build_fork(uint8_t *buf, size_t cap, const SynthRes *res, size_t count) {
    uint32_t data_off = 256, pos = 0, offsets[8];
    if (count == 0 || count > 8) return 0;
    memset(buf, 0, cap);
    for (size_t i = 0; i < count; i++) {
        if (data_off + pos + 4 + res[i].size > cap) return 0;
        offsets[i] = pos;
        put32(buf + data_off + pos, res[i].size);
        if (res[i].size && res[i].data) {
            memcpy(buf + data_off + pos + 4, res[i].data, res[i].size);
        }
        pos += 4 + res[i].size;
    }
    uint32_t map_off = data_off + pos;
    uint32_t types_off = 28;
    uint32_t refs_rel = (uint32_t)(2 + count * 8);
    uint32_t names_off = (uint32_t)(types_off + refs_rel + count * 12);
    if (map_off + names_off > cap) return 0;
    put32(buf, data_off);
    put32(buf + 4, map_off);
    put32(buf + 8, pos);
    put32(buf + 12, names_off);
    put16(buf + map_off + 24, (uint16_t)types_off);
    put16(buf + map_off + 26, (uint16_t)names_off);
    uint8_t *types = buf + map_off + types_off;
    put16(types, (uint16_t)(count - 1));
    for (size_t i = 0; i < count; i++) {
        uint8_t *t = types + 2 + i * 8;
        memcpy(t, res[i].type, 4);
        put16(t + 4, 0);
        put16(t + 6, (uint16_t)(refs_rel + i * 12));
        uint8_t *r = types + refs_rel + i * 12;
        put16(r, (uint16_t)res[i].id);
        put16(r + 2, 0xffff);
        r[4] = 0;
        r[5] = (uint8_t)(offsets[i] >> 16);
        r[6] = (uint8_t)(offsets[i] >> 8);
        r[7] = (uint8_t)offsets[i];
    }
    return map_off + names_off;
}

static int write_fork_file(const char *path, const SynthRes *res, size_t count) {
    uint8_t buf[8192];
    size_t sz = build_fork(buf, sizeof buf, res, count);
    if (!sz) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(buf, 1, sz, f) == sz;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void build_bndl(uint8_t *out, int16_t fref_local, int16_t fref_res,
                       int16_t icn_local, int16_t icn_res) {
    memset(out, 0, 28);
    memcpy(out, "OS99", 4);
    put16(out + 4, 0);
    put16(out + 6, 1); /* 2 resource types (count - 1) */
    memcpy(out + 8, "FREF", 4);
    put16(out + 12, 0);
    put16(out + 14, (uint16_t)fref_local);
    put16(out + 16, (uint16_t)fref_res);
    memcpy(out + 18, "ICN#", 4);
    put16(out + 22, 0);
    put16(out + 24, (uint16_t)icn_local);
    put16(out + 26, (uint16_t)icn_res);
}

static void test_bundle_icons(void) {
    char tmpl[] = "/tmp/ospi-icon-test-XXXXXX";
    char *root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root) return;

    uint8_t code0[24] = {0};
    uint8_t bndl[28];
    build_bndl(bndl, 7, 130, 7, 128);
    uint8_t fref_appl[7] = {'A', 'P', 'P', 'L', 0, 0, 0};
    uint8_t fref_text[7] = {'T', 'E', 'X', 'T', 0, 0, 0};
    uint8_t icn_sharp[256];
    uint8_t icl8[1024];
    for (size_t i = 0; i < sizeof icn_sharp; i++) icn_sharp[i] = (uint8_t)(i ^ 0x5au);
    for (size_t i = 0; i < sizeof icl8; i++) icl8[i] = (uint8_t)((i * 3u) & 0xffu);

    char rsrc_path[OSPI_PATH], link_path[OSPI_PATH];
    snprintf(rsrc_path, sizeof rsrc_path, "%s/BundledApp.rsrc", root);

    /* 1. Complete BNDL + FREF('APPL') + ICN# + icl8 extracts icon and mask. */
    const SynthRes full_bundle[] = {
        {"CODE", 0, code0, sizeof code0},
        {"BNDL", 128, bndl, sizeof bndl},
        {"FREF", 130, fref_appl, sizeof fref_appl},
        {"ICN#", 128, icn_sharp, sizeof icn_sharp},
        {"icl8", 128, icl8, sizeof icl8},
    };
    CHECK(write_fork_file(rsrc_path, full_bundle, 5) == 0);
    Item item = {0};
    CHECK(mac_icon_application(root, "BundledApp", &item) == 1);
    CHECK(item.has_guest_icon == 1);
    CHECK(memcmp(item.guest_icon, icl8, sizeof icl8) == 0);
    CHECK(memcmp(item.guest_icon_mask, icn_sharp + 128, 128) == 0);

    /* 2. CODE(0) without BNDL still classifies as application without custom icon. */
    const SynthRes code_only[] = {{"CODE", 0, code0, sizeof code0}};
    CHECK(write_fork_file(rsrc_path, code_only, 1) == 0);
    memset(&item, 0, sizeof item);
    CHECK(mac_icon_application(root, "BundledApp", &item) == 1);
    CHECK(item.has_guest_icon == 0);

    /* 3. Non-APPL FREF does not bind document icon as application icon. */
    const SynthRes text_fref[] = {
        {"CODE", 0, code0, sizeof code0},
        {"BNDL", 128, bndl, sizeof bndl},
        {"FREF", 130, fref_text, sizeof fref_text},
        {"ICN#", 128, icn_sharp, sizeof icn_sharp},
        {"icl8", 128, icl8, sizeof icl8},
    };
    CHECK(write_fork_file(rsrc_path, text_fref, 5) == 0);
    memset(&item, 0, sizeof item);
    CHECK(mac_icon_application(root, "BundledApp", &item) == 1);
    CHECK(item.has_guest_icon == 0);

    /* 4. Truncated icl8 (< 1024 bytes) fails icon load cleanly without crashing. */
    const SynthRes short_icl8[] = {
        {"CODE", 0, code0, sizeof code0},
        {"BNDL", 128, bndl, sizeof bndl},
        {"FREF", 130, fref_appl, sizeof fref_appl},
        {"ICN#", 128, icn_sharp, sizeof icn_sharp},
        {"icl8", 128, icl8, 512},
    };
    CHECK(write_fork_file(rsrc_path, short_icl8, 5) == 0);
    memset(&item, 0, sizeof item);
    CHECK(mac_icon_application(root, "BundledApp", &item) == 1);
    CHECK(item.has_guest_icon == 0);

    /* 5. Fork without CODE(0) is not an application. */
    const SynthRes no_code0[] = {{"CODE", 1, code0, sizeof code0}};
    CHECK(write_fork_file(rsrc_path, no_code0, 1) == 0);
    memset(&item, 0, sizeof item);
    CHECK(mac_icon_application(root, "BundledApp", &item) == 0);

    /* 6. Symlink .rsrc sidecar and path traversal are rejected. */
    CHECK(write_fork_file(rsrc_path, full_bundle, 5) == 0);
    snprintf(link_path, sizeof link_path, "%s/SymlinkApp.rsrc", root);
    CHECK(symlink(rsrc_path, link_path) == 0);
    CHECK(mac_icon_application(root, "SymlinkApp", &item) == 0);
    CHECK(mac_icon_application(root, "../BundledApp", &item) == 0);

    CHECK(unlink(link_path) == 0);
    /* Directory components and special-file sidecars cannot redirect a listing. */
    snprintf(link_path, sizeof link_path, "%s/Linked", root);
    CHECK(symlink(root, link_path) == 0);
    CHECK(mac_icon_application(root, "Linked/BundledApp", &item) == 0);
    Item listed[4] = {0};
    CHECK(volume_list(root, "Linked", listed, 4) == -1);
    CHECK(volume_list(root, "../", listed, 4) == -1);
    CHECK(volume_list(root, "", NULL, 4) == -1);
    CHECK(volume_list(root, "", listed, -1) == -1);
    CHECK(unlink(link_path) == 0);
    snprintf(link_path, sizeof link_path, "%s/Pipe.rsrc", root);
    CHECK(mkfifo(link_path, 0600) == 0);
    CHECK(mac_icon_application(root, "Pipe", &item) == 0);
    CHECK(unlink(link_path) == 0);

    /* A retained directory descriptor still reads its original fork after rename. */
    int directory = volume_directory_open(root, ""); CHECK(directory >= 0);
    char moved[OSPI_PATH]; snprintf(moved, sizeof moved, "%s-moved", root);
    CHECK(rename(root, moved) == 0);
    CHECK(symlink(moved, root) == 0);
    CHECK(mac_icon_application(root, "BundledApp", &item) == 0);
    CHECK(volume_list(root, "", listed, 4) == -1);
    memset(&item, 0, sizeof item);
    CHECK(mac_icon_application_at(directory, "BundledApp", &item) == 1);
    CHECK(item.has_guest_icon && !memcmp(item.guest_icon, icl8, sizeof icl8));
    CHECK(mac_icon_application_at(directory, "../BundledApp", &item) == 0);
    if (directory >= 0) close(directory);
    CHECK(unlink(root) == 0);
    CHECK(rename(moved, root) == 0);
    CHECK(unlink(rsrc_path) == 0);
    CHECK(rmdir(root) == 0);
}

int main(void) {
    CHECK_COLOR(0,   0xffffffffu);
    CHECK_COLOR(1,   0xffffffccu);
    CHECK_COLOR(6,   0xffffccffu);
    CHECK_COLOR(215, 0xff000000u);
    CHECK_COLOR(216, 0xffee0000u);
    CHECK_COLOR(225, 0xff110000u);
    CHECK_COLOR(226, 0xff00ee00u);
    CHECK_COLOR(236, 0xff0000eeu);
    CHECK_COLOR(246, 0xffeeeeeeu);
    CHECK_COLOR(255, 0xff111111u);
    test_bundle_icons();
    if (fails) {
        fprintf(stderr, "Macintosh icon tests: %d failed\n", fails);
        return 1;
    }
    puts("Macintosh icon palette and bundle tests passed");
    return 0;
}
