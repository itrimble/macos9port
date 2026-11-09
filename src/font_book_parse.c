#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "font_book.h"
#include "mac_resource.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

enum { FONT_FILE_LIMIT = 64 * 1024 * 1024 };

static void message(char *out, size_t size, const char *text) {
    if (out && size) snprintf(out, size, "%s", text ? text : "");
}

static const char *extension(const char *path) {
    const char *leaf = strrchr(path ? path : "", '/');
    const char *dot = strrchr(leaf ? leaf + 1 : path ? path : "", '.');
    return dot ? dot : "";
}

static FontBookFormat format_from_name(const char *path) {
    const char *ext = extension(path);
    if (!strcasecmp(ext, ".ttf")) return FONT_BOOK_FORMAT_TRUETYPE;
    if (!strcasecmp(ext, ".otf")) return FONT_BOOK_FORMAT_OPENTYPE;
    if (!strcasecmp(ext, ".ttc") || !strcasecmp(ext, ".otc")) return FONT_BOOK_FORMAT_COLLECTION;
    if (!strcasecmp(ext, ".pfa") || !strcasecmp(ext, ".pfb")) return FONT_BOOK_FORMAT_TYPE1;
    if (!strcasecmp(ext, ".dfont") || !strcasecmp(ext, ".rsrc") ||
        !strcasecmp(ext, ".suit") || !strcasecmp(ext, ".suitcase"))
        return FONT_BOOK_FORMAT_RESOURCE;
    return FONT_BOOK_FORMAT_UNKNOWN;
}

static void fallback_name(FontBookEntry *entry, const char *path) {
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    snprintf(entry->family, sizeof entry->family, "%s", leaf);
    char *dot = strrchr(entry->family, '.');
    if (dot) *dot = 0;
    snprintf(entry->style, sizeof entry->style, "Regular");
}

static void mac_name(char *out, size_t size, const MacResource *resource) {
    size_t used = 0;
    if (!out || !size) return;
    if (resource && resource->name) for (size_t i = 0; i < resource->name_size && used + 1 < size; i++) {
        unsigned char c = resource->name[i];
        out[used++] = c >= 32 && c < 127 ? (char)c : '?';
    }
    out[used] = 0;
}

