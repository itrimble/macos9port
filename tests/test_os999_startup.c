#include "os999_startup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

void fb_init(Fb *fb, uint32_t *px, int w, int h) {
    *fb = (Fb){.px = px, .w = w, .h = h};
}
void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < fb->w && yy < fb->h)
            fb->px[(size_t)yy * (size_t)fb->w + (size_t)xx] = color;
}
void fb_frame(Fb *fb, int x, int y, int w, int h, uint32_t color) {
    for (int xx = x; xx < x + w; xx++) {
        fb_fill(fb, xx, y, 1, 1, color);
        fb_fill(fb, xx, y + h - 1, 1, 1, color);
    }
    for (int yy = y; yy < y + h; yy++) {
        fb_fill(fb, x, yy, 1, 1, color);
        fb_fill(fb, x + w - 1, yy, 1, 1, color);
    }
}
void fb_text(Fb *fb, int x, int y, uint32_t color, const char *s) {
    (void)fb; (void)x; (void)y; (void)color; (void)s;
}
int fb_text_width(const char *s) { return (int)strlen(s) * 8; }
int typeface_ready(void) { return 0; }
int typeface_startup_ready(void) { return 0; }
int typeface_width(int body, const char *s, int n) { (void)body; (void)s; return n * 8; }
void typeface_draw(void *fb, int body, int x, int y, uint32_t color, const char *s, int n) {
    (void)fb; (void)body; (void)x; (void)y; (void)color; (void)s; (void)n;
}

/* Boot screen palette, mirrored from the renderer so the tests pin the
   contract instead of the implementation. The frame matches the real
   1024x768 desktop window so scaling and layout are checked as shipped. */
#define W 1024
#define H 768
#define COLOR_OUTLINE 0xFF000000u
#define COLOR_DARK_GREEN 0xFF336600u
#define COLOR_MID_GREEN 0xFF669900u
#define COLOR_LIGHT_GREEN 0xFF99CC33u
#define COLOR_HILITE 0xFFCCFF99u
#define COLOR_STEM 0xFF663300u
#define COLOR_STEM_DARK 0xFF333300u
#define COLOR_LEAF 0xFF33CC00u
#define COLOR_LEAF_DARK 0xFF339900u
#define COLOR_WORDMARK 0xFFF4E3A8u
#define COLOR_TAGLINE 0xFFC6D2E6u
#define COLOR_STATUS 0xFFE8EEF8u
#define COLOR_STRIPE_A 0xFF7E9ED6u
#define COLOR_STRIPE_B 0xFF46689Fu
#define DESKTOP_COLOR 0xFF203040u

static uint32_t image[W * H];
static uint32_t desktop[W * H];

static uint32_t mix(uint32_t a, uint32_t b, double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        int av = (int)((a >> shift) & 255u), bv = (int)((b >> shift) & 255u);
        int v = (int)(av + (bv - av) * t + 0.5);
        out |= (uint32_t)v << shift;
    }
    return out;
}

/* Mirrors the boot background so the halo check can compare a pixel against
   the background that should be there instead. */
static uint32_t background_at(int y) {
    double t = (double)y / (double)(H - 1);
    uint32_t c = mix(0xFF2E3A55u, 0xFF586C93u, t);
    if (y & 1) c = mix(c, 0xFF1B2438u, 0.16);
    return c;
}

static int is_fruit(uint32_t c) {
    return c == COLOR_OUTLINE || c == COLOR_DARK_GREEN || c == COLOR_MID_GREEN ||
           c == COLOR_LIGHT_GREEN || c == COLOR_HILITE || c == COLOR_STEM ||
           c == COLOR_STEM_DARK || c == COLOR_LEAF || c == COLOR_LEAF_DARK;
}

static int count_color(uint32_t color) {
    int n = 0;
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        if (image[i] == color) n++;
    return n;
}

static void fruit_bounds(int *x0, int *y0, int *x1, int *y1) {
    *x0 = W; *y0 = H; *x1 = -1; *y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (is_fruit(image[(size_t)y * W + x])) {
                if (x < *x0) *x0 = x;
                if (x > *x1) *x1 = x;
                if (y < *y0) *y0 = y;
                if (y > *y1) *y1 = y;
            }
}

