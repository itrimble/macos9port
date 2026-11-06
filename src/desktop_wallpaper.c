#include "desktop_wallpaper.h"
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "desktop_wallpaper_catalog.h"

enum { MAX_FILE_BYTES = 32 * 1024 * 1024, MAX_DIMENSION = 4096 };
static char resource_dir[4096], preference[4096];
static uint32_t *image, *scaled;
static int image_w, image_h, scaled_w, scaled_h, selected = -1;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

static void clear_picture(void) {
    free(image); free(scaled);
    image = scaled = NULL;
    image_w = image_h = scaled_w = scaled_h = 0;
}

static int decode_bmp(const char *path) {
    /* The resource pack uses regular uncompressed BMPs. Reject symlinks/devices. */
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 54 || st.st_size > MAX_FILE_BYTES) {
        close(fd); return 0;
    }
    size_t n = (size_t)st.st_size;
    uint8_t *b = malloc(n);
    FILE *f = fdopen(fd, "rb");
    if (!f) { free(b); close(fd); return 0; }
    int ok = b && fread(b, 1, n, f) == n;
    fclose(f);
    if (!ok) { free(b); return 0; }
    uint32_t offset = le32(b + 10), dib = le32(b + 14), bw = le32(b + 18), bh = le32(b + 22);
    unsigned bits = le16(b + 28);
    int top_down = !!(bh & UINT32_C(0x80000000));
    uint32_t height = top_down ? ~bh + 1u : bh;
    if (b[0] != 'B' || b[1] != 'M' || le32(b + 2) != n ||
        dib < 40 || dib > 124 || 14u + dib > n || offset < 14u + dib || offset > n ||
        !bw || bw > MAX_DIMENSION || !height || height > MAX_DIMENSION ||
        le16(b + 26) != 1 || (bits != 24 && bits != 32) || le32(b + 30) != 0) {
        free(b); return 0;
    }
    size_t stride = (((size_t)bw * bits + 31) / 32) * 4;
    if ((size_t)height > (n - offset) / stride) { free(b); return 0; }
    uint32_t *pixels = malloc((size_t)bw * height * sizeof *pixels);
    if (!pixels) { free(b); return 0; }
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = b + offset + (top_down ? y : height - 1 - y) * stride;
        for (uint32_t x = 0; x < bw; x++) {
            const uint8_t *p = row + x * (bits / 8);
            pixels[(size_t)y * bw + x] = 0xff000000u | (uint32_t)p[2] << 16 |
                                        (uint32_t)p[1] << 8 | p[0];
        }
    }
    free(b);
    image = pixels; image_w = (int)bw; image_h = (int)height;
    return 1;
}

int desktop_wallpaper_count(void) { return (int)(sizeof catalog / sizeof *catalog); }
const char *desktop_wallpaper_name(int index) {
    if (index == -1) return "Plain Desktop";
    return index >= 0 && index < desktop_wallpaper_count() ? catalog[index].title : "";
}
int desktop_wallpaper_current(void) { return selected; }

static int load_selection(int index) {
    clear_picture();
    selected = index;
    if (index < 0 || !resource_dir[0]) return 0;
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/%s", resource_dir, catalog[index].file);
    return n > 0 && (size_t)n < sizeof path ? decode_bmp(path) : 0;
}

void desktop_wallpaper_shutdown(void) {
    clear_picture(); resource_dir[0] = preference[0] = 0; selected = -1;
}

static void default_directory(void) {
    char executable[4096];
#ifdef __APPLE__
    uint32_t size = sizeof executable;
    int found = _NSGetExecutablePath(executable, &size) == 0;
#else
    ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
    int found = size > 0 && (size_t)size < sizeof executable - 1;
    if (found) executable[size] = 0;
#endif
    if (found) {
        char *slash = strrchr(executable, '/');
        if (slash) {
            *slash = 0;
            const char *suffix[] = {"/../Resources/wallpapers", "/wallpapers"};
            for (unsigned i = 0; i < sizeof suffix / sizeof *suffix; i++) {
                int n = snprintf(resource_dir, sizeof resource_dir, "%s%s", executable, suffix[i]);
                struct stat st;
                if (n > 0 && (size_t)n < sizeof resource_dir &&
                    !stat(resource_dir, &st) && S_ISDIR(st.st_mode)) return;
            }
        }
    }
    snprintf(resource_dir, sizeof resource_dir, "build/wallpapers");
}