static int read_regular(const char *path, uint8_t **bytes, size_t *size,
                        char *error, size_t error_size) {
    struct stat st;
    if (!path || lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        message(error, error_size, "Font must be a regular file, not a link.");
        return -1;
    }
    if (st.st_size < 16 || st.st_size > FONT_FILE_LIMIT) {
        message(error, error_size, "Font file size is outside the 16 byte to 64 MB safety limit.");
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { message(error, error_size, "Font file could not be opened safely."); return -1; }
    *size = (size_t)st.st_size;
    *bytes = malloc(*size);
    if (!*bytes) { close(fd); message(error, error_size, "Not enough memory to inspect the font."); return -1; }
    size_t used = 0;
    while (used < *size) {
        ssize_t got = read(fd, *bytes + used, *size - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        used += (size_t)got;
    }
    int ok = used == *size && close(fd) == 0;
    if (!ok) { free(*bytes); *bytes = NULL; message(error, error_size, "Font file could not be read completely."); return -1; }
    return 0;
}

#ifdef OSPI_FREETYPE
static int face_metadata(FT_Face face, FontBookEntry *entry) {
    if (!face) return 0;
    if (face->family_name && face->family_name[0])
        snprintf(entry->family, sizeof entry->family, "%s", face->family_name);
    if (face->style_name && face->style_name[0])
        snprintf(entry->style, sizeof entry->style, "%s", face->style_name);
    entry->face_count = face->num_faces > 0 ? (int)face->num_faces : 1;
    entry->previewable = FT_Set_Pixel_Sizes(face, 0, 18) == 0;
    return 1;
}

static int freetype_path(const char *path, int face_index, FontBookEntry *entry) {
    FT_Library library = NULL;
    FT_Face face = NULL;
    int ok = FT_Init_FreeType(&library) == 0 && FT_New_Face(library, path, face_index, &face) == 0;
    if (ok) face_metadata(face, entry);
    if (face) FT_Done_Face(face);
    if (library) FT_Done_FreeType(library);
    return ok;
}

static int freetype_memory(const uint8_t *bytes, size_t size, FontBookEntry *entry) {
    FT_Library library = NULL;
    FT_Face face = NULL;
    int ok = size <= (size_t)LONG_MAX && FT_Init_FreeType(&library) == 0 &&
             FT_New_Memory_Face(library, bytes, (FT_Long)size, 0, &face) == 0;
    if (ok) face_metadata(face, entry);
    if (face) FT_Done_Face(face);
    if (library) FT_Done_FreeType(library);
    return ok;
}
#else
static int freetype_path(const char *path, int face_index, FontBookEntry *entry) {
    (void)path; (void)face_index; (void)entry; return 0;
}
static int freetype_memory(const uint8_t *bytes, size_t size, FontBookEntry *entry) {
    (void)bytes; (void)size; (void)entry; return 0;
}
#endif

static int resource_metadata(const uint8_t *bytes, size_t size, FontBookEntry *entry,
                             char *error, size_t error_size) {
    MacResourceFork fork;
    char resource_error[128] = {0};
    if (mac_resource_open_container(&fork, bytes, size, resource_error, sizeof resource_error) != 0) {
        message(error, error_size, resource_error[0] ? resource_error : "Not a valid font resource container.");
        return 0;
    }
    int font_resources = 0, outlines = 0, bitmaps = 0;
    for (size_t i = 0; i < mac_resource_count(&fork); i++) {
        MacResource resource;
        if (mac_resource_at(&fork, i, &resource) != 0) continue;
        int sfnt = resource.type == MAC_RESOURCE_TYPE('s','f','n','t');
        int legacy = resource.type == MAC_RESOURCE_TYPE('F','O','N','D') ||
                     resource.type == MAC_RESOURCE_TYPE('F','O','N','T') ||
                     resource.type == MAC_RESOURCE_TYPE('N','F','N','T');
        if (!sfnt && !legacy) continue;
        font_resources++;
        outlines += sfnt;
        bitmaps += resource.type == MAC_RESOURCE_TYPE('F','O','N','T') ||
                   resource.type == MAC_RESOURCE_TYPE('N','F','N','T');
        if (!entry->family[0]) mac_name(entry->family, sizeof entry->family, &resource);
        if (sfnt && !entry->previewable) freetype_memory(resource.data, resource.size, entry);
    }
    if (!outlines && !bitmaps) {
        message(error, error_size, font_resources ? "Font family resource contains no outline or bitmap glyph resource." :
                "Resource fork contains no sfnt, FOND, FONT, or NFNT resources.");
        return 0;
    }
    entry->format = FONT_BOOK_FORMAT_RESOURCE;
    entry->legacy_resources = font_resources;
    if (!entry->style[0]) snprintf(entry->style, sizeof entry->style,
                                   entry->previewable ? "Resource outline" : "Legacy bitmap");
    if (!entry->face_count) entry->face_count = 1;
    return 1;
}

int font_book_validate(const char *path, FontBookEntry *entry,
                       char *error, size_t error_size) {
    FontBookEntry result = {0};
    uint8_t *bytes = NULL;
    size_t size = 0;
    if (error && error_size) error[0] = 0;
    if (!entry || read_regular(path, &bytes, &size, error, error_size) != 0) return -1;
    snprintf(result.path, sizeof result.path, "%s", path);
    result.format = format_from_name(path);
    result.face_index = 0;
    fallback_name(&result, path);
    int valid = freetype_path(path, 0, &result);
    if (!valid || result.format == FONT_BOOK_FORMAT_RESOURCE ||
        result.format == FONT_BOOK_FORMAT_UNKNOWN) {
        char resource_error[FONT_BOOK_ERROR] = {0};
        int resource_valid = resource_metadata(bytes, size, &result,
                                               resource_error, sizeof resource_error);
        if (resource_valid) valid = 1;
        else if (!valid) message(error, error_size, resource_error);
    }
    free(bytes);
    if (!valid) {
        if (!error || !error[0]) message(error, error_size, "FreeType rejected the font and no valid legacy font resources were found.");
        result.valid = 0;
        snprintf(result.error, sizeof result.error, "%s", error && error[0] ? error : "Invalid font.");
        *entry = result;
        return -1;
    }
    result.valid = 1;
    if (result.format == FONT_BOOK_FORMAT_UNKNOWN)
        result.format = FONT_BOOK_FORMAT_RESOURCE;
    *entry = result;
    return 0;
}

int font_book_validate_face(const char *path, int face_index, FontBookEntry *entry) {
    struct stat st;
    if (!path || !entry || face_index < 0 || lstat(path, &st) != 0 ||
        !S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_size < 16 ||
        st.st_size > FONT_FILE_LIMIT) return -1;
    FontBookEntry result = {0};
    snprintf(result.path, sizeof result.path, "%s", path);
    result.format = format_from_name(path);
    result.face_index = face_index;
    fallback_name(&result, path);
    if (!freetype_path(path, face_index, &result)) return -1;
    result.valid = 1;
    *entry = result;
    return 0;
}

const char *font_book_format_name(FontBookFormat format) {
    switch (format) {
    case FONT_BOOK_FORMAT_TRUETYPE: return "TrueType";
    case FONT_BOOK_FORMAT_OPENTYPE: return "OpenType";
    case FONT_BOOK_FORMAT_COLLECTION: return "Font Collection";
    case FONT_BOOK_FORMAT_TYPE1: return "PostScript Type 1";
    case FONT_BOOK_FORMAT_RESOURCE: return "Suitcase / Resource";
    default: return "Unknown";
    }
}