/* Expected glyph blocks for a string in the 8x8 bitmap font, in frame
   coordinates. Each lit glyph pixel covers scale x scale frame pixels. */
static int expected_glyphs(const char *text, int scale, int *min_x, int *min_y,
                           int *max_x, int *max_y) {
    int count = 0, lo_x = 1 << 30, lo_y = 1 << 30, hi_x = -1, hi_y = -1;
    int pen = 0;
    while (text && *text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (rows[row] & (uint8_t)(1u << col)) {
                    int x = pen + col * scale, y = row * scale;
                    if (x < lo_x) lo_x = x;
                    if (y < lo_y) lo_y = y;
                    if (x + scale - 1 > hi_x) hi_x = x + scale - 1;
                    if (y + scale - 1 > hi_y) hi_y = y + scale - 1;
                    count += scale * scale;
                }
        pen += 8 * scale;
        text += bytes;
    }
    *min_x = lo_x; *min_y = lo_y; *max_x = hi_x; *max_y = hi_y;
    return count;
}

static void write_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char rgb[3] = {(unsigned char)((px[i] >> 16) & 255u),
                                (unsigned char)((px[i] >> 8) & 255u),
                                (unsigned char)(px[i] & 255u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void) {
    char root[] = "/tmp/os999-startup-prefs-XXXXXX";
    char *directory = mkdtemp(root);
    CHECK(directory != NULL);
    if (!directory) return 1;
    CHECK(os999_startup_pref_read(root) == 1);
    CHECK(os999_startup_pref_write(root, 0) == 0);
    CHECK(os999_startup_pref_read(root) == 0);
    CHECK(os999_startup_pref_write(root, 1) == 0);
    CHECK(os999_startup_pref_read(root) == 1);
    char preference[256];
    snprintf(preference, sizeof preference, "%s/.os999-startup", root);
    CHECK(unlink(preference) == 0);
    CHECK(rmdir(root) == 0);

    /* The status line below the progress bar is exact text. */
    CHECK(strcmp(os999_startup_status_text(), "Starting Up...") == 0);
    CHECK(strlen(os999_startup_status_text()) == 14);

    memset(image, 0, sizeof image);
    for (size_t i = 0; i < sizeof desktop / sizeof desktop[0]; i++) desktop[i] = DESKTOP_COLOR;

    Os999Startup s;
    uint32_t start = 0xFFFFFF00u;
    os999_startup_begin(&s, start, 0);
    CHECK(s.phase == OS999_STARTUP_SPIN && os999_startup_active(&s));
    os999_startup_frame(&s, start + 3299u, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_frame(&s, start + 3300u, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_SETTLE);
    os999_startup_frame(&s, start + 4800u, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_HOLD);
    os999_startup_frame(&s, start + 4990u, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_REVEAL);
    os999_startup_frame(&s, start + 5240u, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_DONE && !os999_startup_active(&s));
    CHECK(image[0] == desktop[0]);

    os999_startup_begin(&s, 100, 1);
    os999_startup_frame(&s, 400, 1, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_SETTLE && s.reduced_motion);
    os999_startup_fail(&s, 2000);
    CHECK(s.phase == OS999_STARTUP_ERROR);
    os999_startup_frame(&s, 32000, 0, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_DONE);
    /* An inactive/finished intro must never paint itself over the desktop,
       including callers that have no saved desktop frame. */
    image[0] = 0xFFABCDEFu;
    os999_startup_frame(&s, 32001, 1, image, NULL, W, H);
    CHECK(image[0] == 0xFFABCDEFu);
    s.phase = OS999_STARTUP_INACTIVE;
    os999_startup_frame(&s, 32002, 1, image, desktop, W, H);
    CHECK(image[0] == desktop[0]);
    /* Replay allocates fresh renderer scratch after completed/aborted runs. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 50, 0, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_release();
    os999_startup_release();
    os999_startup_begin(&s, 0, 1);
    os999_startup_frame(&s, 50, 0, image, desktop, W, H);
    CHECK(s.phase == OS999_STARTUP_SPIN);
    os999_startup_release();

    /* Progress is truthful: it never completes while work is pending. */
    os999_startup_begin(&s, 0, 0);
    CHECK(os999_startup_progress(&s, 0) < 0.01);
    CHECK(os999_startup_progress(&s, 1650) > 0.4);
    CHECK(os999_startup_progress(&s, 100000) <= 0.92);
    CHECK(os999_startup_progress(&s, 100000) > 0.9);
    s.ready = 1;
    CHECK(os999_startup_progress(&s, 100000) == 1.0);

    /* A failure freezes the bar instead of inventing progress. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 1000, 0, image, desktop, W, H);
    double frozen = os999_startup_progress(&s, 1200);
    CHECK(frozen > 0.2 && frozen < 0.4);
    os999_startup_fail(&s, 1200);
    CHECK(os999_startup_progress(&s, 999999) == frozen);

    /* Frame content: fruit, wordmark, bar and status. The fruit is sampled
       while it faces the viewer so its palette is unblended. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 0, 0, image, desktop, W, H);
    CHECK(count_color(COLOR_LIGHT_GREEN) > 100);
    CHECK(count_color(COLOR_DARK_GREEN) > 10);
    CHECK(count_color(COLOR_HILITE) > 0);
    CHECK(count_color(COLOR_LEAF) > 4);
    CHECK(count_color(COLOR_STEM) > 4);
    CHECK(count_color(COLOR_WORDMARK) > 50);
    CHECK(count_color(COLOR_TAGLINE) > 20);
    CHECK(count_color(COLOR_STATUS) > 20);
    /* No desktop content leaks through the boot display. */
    CHECK(count_color(DESKTOP_COLOR) == 0);

    int fx0, fy0, fx1, fy1;
    fruit_bounds(&fx0, &fy0, &fx1, &fy1);
    CHECK(fx0 >= 0 && fy0 >= 0);
    /* The fruit is drawn at an integer scale: 24 sprite columns wide. */
    int scale = W / 200;
    if (scale < 2) scale = 2;
    CHECK(fx1 - fx0 + 1 == 24 * scale);
    CHECK(fy1 - fy0 + 1 >= 30 * scale);
    CHECK(fy1 - fy0 + 1 <= 32 * scale);

    /* No halo or shadow is painted under the fruit. */
    int halo_pixels = 0;
    for (int y = fy1 + 1; y <= fy1 + 12 && y < H; y++) {
        uint32_t bg = background_at(y);
        for (int x = 0; x < W; x++) {
            uint32_t p = image[(size_t)y * W + (size_t)x];
            for (int shift = 0; shift <= 16; shift += 8) {
                int pv = (int)((p >> shift) & 255u);
                int bv = (int)((bg >> shift) & 255u);
                if (pv < bv) halo_pixels++;
            }
        }
    }
    CHECK(halo_pixels == 0);

    /* The status line renders exactly "Starting Up..." in the pixel font. */
    int sx0 = W, sy0 = H, sx1 = -1, sy1 = -1, status_pixels = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (image[(size_t)y * W + (size_t)x] == COLOR_STATUS) {
                if (x < sx0) sx0 = x;
                if (x > sx1) sx1 = x;
                if (y < sy0) sy0 = y;
                if (y > sy1) sy1 = y;
                status_pixels++;
            }
    CHECK(status_pixels > 0);
    /* The glyph height gives the integer pixel scale of the status line. */
    int status_scale = (sy1 - sy0 + 1) / 8;
    CHECK(status_scale >= 1);
    int gx0, gy0, gx1, gy1;
    int expected = expected_glyphs("Starting Up...", status_scale,
                                   &gx0, &gy0, &gx1, &gy1);
    CHECK(expected > 0);
    /* The rendered status line is exactly the expected text, pixel for pixel. */
    CHECK((sx1 - sx0 + 1) == (gx1 - gx0 + 1));
    CHECK((sy1 - sy0 + 1) == (gy1 - gy0 + 1));
    CHECK(expected == status_pixels);

    /* Every expected glyph block is present and no other status pixel exists. */
    int points[8 * 8 * 32][2];
    int point_count = 0;
    const char *text = "Starting Up...";
    int pen = 0;
    while (*text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (rows[row] & (uint8_t)(1u << col)) {
                    if (point_count < (int)(sizeof points / sizeof points[0])) {
                        points[point_count][0] = pen + col * status_scale;
                        points[point_count][1] = row * status_scale;
                    }
                    point_count++;
                }
        pen += 8 * status_scale;
        text += bytes;
    }
    int matched = 0;
    for (int i = 0; i < point_count && i < (int)(sizeof points / sizeof points[0]); i++) {
        int px = sx0 + (points[i][0] - gx0);
        int py = sy0 + (points[i][1] - gy0);
        int block_ok = 1;
        for (int by = 0; by < status_scale && block_ok; by++)
            for (int bx = 0; bx < status_scale; bx++) {
                int qx = px + bx, qy = py + by;
                if (qx < 0 || qx >= W || qy < 0 || qy >= H ||
                    image[(size_t)qy * W + (size_t)qx] != COLOR_STATUS) {
                    block_ok = 0;
                    break;
                }
            }
        if (block_ok) matched += status_scale * status_scale;
    }
    CHECK(matched == status_pixels);
    CHECK(matched == expected);

    /* The progress bar advances as the sequence runs. */
    int early = 0;
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 1200, 0, image, desktop, W, H);
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        if (image[i] == COLOR_STRIPE_A || image[i] == COLOR_STRIPE_B) early++;
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 3000, 1, image, desktop, W, H);
    int late = 0;
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        if (image[i] == COLOR_STRIPE_A || image[i] == COLOR_STRIPE_B) late++;
    CHECK(early > 0);
    CHECK(late > early);

    /* Motion: the fruit turns while work is pending and rests when reduced.
       The silhouette stays put; the shading bands move. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 200, 0, image, desktop, W, H);
    fruit_bounds(&fx0, &fy0, &fx1, &fy1);
    int moving_width = fx1 - fx0 + 1;
    uint32_t moving_first = 0;
    for (int y = fy0; y <= fy1; y++)
        for (int x = fx0; x <= fx1; x++)
            moving_first = moving_first * 31u + image[(size_t)y * W + (size_t)x];
    os999_startup_frame(&s, 900, 0, image, desktop, W, H);
    fruit_bounds(&fx0, &fy0, &fx1, &fy1);
    uint32_t moving_second = 0;
    for (int y = fy0; y <= fy1; y++)
        for (int x = fx0; x <= fx1; x++)
            moving_second = moving_second * 31u + image[(size_t)y * W + (size_t)x];
    CHECK((fx1 - fx0 + 1) == moving_width);
    CHECK(moving_first != moving_second);

    os999_startup_begin(&s, 0, 1);
    os999_startup_frame(&s, 200, 0, image, desktop, W, H);
    fruit_bounds(&fx0, &fy0, &fx1, &fy1);
    int still_width = fx1 - fx0 + 1;
    uint32_t still_first = 0;
    for (int y = fy0; y <= fy1; y++)
        for (int x = fx0; x <= fx1; x++)
            still_first = still_first * 31u + image[(size_t)y * W + (size_t)x];
    os999_startup_frame(&s, 900, 0, image, desktop, W, H);
    fruit_bounds(&fx0, &fy0, &fx1, &fy1);
    uint32_t still_second = 0;
    for (int y = fy0; y <= fy1; y++)
        for (int x = fx0; x <= fx1; x++)
            still_second = still_second * 31u + image[(size_t)y * W + (size_t)x];
    CHECK((fx1 - fx0 + 1) == still_width);
    CHECK(still_first == still_second);

    /* Optional frame dumps for visual review outside the test. */
    const char *dump = getenv("OS999_STARTUP_DUMP");
    if (dump && dump[0]) {
        char path[512];
        os999_startup_begin(&s, 0, 0);
        os999_startup_frame(&s, 1200, 0, image, desktop, W, H);
        snprintf(path, sizeof path, "%s-spin.ppm", dump);
        write_ppm(path, image, W, H);
        os999_startup_frame(&s, 3400, 1, image, desktop, W, H);
        snprintf(path, sizeof path, "%s-settle.ppm", dump);
        write_ppm(path, image, W, H);
        os999_startup_frame(&s, 4900, 1, image, desktop, W, H);
        snprintf(path, sizeof path, "%s-hold.ppm", dump);
        write_ppm(path, image, W, H);
        os999_startup_frame(&s, 5100, 1, image, desktop, W, H);
        snprintf(path, sizeof path, "%s-reveal.ppm", dump);
        write_ppm(path, image, W, H);
    }

    printf("OS999 startup: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
