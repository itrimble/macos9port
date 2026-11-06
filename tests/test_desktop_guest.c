#include "desktop_guest.h"
#include "draw.h"
#include "runtime_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

static void test_layout_and_scaling(void) {
    GuestWindow gw = {0};
    guest_window_layout(&gw, 640, 480, 1, 0, 0);
    CHECK(gw.image_w == 640 && gw.image_h == 480);
    CHECK(gw.w == 656 && gw.h == 524);
    CHECK(gw.x == (OSPI_W - 656) / 2 && gw.y == (OSPI_H - 524) / 2);
    CHECK(gw.image_x == gw.x + 8 && gw.image_y == gw.y + 36);

    /* Preserves user-dragged position when guest dimensions do not change. */
    gw.x = 44;
    gw.y = 52;
    guest_window_layout(&gw, 640, 480, 0, 640, 480);
    CHECK(gw.x == 44 && gw.y == 52);
    CHECK(gw.image_x == 52 && gw.image_y == 88);

    /* Recenters when guest resolution changes. */
    guest_window_layout(&gw, 512, 384, 0, 640, 480);
    CHECK(gw.image_w == 512 && gw.image_h == 384);
    CHECK(gw.w == 528 && gw.h == 428);
    CHECK(gw.x == (OSPI_W - 528) / 2 && gw.y == (OSPI_H - 428) / 2);

    /* Scales oversized 1280x960 frame proportionally inside desktop bounds. */
    guest_window_layout(&gw, 1280, 960, 1, 512, 384);
    CHECK(gw.w <= OSPI_W - 48 && gw.h <= OSPI_H - 78);
    CHECK(gw.image_w == 861 && gw.image_h == 646);
    CHECK(gw.x >= 0 && gw.y >= 0);
    CHECK(gw.x + gw.w <= OSPI_W && gw.y + gw.h <= OSPI_H);
}

static void test_pixel_mapping_and_clamping(void) {
    GuestWindow gw = {0};
    guest_window_layout(&gw, 640, 480, 1, 0, 0);

    int gx = -1, gy = -1;
    guest_pixel_point(&gw, 640, 480, gw.image_x, gw.image_y, &gx, &gy);
    CHECK(gx == 0 && gy == 0);

    guest_pixel_point(&gw, 640, 480, gw.image_x + 320, gw.image_y + 240, &gx, &gy);
    CHECK(gx == 320 && gy == 240);

    guest_pixel_point(&gw, 640, 480, gw.image_x + 639, gw.image_y + 479, &gx, &gy);
    CHECK(gx == 639 && gy == 479);

    /* Clamps out-of-bounds desktop coordinates on all four sides. */
    guest_pixel_point(&gw, 640, 480, -50, -50, &gx, &gy);
    CHECK(gx == 0 && gy == 0);

    guest_pixel_point(&gw, 640, 480, OSPI_W + 200, OSPI_H + 200, &gx, &gy);
    CHECK(gx == 639 && gy == 479);

    /* Scaled coordinate mapping (1280x960 guest inside scaled window). */
    guest_window_layout(&gw, 1280, 960, 1, 640, 480);
    guest_pixel_point(&gw, 1280, 960, gw.image_x, gw.image_y, &gx, &gy);
    CHECK(gx == 0 && gy == 0);
    guest_pixel_point(&gw, 1280, 960, gw.image_x + gw.image_w + 50,
                      gw.image_y + gw.image_h + 50, &gx, &gy);
    CHECK(gx == (gw.image_w - 1) * 1280 / gw.image_w);
    CHECK(gy == (gw.image_h - 1) * 960 / gw.image_h);
}

static int count_color_in_rect(const uint32_t *frame, int x0, int y0, int w, int h, uint32_t color) {
    int count = 0;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            if (frame[(size_t)y * OSPI_W + (size_t)x] == color) {
                count++;
            }
        }
    }
    return count;
}

