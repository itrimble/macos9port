#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "font_book.h"
#include "font_book_window.h"
#include "ospi.h"
#include "text_system.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; \
} } while (0)

static void be16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)(value >> 8); p[1] = (unsigned char)value;
}

static void be32(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)(value >> 24); p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8); p[3] = (unsigned char)value;
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) || (*size = (size_t)ftell(file)) == 0 ||
        fseek(file, 0, SEEK_SET)) { if (file) fclose(file); return NULL; }
    unsigned char *bytes = malloc(*size);
    if (!bytes || fread(bytes, 1, *size, file) != *size) { free(bytes); bytes = NULL; }
    fclose(file);
    return bytes;
}

static int write_file(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    int ok = fwrite(bytes, 1, size, file) == size && fclose(file) == 0;
    return ok ? 0 : -1;
}

static int make_suitcase(const char *path, const char *font_path) {
    size_t font_size = 0;
    unsigned char *font = read_file(font_path, &font_size);
    if (!font || font_size > 0xffffffu) { free(font); return -1; }
    size_t data_offset = 256, data_size = 4 + font_size;
    size_t map_offset = data_offset + data_size, map_size = 50;
    unsigned char *bytes = calloc(1, map_offset + map_size);
    if (!bytes) { free(font); return -1; }
    be32(bytes, (unsigned)data_offset); be32(bytes + 4, (unsigned)map_offset);
    be32(bytes + 8, (unsigned)data_size); be32(bytes + 12, (unsigned)map_size);
    be32(bytes + data_offset, (unsigned)font_size);
    memcpy(bytes + data_offset + 4, font, font_size);
    unsigned char *map = bytes + map_offset;
    be16(map + 24, 28); be16(map + 26, 50);
    unsigned char *types = map + 28;
    be16(types, 0); memcpy(types + 2, "sfnt", 4); be16(types + 6, 0); be16(types + 8, 10);
    be16(types + 10, 128); be16(types + 12, 0xffff);
    int result = write_file(path, bytes, map_offset + map_size);
    free(bytes); free(font);
    return result;
}

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char child[OSPI_PATH * 2];
            snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
            remove_tree(child);
        }
        closedir(dir);
    }
    rmdir(path);
}

static int find_font(const FontBookCatalog *catalog, const char *family, FontBookSource source) {
    for (int i = 0; i < catalog->count; i++)
        if (catalog->fonts[i].source == source && strstr(catalog->fonts[i].family, family)) return i;
    return -1;
}

static void test_validation_and_resources(const char *root) {
    FontBookEntry entry;
    char error[FONT_BOOK_ERROR];
    CHECK(font_book_validate("fonts/ChicagoFLF.ttf", &entry, error, sizeof error) == 0);
    CHECK(entry.valid && entry.previewable && entry.face_count >= 1);
    CHECK(strstr(entry.family, "Chicago") != NULL);
    CHECK(entry.format == FONT_BOOK_FORMAT_TRUETYPE);

    char disguised_otf[OSPI_PATH];
    snprintf(disguised_otf, sizeof disguised_otf, "%s/Chicago Fixture.otf", root);
    size_t chicago_size = 0;
    unsigned char *chicago = read_file("fonts/ChicagoFLF.ttf", &chicago_size);
    CHECK(chicago != NULL && write_file(disguised_otf, chicago, chicago_size) == 0);
    free(chicago);
    CHECK(font_book_validate(disguised_otf, &entry, error, sizeof error) == 0);
    CHECK(entry.format == FONT_BOOK_FORMAT_OPENTYPE && strstr(entry.family, "Chicago"));

    char suitcase[OSPI_PATH];
    snprintf(suitcase, sizeof suitcase, "%s/Fixture Suitcase", root);
    CHECK(make_suitcase(suitcase, "fonts/ChicagoFLF.ttf") == 0);
    CHECK(font_book_validate(suitcase, &entry, error, sizeof error) == 0);
    CHECK(entry.format == FONT_BOOK_FORMAT_RESOURCE && entry.legacy_resources == 1);
    CHECK(entry.previewable && entry.face_count >= 1);

    char invalid[OSPI_PATH], link[OSPI_PATH];
    snprintf(invalid, sizeof invalid, "%s/Broken.otf", root);
    snprintf(link, sizeof link, "%s/Linked.ttf", root);
    CHECK(write_file(invalid, "not a font at all", 17) == 0);
    CHECK(font_book_validate(invalid, &entry, error, sizeof error) == -1);
    CHECK(error[0] != 0 && !entry.valid);
    CHECK(symlink("Broken.otf", link) == 0);
    CHECK(font_book_validate(link, &entry, error, sizeof error) == -1);
}

