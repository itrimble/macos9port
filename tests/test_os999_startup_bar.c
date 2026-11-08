/* Boot progress bar checks: the bar must be a live indicator whose bands
   crawl to the right, whose empty track is striped too, and whose lit edge
   advances to the right as the machine boots. */
#include "os999_startup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* The frame matches the real 1024x768 desktop window so the bar is checked
   as shipped. The palette mirrors the renderer, pinning the contract instead
   of the implementation. */
#define W 1024
#define H 768
#define COLOR_STRIPE_A 0xFF7E9ED6u
#define COLOR_STRIPE_B 0xFF46689Fu
#define COLOR_BEVEL_LIGHT 0xFF9FB2D6u
#define COLOR_EMPTY_ON 0xFF44587Fu
#define COLOR_EMPTY_OFF 0xFF32425Fu
#define DESKTOP_COLOR 0xFF203040u

/* The boot bar is split into this many segments by gaps of BAR_GAP pixels. */
#define BAR_SEGMENTS 24
#define BAR_GAP 2

static uint32_t image[W * H];
static uint32_t desktop[W * H];

static int count_color(uint32_t color) {
    int n = 0;
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        if (image[i] == color) n++;
    return n;
}

/* The boot bar is the only thing drawn with the light bevel colour, so its
   border gives the interior geometry without mirroring the layout code. */
static void find_bar_interior(int *inner_x, int *inner_y, int *inner_w, int *inner_h) {
    int min_x = W, min_y = H, max_x = -1, max_y = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (image[(size_t)y * W + x] == COLOR_BEVEL_LIGHT) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
    *inner_x = min_x + 1;
    *inner_y = min_y + 1;
    *inner_w = max_x - min_x - 2;
    *inner_h = max_y - min_y - 2;
}

/* The first segment of the bar, which carries no separator gap. */
static void bar_first_segment(int inner_w, int *offset, int *width) {
    int seg_w = (inner_w - (BAR_SEGMENTS - 1) * BAR_GAP) / BAR_SEGMENTS;
    if (seg_w < 1) seg_w = 1;
    int total = BAR_SEGMENTS * seg_w + (BAR_SEGMENTS - 1) * BAR_GAP;
    *offset = (inner_w - total) / 2;
    *width = seg_w;
}

/* Stripe state of one interior row: gap, stripe on, stripe off. */
#define BAR_CLASS_GAP 0
#define BAR_CLASS_ON 1
#define BAR_CLASS_OFF 2

static void bar_row_classes(int inner_x, int row, int *out, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t c = image[(size_t)row * W + inner_x + i];
        if (c == COLOR_STRIPE_A || c == COLOR_EMPTY_ON) out[i] = BAR_CLASS_ON;
        else if (c == COLOR_STRIPE_B || c == COLOR_EMPTY_OFF) out[i] = BAR_CLASS_OFF;
        else out[i] = BAR_CLASS_GAP;
    }
}

/* Rightmost lit stripe column of one row, or -1 when nothing is lit. */
static int bright_edge(int inner_x, int row, int inner_w) {
    int edge = -1;
    for (int i = 0; i < inner_w; i++) {
        uint32_t c = image[(size_t)row * W + inner_x + i];
        if (c == COLOR_STRIPE_A || c == COLOR_STRIPE_B) edge = i;
    }
    return edge;
}

int main(void) {
    memset(image, 0, sizeof image);
    for (size_t i = 0; i < sizeof desktop / sizeof desktop[0]; i++) desktop[i] = DESKTOP_COLOR;

    Os999Startup s;
    int inner_x, inner_y, inner_w, inner_h;
    int seg_offset, seg_width;
    int classes_a[64], classes_b[64];
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 1000, 0, image, desktop, W, H);
    find_bar_interior(&inner_x, &inner_y, &inner_w, &inner_h);
    bar_first_segment(inner_w, &seg_offset, &seg_width);
    CHECK(inner_w > 0 && inner_h > 0);
    CHECK(seg_width >= 8);
    /* The track behind the fill is a dim zebra, not a flat plate, so the bar
       reads as alive from the very first frame. */
    CHECK(count_color(COLOR_EMPTY_ON) > 20);
    CHECK(count_color(COLOR_EMPTY_OFF) > 20);

    /* Find the step at which the bands move, then check which way they went. */
    bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_a, seg_width);
    int step_ms = 0;
    for (int dt = 5; dt <= 100 && step_ms == 0; dt += 5) {
        os999_startup_frame(&s, 1000 + (uint32_t)dt, 0, image, desktop, W, H);
        bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_b, seg_width);
        if (memcmp(classes_a, classes_b, sizeof(int) * (size_t)seg_width) != 0)
            step_ms = dt;
    }
    /* The bands must move, and they must move fast enough to be seen. */
    CHECK(step_ms > 0);
    CHECK(step_ms <= 40);
    int right_shift = 1, left_shift = 1;
    for (int i = 0; i < seg_width; i++) {
        int prev = (i - 1 + seg_width) % seg_width;
        int next = (i + 1) % seg_width;
        if (classes_b[i] != classes_a[prev]) right_shift = 0;
        if (classes_b[i] != classes_a[next]) left_shift = 0;
    }
    CHECK(right_shift);
    CHECK(!left_shift);

    /* The bar is animated before any progress has been made. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 0, 0, image, desktop, W, H);
    bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_a, seg_width);
    os999_startup_frame(&s, 100, 0, image, desktop, W, H);
    bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_b, seg_width);
    CHECK(memcmp(classes_a, classes_b, sizeof(int) * (size_t)seg_width) != 0);

    /* The lit part of the bar advances to the right as the boot runs. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 500, 0, image, desktop, W, H);
    int edge_early = bright_edge(inner_x, inner_y + 2, inner_w);
    os999_startup_frame(&s, 2500, 0, image, desktop, W, H);
    int edge_late = bright_edge(inner_x, inner_y + 2, inner_w);
    CHECK(edge_early > 0);
    CHECK(edge_late > edge_early);

    /* A desktop that is ready early must not snap the bar to full: the fill
       still has to cross the track while the intro plays. */
    os999_startup_begin(&s, 0, 0);
    s.ready = 1;
    os999_startup_frame(&s, 1000, 1, image, desktop, W, H);
    int edge_ready = bright_edge(inner_x, inner_y + 2, inner_w);
    CHECK(edge_ready > 0);
    CHECK(edge_ready < inner_w / 2);

    /* Reduced motion keeps the bar still. */
    os999_startup_begin(&s, 0, 1);
    os999_startup_frame(&s, 1000, 0, image, desktop, W, H);
    bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_a, seg_width);
    os999_startup_frame(&s, 1400, 0, image, desktop, W, H);
    bar_row_classes(inner_x + seg_offset, inner_y + 2, classes_b, seg_width);
    CHECK(memcmp(classes_a, classes_b, sizeof(int) * (size_t)seg_width) == 0);

    /* The failure screen is static: the bar is frozen behind the retry dialog
       and nothing on the screen keeps moving. */
    os999_startup_begin(&s, 0, 0);
    os999_startup_frame(&s, 1000, 0, image, desktop, W, H);
    os999_startup_fail(&s, 1000);
    os999_startup_frame(&s, 1400, 0, image, desktop, W, H);
    uint32_t frozen = 0;
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        frozen = frozen * 31u + image[i];
    os999_startup_frame(&s, 1900, 0, image, desktop, W, H);
    uint32_t later = 0;
    for (size_t i = 0; i < sizeof image / sizeof image[0]; i++)
        later = later * 31u + image[i];
    CHECK(frozen == later);

    printf("OS999 startup bar: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
