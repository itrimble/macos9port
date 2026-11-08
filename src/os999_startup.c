#include "os999_startup.h"
#include "os999_startup_sprite.h"
#include "ospi.h"
#include <stdio.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define SPIN_MS 3300u
#define SPIN_PERIOD_MS 4800.0
#define SETTLE_MS 1500u
#define REDUCED_SPIN_MS 300u
#define REDUCED_SETTLE_MS 180u
#define HOLD_MS 190u
#define REVEAL_MS 250u

/* Boot copy. The status line below the progress bar is exact text. */
#define WORDMARK "OS999"
#define TAGLINE "Made for the fun of it."
#define STATUS_TEXT "Starting Up..."

/* Late-90s palette: deep blue-grey panel, cream wordmark, striped bar. */
#define BG_TOP 0xFF2E3A55u
#define BG_BOTTOM 0xFF586C93u
#define BG_SCANLINE 0xFF1B2438u
#define WORDMARK_COLOR 0xFFF4E3A8u
#define WORDMARK_SHADOW 0xFF16203Au
#define TAGLINE_COLOR 0xFFC6D2E6u
#define STATUS_COLOR 0xFFE8EEF8u
#define BAR_EDGE 0xFF16203Au
#define BAR_BEVEL_LIGHT 0xFF9FB2D6u
#define BAR_BEVEL_DARK 0xFF101A2Eu
#define BAR_TRACK 0xFF3A4A6Bu
#define BAR_STRIPE_A 0xFF7E9ED6u
#define BAR_STRIPE_B 0xFF46689Fu
#define BAR_EMPTY_ON 0xFF44587Fu
#define BAR_EMPTY_OFF 0xFF32425Fu
#define BAR_HEAD 0xFFE8F1FFu

#define BAR_SEGMENTS 24
#define BAR_GAP 2

/* The bar is one continuous zebra. The bands repeat every 8 pixels, half of
   them lit, and they advance to the right as the phase grows. */
#define BAR_STRIPE_PERIOD 8
#define BAR_STRIPE_ON 4
/* One pixel every 25 ms is about 40 pixels a second: fast enough to read as
   motion, slow enough to look like the late-90s striped bar. */
#define BAR_STRIPE_STEP_MS 25u

void os999_startup_release(void) {
    /* The renderer owns no scratch memory any more. */
}

int os999_startup_pref_read(const char *root) {
    char path[OSPI_PATH + 32];
    snprintf(path, sizeof path, "%s/.os999-startup", root);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char value[16] = {0};
    int enabled = 1;
    if (fgets(value, sizeof value, f) && value[0] == '0') enabled = 0;
    fclose(f);
    return enabled;
}

int os999_startup_pref_write(const char *root, int enabled) {
    char path[OSPI_PATH + 32];
    snprintf(path, sizeof path, "%s/.os999-startup", root);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%d\n", enabled ? 1 : 0) > 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

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

void os999_draw_menu_apple(Fb *fb, int x, int y) {
    if(!fb)return;
    static const uint8_t half_width[]={2,4,5,6,7,7,7,7,6,6,5,4,3,2};
    const uint32_t green=0xFF78AD32u,light=0xFFA3CE55u,dark=0xFF477625u;
    fb_fill(fb,x+8,y+1,1,3,0xFF77542Fu);
    fb_fill(fb,x+9,y+2,3,1,0xFF9ECC4Eu);
    fb_fill(fb,x+8,y+3,4,1,0xFF85B83Bu);
    for(int row=0;row<14;row++){
        int width=half_width[row];
        fb_fill(fb,x+8-width,y+4+row,width*2,1,green);
        if(row>=4&&row<=10) fb_fill(fb,x+5,y+4+row,2,1,light);
        if(row>=8) fb_fill(fb,x+8+width-2,y+4+row,2,1,dark);
    }
}

static int bitmap_width_scaled(const char *text, int scale) {
    int width = 0;
    while (text && *text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        width += 8 * scale;
        text += bytes;
    }
    return width;
}

static void bitmap_draw_scaled(Fb *fb, int x, int y, uint32_t color,
                              const char *text, int scale) {
    while (text && *text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (rows[row] & (uint8_t)(1u << col))
                    fb_fill(fb, x + col * scale, y + row * scale,
                            scale, scale, color);
            }
        }
        x += 8 * scale;
        text += bytes;
    }
}