static void test_catalog_activation_install(const char *root) {
    FontBookCatalog catalog;
    CHECK(font_book_catalog_init(&catalog, root, "fonts", NULL) > 0);
    int bundled = find_font(&catalog, "Chicago", FONT_BOOK_SOURCE_BUNDLED);
    CHECK(bundled >= 0 && catalog.fonts[bundled].valid && catalog.fonts[bundled].active);
    int cjk_faces = 0;
    for (int i = 0; i < catalog.count; i++)
        if (strstr(catalog.fonts[i].path, "NotoSansCJK-Regular.ttc")) cjk_faces++;
    CHECK(cjk_faces == 10);

    char installed[OSPI_PATH], error[FONT_BOOK_ERROR];
    CHECK(font_book_install(&catalog, "fonts/ChicagoFLF.ttf", installed, sizeof installed,
                            error, sizeof error) == 0);
    CHECK(strstr(installed, "/System Folder/Fonts/ChicagoFLF.ttf") != NULL);
    int system = find_font(&catalog, "Chicago", FONT_BOOK_SOURCE_SYSTEM);
    CHECK(system >= 0 && catalog.fonts[system].active);
    unsigned generation = catalog.generation;
    CHECK(font_book_set_active(&catalog, system, 0, error, sizeof error) == 0);
    CHECK(!catalog.fonts[system].active && catalog.generation == generation + 1);

    FontBookCatalog reopened;
    CHECK(font_book_catalog_init(&reopened, root, "fonts", NULL) > 0);
    system = find_font(&reopened, "Chicago", FONT_BOOK_SOURCE_SYSTEM);
    CHECK(system >= 0 && !reopened.fonts[system].active);
    CHECK(font_book_set_active(&reopened, system, 1, error, sizeof error) == 0);
    CHECK(reopened.fonts[system].active);
    CHECK(font_book_install(&reopened, "fonts/ChicagoFLF.ttf", installed, sizeof installed,
                            error, sizeof error) == -1);

    char invalid[OSPI_PATH], expected[OSPI_PATH];
    snprintf(invalid, sizeof invalid, "%s/Bad Font.ttf", root);
    snprintf(expected, sizeof expected, "%s/System Folder/Fonts/Bad Font.ttf", root);
    CHECK(write_file(invalid, "bad font fixture!", 17) == 0);
    CHECK(font_book_install(&reopened, invalid, installed, sizeof installed, error, sizeof error) == -1);
    CHECK(access(expected, F_OK) != 0);

    char registry[OSPI_PATH], registry_saved[OSPI_PATH], target[OSPI_PATH];
    snprintf(registry, sizeof registry, "%s/System Folder/Preferences/Font Book 999 Registry", root);
    snprintf(registry_saved, sizeof registry_saved, "%s.saved", registry);
    snprintf(target, sizeof target, "%s/registry-target", root);
    CHECK(rename(registry, registry_saved) == 0);
    CHECK(write_file(target, "outside", 7) == 0);
    CHECK(symlink(target, registry) == 0);
    system = find_font(&reopened, "Chicago", FONT_BOOK_SOURCE_SYSTEM);
    CHECK(font_book_set_active(&reopened, system, 0, error, sizeof error) == -1);
    CHECK(reopened.fonts[system].active);
    CHECK(unlink(registry) == 0 && rename(registry_saved, registry) == 0);

    char empty[OSPI_PATH];
    snprintf(empty, sizeof empty, "%s/empty", root);
    CHECK(mkdir(empty, 0755) == 0);
    FontBookCatalog one;
    CHECK(font_book_catalog_init(&one, root, empty, NULL) > 0);
    system = find_font(&one, "Chicago", FONT_BOOK_SOURCE_SYSTEM);
    CHECK(system >= 0);
    CHECK(font_book_set_active(&one, system, 0, error, sizeof error) == -1);
    CHECK(strstr(error, "one usable") != NULL);
}

static void test_session_window(const char *root) {
    CHECK(volume_seed(root) == 0);
    CHECK(volume_builtin_font_book(root, "Applications/Utilities/Font Book 999") == 1);
    Session *session = session_create(root);
    CHECK(session != NULL && font_book_session_catalog(session) != NULL);
    if (!session) return;
    int before = session->nwin;
    CHECK(font_book_window_open(session) == 0);
    CHECK(session->nwin == before + 1 && session->win[session->nwin - 1].kind == WIN_FONT_BOOK);
    CHECK(!strcmp(session->win[session->nwin - 1].title, "Font Book 999"));
    uint32_t *pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        session_paint(session, pixels, OSPI_W, OSPI_H);
        unsigned nonzero = 0;
        for (int i = 0; i < OSPI_W * OSPI_H; i++) nonzero += pixels[i] != 0;
        CHECK(nonzero > 100000);
        unsigned capabilities = text_system_capabilities();
        if ((capabilities & 7u) == 7u) {
            for (int i = 0; i < OSPI_W * OSPI_H; i++) pixels[i] = C_WHITE;
            const char arabic[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
            Fb fb; fb_init(&fb, pixels, OSPI_W, OSPI_H);
            CHECK(text_system_available());
            CHECK(text_system_width(arabic, sizeof arabic - 1) > 0);
            CHECK(text_system_draw(&fb, 20, 20, C_BLACK, arabic, sizeof arabic - 1) == 1);
        } else CHECK(capabilities & 8u);
        free(pixels);
    }
    CHECK(font_book_window_key(session, &session->win[session->nwin - 1], ' ', 0) == 1);
    session_command(session, CMD_CLOSE);
    CHECK(session->nwin == before);
    session_destroy(session);
}

int main(void) {
    char root[] = "/tmp/os999-fontbook-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    test_validation_and_resources(root);
    test_catalog_activation_install(root);
    test_session_window(root);
    remove_tree(root);
    printf("fontbook: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
