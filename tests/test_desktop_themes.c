#include "ospi_icon_hd.h"
#include "archive_fs.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_hidpi_icon_packs(void) {
    static const struct {
        const char *path;
        int expected_size;
    } packs[2] = {
        { "assets/icons/ospi-icons@2x.rgba", 64 },
        { "assets/icons/ospi-icons@4x.rgba", 128 }
    };
    for (int p = 0; p < 2; p++) {
        OspiHdIconSet set = {0};
        CHECK(ospi_hd_icon_set_load(&set, packs[p].path) == 1);
        CHECK(set.size == packs[p].expected_size);
        CHECK(set.count >= 14);

        for (size_t i = 0; i < set.count; i++) {
            CHECK(set.icons[i].name[0] != '\0');
            CHECK(set.icons[i].rgba != NULL);
            CHECK(ospi_hd_icon_find(&set, set.icons[i].name) == set.icons[i].rgba);
        }
        CHECK(ospi_hd_icon_find(&set, "nonexistent-icon") == NULL);

        const uint8_t *rgba = set.icons[0].rgba;
        int opaque_pixels = 0;
        for (int y = 0; y < set.size; y++) {
            for (int x = 0; x < set.size; x++) {
                uint32_t argb = ospi_hd_icon_argb(rgba, set.size, x, y);
                if ((argb >> 24) > 0) opaque_pixels++;
            }
        }
        CHECK(opaque_pixels > 64);
        ospi_hd_icon_set_free(&set);
    }

    /* Missing or corrupt OSPIHD01 file must fail closed (returns 0). */
    OspiHdIconSet bad = {0};
    CHECK(ospi_hd_icon_set_load(&bad, "/tmp/os999-nonexistent-hd.rgba") == 0);
}

static void test_classic_cursors_suite(void) {
    static const char *const cursor_files[7] = {
        "assets/icons/cursors/arrow.curs",
        "assets/icons/cursors/ibeam.curs",
        "assets/icons/cursors/crosshair.curs",
        "assets/icons/cursors/plus.curs",
        "assets/icons/cursors/wait.curs",
        "assets/icons/cursors/hand.curs",
        "assets/icons/cursors/grab.curs"
    };
    for (int i = 0; i < 7; i++) {
        struct stat st;
        CHECK(stat(cursor_files[i], &st) == 0 && st.st_size == 68);
        FILE *f = fopen(cursor_files[i], "rb");
        CHECK(f != NULL);
        if (!f) continue;
        uint8_t buf[68] = {0};
        CHECK(fread(buf, 1, 68, f) == 68);
        fclose(f);

        uint16_t hot_y = (uint16_t)(((uint16_t)buf[64] << 8) | buf[65]);
        uint16_t hot_x = (uint16_t)(((uint16_t)buf[66] << 8) | buf[67]);
        CHECK(hot_x < 16 && hot_y < 16);

        int nonzero_bits = 0;
        for (int b = 0; b < 64; b++) {
            if (buf[b] != 0) nonzero_bits++;
        }
        CHECK(nonzero_bits > 0);

        /* Integer @2x / @4x hotspot scaling stays bounded within scaled cursor edge. */
        CHECK(hot_x * 2u < 32u && hot_y * 2u < 32u);
        CHECK(hot_x * 4u < 64u && hot_y * 4u < 64u);
    }
}

typedef struct {
    int id;
    const char *name;
    uint32_t menu_bg;
    uint32_t menu_fg;
    uint32_t win_frame;
    uint32_t accent;
} Os999ThemeDef;