int desktop_wallpaper_init(const char *directory, const char *preference_file) {
    desktop_wallpaper_shutdown();
    if ((directory && strlen(directory) >= sizeof resource_dir) ||
        (preference_file && strlen(preference_file) >= sizeof preference)) return -1;
    if (directory) strcpy(resource_dir, directory);
    else default_directory();
    if (preference_file) strcpy(preference, preference_file);
    int index = -1;
    if (preference[0]) {
        FILE *f = fopen(preference, "r");
        if (f) {
            char id[16] = {0};
            if (fgets(id, sizeof id, f)) {
                id[strcspn(id, "\r\n")] = 0;
                for (int i = 0; i < desktop_wallpaper_count(); i++)
                    if (!strcmp(id, catalog[i].id)) index = i;
            }
            fclose(f);
        }
    }
    return load_selection(index);
}

int desktop_wallpaper_select(int index) {
    if (index < -1 || index >= desktop_wallpaper_count()) return -1;
    if (preference[0]) {
        char temp[4096];
        int n = snprintf(temp, sizeof temp, "%s.XXXXXX", preference);
        if (n <= 0 || (size_t)n >= sizeof temp) return -1;
        int fd = mkstemp(temp);
        if (fd < 0) return -1;
        const char *id = index < 0 ? "plain" : catalog[index].id;
        size_t len = strlen(id);
        int ok = write(fd, id, len) == (ssize_t)len && write(fd, "\n", 1) == 1 && fsync(fd) == 0;
        if (close(fd)) ok = 0;
        if (ok && rename(temp, preference)) ok = 0;
        if (!ok) { unlink(temp); return -1; }
    }
    return load_selection(index);
}

int desktop_wallpaper_draw(Fb *fb) {
    if (!image || !fb || !fb->px || fb->w < 1 || fb->h < 1 ||
        fb->w > MAX_DIMENSION || fb->h > MAX_DIMENSION) return 0;
    if (!scaled || scaled_w != fb->w || scaled_h != fb->h) {
        uint32_t *next = malloc((size_t)fb->w * fb->h * sizeof *next);
        if (!next) return 0;
        int crop_w = image_w, crop_h = image_h;
        if ((int64_t)image_w * fb->h > (int64_t)image_h * fb->w)
            crop_w = (int)((int64_t)image_h * fb->w / fb->h);
        else crop_h = (int)((int64_t)image_w * fb->h / fb->w);
        if (crop_w < 1) crop_w = 1;
        if (crop_h < 1) crop_h = 1;
        int left = (image_w - crop_w) / 2, top = (image_h - crop_h) / 2;
        for (int y = 0; y < fb->h; y++) {
            int sy = top + (int)((int64_t)y * crop_h / fb->h);
            for (int x = 0; x < fb->w; x++) {
                int sx = left + (int)((int64_t)x * crop_w / fb->w);
                next[(size_t)y * fb->w + x] = image[(size_t)sy * image_w + sx];
            }
        }
        free(scaled); scaled = next; scaled_w = fb->w; scaled_h = fb->h;
    }
    for (int y = 0; y < fb->h; y++) {
        if (fb->clip && (y < fb->cy || y >= (int64_t)fb->cy + fb->ch)) continue;
        int left = fb->clip && fb->cx > 0 ? fb->cx : 0;
        int64_t right = fb->clip ? (int64_t)fb->cx + fb->cw : fb->w;
        if (right > fb->w) right = fb->w;
        if (left >= right) continue;
        memcpy(fb->px + (size_t)y * fb->w + left, scaled + (size_t)y * fb->w + left,
               (size_t)(right - left) * sizeof *scaled);
    }
    return 1;
}