static void bitmap_draw_extruded(Fb *fb, int x, int y, uint32_t color,
                                 uint32_t shadow, const char *text, int scale) {
    if (shadow) bitmap_draw_scaled(fb, x + scale, y + scale, shadow, text, scale);
    bitmap_draw_scaled(fb, x, y, color, text, scale);
}

void os999_startup_begin(Os999Startup *s, uint32_t now, int reduced_motion) {
    if (!s) return;
    *s = (Os999Startup){.phase = OS999_STARTUP_SPIN,
                        .phase_started = now,
                        .reduced_motion = reduced_motion,
                        .progress = 0.0};
}

void os999_startup_fail(Os999Startup *s, uint32_t now) {
    if (!s) return;
    /* Freeze the bar where it stood so the failure does not invent progress. */
    s->progress = os999_startup_progress(s, now);
    s->phase = OS999_STARTUP_ERROR;
    s->phase_started = now;
    s->failed = 1;
}

int os999_startup_active(const Os999Startup *s) {
    return s && s->phase != OS999_STARTUP_INACTIVE && s->phase != OS999_STARTUP_DONE;
}

const char *os999_startup_status_text(void) {
    return STATUS_TEXT;
}

double os999_startup_progress(const Os999Startup *s, uint32_t now) {
    if (!s) return 0.0;
    if (s->phase == OS999_STARTUP_ERROR) return s->progress;
    /* The bar completes when the intro leaves the spin phase, which only
       happens once the desktop is genuinely ready. A desktop that exists
       early does not complete the bar: the fill still crosses the track. */
    if (s->phase != OS999_STARTUP_SPIN) return 1.0;
    uint32_t elapsed = now - s->phase_started;
    double t = (double)elapsed / (double)SPIN_MS;
    if (t > 1.0) t = 1.0;
    /* Work is still pending, so the bar never claims completion. */
    return t * 0.92;
}

static void advance(Os999Startup *s, uint32_t now, int ready) {
    uint32_t elapsed = now - s->phase_started;
    if (s->phase == OS999_STARTUP_ERROR) {
        /* A failed intro is bounded and yields back to the desktop after a message. */
        if (elapsed >= 30000u) s->phase = OS999_STARTUP_DONE;
    } else if (s->phase == OS999_STARTUP_SPIN &&
               elapsed >= (s->reduced_motion ? REDUCED_SPIN_MS : SPIN_MS) && ready) {
        s->ready = 1; s->phase = OS999_STARTUP_SETTLE; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_SETTLE &&
               elapsed >= (s->reduced_motion ? REDUCED_SETTLE_MS : SETTLE_MS)) {
        s->phase = OS999_STARTUP_HOLD; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_HOLD && elapsed >= HOLD_MS) {
        s->phase = OS999_STARTUP_REVEAL; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_REVEAL && elapsed >= REVEAL_MS) {
        s->phase = OS999_STARTUP_DONE;
    }
}

typedef struct {
    int fruit_scale, fruit_x, fruit_y;
    int word_scale, word_x, word_y;
    int tag_scale, tag_x, tag_y;
    int bar_x, bar_y, bar_w, bar_h;
    int status_scale, status_x, status_y;
} BootLayout;

static void compute_layout(const Fb *fb, BootLayout *L) {
    int w = fb->w, h = fb->h;
    int scale = w / 200;
    if (scale < 2) scale = 2;
    if (scale > 8) scale = 8;
    int small = scale / 2;
    if (small < 1) small = 1;
    int fruit_h = os999_startup_fruit_height(scale);
    int word_w = bitmap_width_scaled(WORDMARK, scale);
    int word_h = 8 * scale;
    int tag_w = bitmap_width_scaled(TAGLINE, small);
    int tag_h = 8 * small;
    int status_w = bitmap_width_scaled(STATUS_TEXT, small);
    int status_h = 8 * small;
    int bar_w = w / 2;
    if (bar_w > 440) bar_w = 440;
    if (bar_w < 180) bar_w = 180;
    int bar_h = 20;
    int gap_fruit = 24, gap_word = 18, gap_tag = 26, gap_bar = 14;
    int block = fruit_h + gap_fruit + word_h + gap_word + tag_h +
                gap_tag + bar_h + gap_bar + status_h;
    int top = (h - block) / 2 - h / 24;
    if (top < 8) top = 8;
    L->fruit_scale = scale;
    L->fruit_x = w / 2;
    L->fruit_y = top + fruit_h / 2;
    int y = top + fruit_h + gap_fruit;
    L->word_scale = scale;
    L->word_x = (w - word_w) / 2;
    L->word_y = y;
    y += word_h + gap_word;
    L->tag_scale = small;
    L->tag_x = (w - tag_w) / 2;
    L->tag_y = y;
    y += tag_h + gap_tag;
    L->bar_x = (w - bar_w) / 2;
    L->bar_y = y;
    L->bar_w = bar_w;
    L->bar_h = bar_h;
    y += bar_h + gap_bar;
    L->status_scale = small;
    L->status_x = (w - status_w) / 2;
    L->status_y = y;
}