static const Os999ThemeDef kThemes[16] = {
    {  0, "Platinum Classic",   0xFFDDDDDDu, 0xFF000000u, 0xFFCCCCCCu, 0xFF333399u },
    {  1, "Bondi Blue",         0xFFD8ECF0u, 0xFF082228u, 0xFFB8DCE4u, 0xFF007B8Au },
    {  2, "Graphite Pro",       0xFFD0D4D8u, 0xFF101418u, 0xFFAEB4BCu, 0xFF4A5A6Au },
    {  3, "Tangerine Citrus",   0xFFF8E6D4u, 0xFF281404u, 0xFFF0CEB0u, 0xFFE06810u },
    {  4, "Strawberry Pop",     0xFFF8DCE4u, 0xFF280812u, 0xFFF0BCCE,  0xFFC82858u },
    {  5, "Lime Studio",        0xFFE4F4D8u, 0xFF102408u, 0xFFC8E8B4u, 0xFF489818u },
    {  6, "Grape Sherbet",      0xFFE8DCF4u, 0xFF1A0C28u, 0xFFD2BCECu, 0xFF6A329Fu },
    {  7, "Blueberry DV",       0xFFDCE8F8u, 0xFF0A1828u, 0xFFB8D0F0u, 0xFF1E5CB8u },
    {  8, "Sunny Yellow",       0xFFFBF4D4u, 0xFF241E04u, 0xFFF4E4A8u, 0xFFB88E08u },
    {  9, "Dark Obsidian",      0xFF22262Cu, 0xFFF0F4F8u, 0xFF181C20u, 0xFF4C8DFFu },
    { 10, "Warm Parchment",     0xFFF4ECD8u, 0xFF221A10u, 0xFFE4D6B8u, 0xFF8C5828u },
    { 11, "After Dark Neon",    0xFF161A28u, 0xFFE8F0FFu, 0xFF101420u, 0xFF00D4A8u },
    { 12, "Sage Meadow",        0xFFDCE8DCu, 0xFF101E12u, 0xFFC0D4C0u, 0xFF3A7044u },
    { 13, "Crimson Velvet",     0xFFF2D8DCu, 0xFF26080Cu, 0xFFE2B4BCu, 0xFF9E1B2Eu },
    { 14, "Cobalt CAD",         0xFFD4E0F4u, 0xFF081428u, 0xFFB4C8ECu, 0xFF1446A0u },
    { 15, "High Contrast Mono", 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu, 0xFF000000u }
};

static int theme_save(const char *dir, int theme_id) {
    if (!dir || theme_id < 0 || theme_id >= 16) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/.os999-theme", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "%d\n", theme_id);
    fclose(f);
    return 0;
}

static int theme_load(const char *dir) {
    if (!dir) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/.os999-theme", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[32] = {0};
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || v < 0 || v >= 16) return 0;
    return (int)v;
}

static void test_theme_catalog_and_persistence(void) {
    for (int i = 0; i < 16; i++) {
        CHECK(kThemes[i].id == i);
        CHECK(kThemes[i].name != NULL && strlen(kThemes[i].name) > 0);
        CHECK((kThemes[i].menu_bg >> 24) == 0xFFu);
        CHECK((kThemes[i].menu_fg >> 24) == 0xFFu);
        CHECK(kThemes[i].menu_bg != kThemes[i].menu_fg);
    }

    char dir[] = "/tmp/os999-themes-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(theme_load(dir) == 0);
    CHECK(theme_save(dir, 9) == 0 && theme_load(dir) == 9);
    CHECK(theme_save(dir, -1) == -1 && theme_save(dir, 16) == -1);

    /* Corrupt preference file falls back to Default Platinum (0). */
    char pref[512];
    snprintf(pref, sizeof(pref), "%s/.os999-theme", dir);
    FILE *f = fopen(pref, "wb");
    CHECK(f != NULL);
    if (f) { fputs("999\n", f); fclose(f); }
    CHECK(theme_load(dir) == 0);

    CHECK(archive_remove_tree(dir) == 0);
}

int main(void) {
    test_hidpi_icon_packs();
    test_classic_cursors_suite();
    test_theme_catalog_and_persistence();

    if (failures > 0) {
        fprintf(stderr, "test_desktop_themes: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_desktop_themes: all %d checks passed\n", checks);
    return 0;
}
