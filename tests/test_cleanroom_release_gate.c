#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    const char *id;
    const char *title;
    const char *garden_url;
    const char *sha256;
} GameCatalogEntry;

typedef struct {
    int cleanroom_provenance_passed;
    int fuzz_campaign_passed;
    int platform_matrix_passed;
    int sandbox_audit_passed;
    int packaging_reproducible;
    int tier1_games_passing;
    int browser_youtube_hw_passed;
    int p0_p1_open_defects;
} ReleaseGateSummary;

enum {
    CATALOG_OK = 0,
    CATALOG_ERR_DUPLICATE_URL = -1,
    CATALOG_ERR_DUPLICATE_HASH = -2,
    CATALOG_ERR_INVALID_ENTRY = -3
};

static int file_nonempty(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static int buffer_contains_forbidden_string(const uint8_t *buf, size_t n) {
    static const char *const forbidden[] = {
        "Apple Computer, Inc.",
        "OldWorld ROM",
        "NewWorld ROM"
    };
    for (size_t f = 0; f < sizeof(forbidden) / sizeof(forbidden[0]); f++) {
        size_t pat_len = strlen(forbidden[f]);
        if (n >= pat_len) {
            for (size_t i = 0; i + pat_len <= n; i++) {
                if (memcmp(buf + i, forbidden[f], pat_len) == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int scan_cleanroom_dir(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return -1;
    struct dirent *ent;
    int clean = 1;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        size_t nlen = strlen(ent->d_name);
        if ((nlen > 4 && strcmp(ent->d_name + nlen - 4, ".rom") == 0) ||
            strcmp(ent->d_name, "MacROM") == 0 ||
            strcmp(ent->d_name, "System") == 0) {
            clean = 0;
            break;
        }
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(sub, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (scan_cleanroom_dir(sub) != 1) {
                clean = 0;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            FILE *f = fopen(sub, "rb");
            if (!f) continue;
            uint8_t probe[4096];
            size_t rd = fread(probe, 1, sizeof(probe), f);
            fclose(f);
            if (buffer_contains_forbidden_string(probe, rd)) {
                clean = 0;
                break;
            }
        }
    }
    closedir(d);
    return clean;
}

static int audit_game_catalog(const GameCatalogEntry *entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].id || !entries[i].title ||
            !entries[i].garden_url || !entries[i].sha256 ||
            strncmp(entries[i].garden_url, "https://macintoshgarden.org/", 28) != 0 ||
            strlen(entries[i].sha256) != 64u) {
            return CATALOG_ERR_INVALID_ENTRY;
        }
        for (size_t j = i + 1; j < count; j++) {
            if (strcmp(entries[i].garden_url, entries[j].garden_url) == 0) {
                return CATALOG_ERR_DUPLICATE_URL;
            }
            if (strcmp(entries[i].sha256, entries[j].sha256) == 0) {
                return CATALOG_ERR_DUPLICATE_HASH;
            }
        }
    }
    return CATALOG_OK;
}

static int evaluate_master_release_gate(const ReleaseGateSummary *s) {
    if (!s) return 0;
    return s->cleanroom_provenance_passed == 1 &&
           s->fuzz_campaign_passed == 1 &&
           s->platform_matrix_passed == 1 &&
           s->sandbox_audit_passed == 1 &&
           s->packaging_reproducible == 1 &&
           s->tier1_games_passing >= 10 &&
           s->browser_youtube_hw_passed == 1 &&
           s->p0_p1_open_defects == 0;
}

static void test_cleanroom_assets_and_provenance(void) {
    CHECK(file_nonempty("fonts/ChicagoFLF.ttf"));
    CHECK(file_nonempty("fonts/LICENSE.txt"));
    CHECK(file_nonempty("macos/Info.plist"));
    CHECK(file_nonempty("assets/icons/ospi-icons.icl8"));
    CHECK(file_nonempty("assets/icons/ospi-icons@2x.rgba"));
    CHECK(file_nonempty("assets/icons/ospi-icons@4x.rgba"));
    CHECK(file_nonempty("assets/sounds/classic9/01-startup_chord.wav"));
    CHECK(file_nonempty("assets/sounds/classic9/03-plain_beep.wav"));
    CHECK(file_nonempty("assets/sounds/classic9/16-menu_open.wav"));
    CHECK(file_nonempty("assets/sounds/classic9/17-menu_select.wav"));

    CHECK(scan_cleanroom_dir("assets") == 1);
    CHECK(scan_cleanroom_dir("fonts") == 1);

    const char *bad_rom = "Header Copyright Apple Computer, Inc. 1991";
    CHECK(buffer_contains_forbidden_string((const uint8_t *)bad_rom, strlen(bad_rom)) == 1);
}

static void test_catalog_c04_duplicate_detection(void) {
    /* Defective C04 catalog where MacSurf and Castle of Dr. Brain share a URL. */
    const GameCatalogEntry defective[] = {
        {
            "G01", "Prince of Persia",
            "https://macintoshgarden.org/games/prince-of-persia",
            "1111111111111111111111111111111111111111111111111111111111111111"
        },
        {
            "G11", "Castle of Dr. Brain",
            "https://macintoshgarden.org/games/castle-of-dr-brain",
            "2222222222222222222222222222222222222222222222222222222222222222"
        },
        {
            "G12", "MacSurf",
            "https://macintoshgarden.org/games/castle-of-dr-brain",
            "3333333333333333333333333333333333333333333333333333333333333333"
        }
    };
    CHECK(audit_game_catalog(defective, 3u) == CATALOG_ERR_DUPLICATE_URL);

    /* Corrected catalog with unique Macintosh Garden URLs and SHA-256 digests. */
    GameCatalogEntry corrected[3];
    memcpy(corrected, defective, sizeof(defective));
    corrected[2].garden_url = "https://macintoshgarden.org/games/macsurf";
    CHECK(audit_game_catalog(corrected, 3u) == CATALOG_OK);
}

static void test_master_release_readiness_gate(void) {
    ReleaseGateSummary ready = {1, 1, 1, 1, 1, 12, 1, 0};
    CHECK(evaluate_master_release_gate(&ready) == 1);

    ReleaseGateSummary open_p1 = ready;
    open_p1.p0_p1_open_defects = 1;
    CHECK(evaluate_master_release_gate(&open_p1) == 0);

    ReleaseGateSummary low_games = ready;
    low_games.tier1_games_passing = 9;
    CHECK(evaluate_master_release_gate(&low_games) == 0);

    ReleaseGateSummary no_hw_yt = ready;
    no_hw_yt.browser_youtube_hw_passed = 0;
    CHECK(evaluate_master_release_gate(&no_hw_yt) == 0);
}

int main(void) {
    test_cleanroom_assets_and_provenance();
    test_catalog_c04_duplicate_detection();
    test_master_release_readiness_gate();
    if (failures > 0) {
        fprintf(stderr, "test_cleanroom_release_gate: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_cleanroom_release_gate: all %d checks passed\n", checks);
    return 0;
}