static void draw_background(Fb *fb) {
    for (int y = 0; y < fb->h; y++) {
        double t = fb->h > 1 ? (double)y / (double)(fb->h - 1) : 0.0;
        uint32_t c = mix(BG_TOP, BG_BOTTOM, t);
        /* Every other row is dimmed for a subtle CRT scanline texture. */
        if (y & 1) c = mix(c, BG_SCANLINE, 0.16);
        fb_fill(fb, 0, y, fb->w, 1, c);
    }
}

/* Zebra bands run diagonally and advance to the right as the phase grows. */
static int stripe_on(int col, int row, int phase) {
    int p = (col + row - phase) % BAR_STRIPE_PERIOD;
    if (p < 0) p += BAR_STRIPE_PERIOD;
    return p < BAR_STRIPE_ON;
}

/* Paint one row of the zebra. The column index is absolute inside the track,
   so the bands run unbroken across the whole bar instead of restarting in
   every segment. */
static void zebra_row(Fb *fb, int x, int y, int width, int row, int phase,
                       uint32_t on_color, uint32_t off_color) {
    int col = 0;
    while (col < width) {
        int on = stripe_on(col, row, phase);
        int run = 1;
        while (col + run < width && stripe_on(col + run, row, phase) == on) run++;
        fb_fill(fb, x + col, y, run, 1, on ? on_color : off_color);
        col += run;
    }
}

static void draw_progress_bar(Fb *fb, int x, int y, int w, int h,
                              double progress, uint32_t now, int animate) {
    if (w < 8 || h < 8) return;
    fb_frame(fb, x, y, w, h, BAR_EDGE);
    fb_fill(fb, x + 1, y + 1, w - 2, h - 2, BAR_TRACK);
    /* Classic bevel: light along the top and left, dark along the bottom. */
    fb_fill(fb, x + 1, y + 1, w - 2, 1, BAR_BEVEL_LIGHT);
    fb_fill(fb, x + 1, y + 1, 1, h - 2, BAR_BEVEL_LIGHT);
    fb_fill(fb, x + 1, y + h - 2, w - 2, 1, BAR_BEVEL_DARK);
    fb_fill(fb, x + w - 2, y + 1, 1, h - 2, BAR_BEVEL_DARK);
    int inner_x = x + 2, inner_y = y + 2;
    int inner_w = w - 4, inner_h = h - 4;
    if (inner_w < BAR_STRIPE_PERIOD || inner_h < 2) return;
    int phase = animate ? (int)((now / BAR_STRIPE_STEP_MS) % BAR_STRIPE_PERIOD) : 0;

    /* The empty track carries a dim zebra, so the bar reads as alive from the
       first frame and the fill is seen crossing it. */
    for (int row = 0; row < inner_h; row++)
        zebra_row(fb, inner_x, inner_y + row, inner_w, row, phase,
                  BAR_EMPTY_ON, BAR_EMPTY_OFF);
    /* The filled part is the bright zebra, clipped at the progress edge. */
    int fill_w = (int)(progress * (double)inner_w);
    if (fill_w > inner_w) fill_w = inner_w;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > 0)
        for (int row = 0; row < inner_h; row++)
            zebra_row(fb, inner_x, inner_y + row, fill_w, row, phase,
                      BAR_STRIPE_A, BAR_STRIPE_B);
    /* Segment separators sit on top of the zebra, so the bar stays segmented. */
    int seg_w = (inner_w - (BAR_SEGMENTS - 1) * BAR_GAP) / BAR_SEGMENTS;
    if (seg_w < 1) seg_w = 1;
    int total = BAR_SEGMENTS * seg_w + (BAR_SEGMENTS - 1) * BAR_GAP;
    int start_x = inner_x + (inner_w - total) / 2;
    for (int i = 1; i < BAR_SEGMENTS; i++) {
        int gx = start_x + i * (seg_w + BAR_GAP);
        if (gx + BAR_GAP > inner_x + inner_w) break;
        fb_fill(fb, gx, inner_y, BAR_GAP, inner_h, BAR_TRACK);
    }
    /* A bright cap marks the head of the fill. */
    if (fill_w > 0 && fill_w < inner_w) {
        int cap = fill_w < 3 ? fill_w : 3;
        fb_fill(fb, inner_x + fill_w - cap, inner_y, cap, inner_h, BAR_HEAD);
    }
}