static void test_draw_guest_states(void) {
    uint32_t *frame = calloc((size_t)OSPI_W * OSPI_H, sizeof *frame);
    CHECK(frame != NULL);
    if (!frame) return;

    GuestWindow gw = {0};
    guest_window_layout(&gw, 320, 200, 1, 0, 0);

    /* 1. Starting state (no guest pixels yet). */
    draw_guest_window(frame, &gw, NULL, 0, 0, RUNTIME_BRIDGE_STARTING, "SynthGuest", "");
    CHECK(frame[(size_t)gw.y * OSPI_W + (size_t)gw.x] == C_BLACK);
    CHECK(frame[(size_t)(gw.y + gw.h + 2) * OSPI_W + (size_t)(gw.x + gw.w + 2)] == 0xFF26304Au);
    CHECK(frame[(size_t)(gw.y + 13) * OSPI_W + (size_t)(gw.x + 15)] == 0xFFD3D3D3u);
    CHECK(count_color_in_rect(frame, gw.image_x, gw.image_y, 260, 40, C_WHITE) > 20);

    /* 2. Failed state renders red close indicator and failure diagnostic. */
    memset(frame, 0, (size_t)OSPI_W * OSPI_H * sizeof *frame);
    draw_guest_window(frame, &gw, NULL, 0, 0, RUNTIME_BRIDGE_FAILED,
                      "SynthGuest", "Unsupported trap 0xABCD");
    CHECK(frame[(size_t)(gw.y + 13) * OSPI_W + (size_t)(gw.x + 15)] == 0xFFE54A4Au);
    CHECK(count_color_in_rect(frame, gw.image_x, gw.image_y, 260, 40, C_WHITE) > 20);

    /* 3. Ready state blits guest pixels (including scaling). */
    const uint32_t src[4] = {
        0xFF112233u, 0xFF445566u,
        0xFF778899u, 0xFFAABBCCu
    };
    memset(frame, 0, (size_t)OSPI_W * OSPI_H * sizeof *frame);
    draw_guest_window(frame, &gw, src, 2, 2, RUNTIME_BRIDGE_READY, "SynthGuest", "");
    CHECK(frame[(size_t)gw.image_y * OSPI_W + (size_t)gw.image_x] == 0xFF112233u);
    CHECK(frame[(size_t)gw.image_y * OSPI_W + (size_t)(gw.image_x + gw.image_w - 1)] == 0xFF445566u);
    CHECK(frame[(size_t)(gw.image_y + gw.image_h - 1) * OSPI_W + (size_t)gw.image_x] == 0xFF778899u);
    CHECK(frame[(size_t)(gw.image_y + gw.image_h - 1) * OSPI_W +
                (size_t)(gw.image_x + gw.image_w - 1)] == 0xFFAABBCCu);

    free(frame);
}

static void test_drag_containment(void) {
    GuestWindow gw = {0};
    guest_window_layout(&gw, 640, 480, 1, 0, 0);
    int press_x = 100, press_y = 10; /* grab in the title bar */
    /* Free move inside the desktop keeps the grab offset and image origin. */
    CHECK(guest_window_drag(&gw, 300, 200, press_x, press_y) == 0);
    CHECK(gw.x == 200 && gw.y == 190 && gw.image_x == 208 && gw.image_y == 226);
    /* Past the left edge and above the menu bar: held at x=0, y=TITLE_H. */
    CHECK(guest_window_drag(&gw, 5, 3, press_x, press_y) == 1);
    CHECK(gw.x == 0 && gw.y == TITLE_H && gw.image_x == 8 && gw.image_y == TITLE_H + 36);
    /* Far beyond the bottom-right corner: right and bottom edges stay inside. */
    CHECK(guest_window_drag(&gw, OSPI_W + 500, OSPI_H + 500, press_x, press_y) == 1);
    CHECK(gw.x + gw.w == OSPI_W && gw.y + gw.h == OSPI_H);
    CHECK(gw.image_x + gw.image_w <= OSPI_W && gw.image_y + gw.image_h <= OSPI_H);
    /* Exactly at the limits is not clamped. */
    CHECK(guest_window_drag(&gw, OSPI_W - gw.w + press_x, OSPI_H - gw.h + press_y, press_x, press_y) == 0);
    CHECK(gw.x + gw.w == OSPI_W && gw.y + gw.h == OSPI_H);
    /* Negative pointer coordinates (SDL can report them during capture). */
    CHECK(guest_window_drag(&gw, -1000, -1000, press_x, press_y) == 1);
    CHECK(gw.x == 0 && gw.y == TITLE_H);
    /* The largest window the layout allows still fits after any drag. */
    GuestWindow big = {0};
    guest_window_layout(&big, 1280, 960, 1, 0, 0);
    for (int corner = 0; corner < 4; corner++) {
        int x = corner & 1 ? 10000 : -10000, y = corner & 2 ? 10000 : -10000;
        guest_window_drag(&big, x, y, 1, 1);
        CHECK(big.x >= 0 && big.y >= TITLE_H && big.x + big.w <= OSPI_W && big.y + big.h <= OSPI_H);
    }
}

int main(void) {
    test_drag_containment();
    test_layout_and_scaling();
    test_pixel_mapping_and_clamping();
    test_draw_guest_states();
    if (fails) {
        fprintf(stderr, "desktop guest checks: %d failed\n", fails);
        return 1;
    }
    puts("desktop guest checks: ok");
    return 0;
}