static double fruit_angle(const Os999Startup *s, uint32_t now) {
    if (s->reduced_motion) return 0.0;
    uint32_t elapsed = now - s->phase_started;
    if (s->phase == OS999_STARTUP_SPIN)
        return ((double)elapsed / SPIN_PERIOD_MS) * 2.0 * PI;
    if (s->phase == OS999_STARTUP_SETTLE) {
        /* Keep the spin's velocity, then taper it so the fruit lands upright. */
        double base = 2.0 * PI * ((double)SPIN_MS / SPIN_PERIOD_MS);
        double left = 2.0 * PI - base;
        double t = (double)elapsed / (double)SETTLE_MS;
        if (t > 1.0) t = 1.0;
        double stop = t + t * t - t * t * t;
        return base + left * stop;
    }
    return 2.0 * PI;
}

static void render_intro(Fb *fb, const Os999Startup *s, uint32_t now) {
    draw_background(fb);
    BootLayout L;
    compute_layout(fb, &L);
    os999_startup_draw_fruit(fb, L.fruit_x, L.fruit_y, L.fruit_scale,
                             fruit_angle(s, now));
    bitmap_draw_extruded(fb, L.word_x, L.word_y, WORDMARK_COLOR, WORDMARK_SHADOW,
                          WORDMARK, L.word_scale);
    bitmap_draw_scaled(fb, L.tag_x, L.tag_y, TAGLINE_COLOR, TAGLINE, L.tag_scale);
    double progress = os999_startup_progress(s, now);
    /* The bands keep crawling until the desktop takes over the screen. A
       failure freezes them, so the bar never invents progress. */
    int animate = !s->reduced_motion && s->phase != OS999_STARTUP_ERROR;
    draw_progress_bar(fb, L.bar_x, L.bar_y, L.bar_w, L.bar_h, progress, now, animate);
    bitmap_draw_scaled(fb, L.status_x, L.status_y, STATUS_COLOR, STATUS_TEXT,
                        L.status_scale);
    if (s->phase == OS999_STARTUP_ERROR) {
        int x = fb->w / 2 - 275, y = fb->h / 2 - 104;
        fb_fill(fb, x + 7, y + 8, 550, 208, 0xFF26344Du);
        fb_fill(fb, x, y, 550, 208, 0xFFF4F5F7u);
        fb_frame(fb, x, y, 550, 208, 0xFF17243Au);
        fb_text(fb, x + 28, y + 26, 0xFF202B3Cu, "OS999 could not start.");
        fb_text(fb, x + 28, y + 66, 0xFF344259u, "Desktop initialization failed.");
        fb_text(fb, x + 28, y + 88, 0xFF344259u, "Check volume permissions, then retry.");
        fb_fill(fb, x + 82, y + 142, 150, 40, 0xFF526F9Du);
        fb_frame(fb, x + 82, y + 142, 150, 40, 0xFF233653u);
        fb_text(fb, x + 131, y + 158, C_WHITE, "Retry");
        fb_fill(fb, x + 318, y + 142, 150, 40, 0xFFD8DDE6u);
        fb_frame(fb, x + 318, y + 142, 150, 40, 0xFF66758Au);
        fb_text(fb, x + 371, y + 158, 0xFF202B3Cu, "Close");
    }
}

void os999_startup_frame(Os999Startup *s, uint32_t now, int ready,
                         uint32_t *pixels, const uint32_t *desktop,
                         int width, int height) {
    if (!s || !pixels || width < 1 || height < 1) return;
    advance(s, now, ready);
    Fb fb; fb_init(&fb, pixels, width, height);
    if (!os999_startup_active(s)) {
        os999_startup_release();
        if (desktop && desktop != pixels)
            memcpy(pixels, desktop, (size_t)width * (size_t)height * sizeof *pixels);
        return;
    }
    render_intro(&fb, s, now);
    if (s->phase == OS999_STARTUP_REVEAL && desktop) {
        double t = (double)(now - s->phase_started) / REVEAL_MS;
        if (t > 1.0) t = 1.0;
        size_t count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < count; i++) pixels[i] = mix(pixels[i], desktop[i], t);
    }
}
