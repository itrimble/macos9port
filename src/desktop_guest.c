#include "desktop_guest.h"
#include "draw.h"
#include "runtime_bridge.h"

void guest_window_layout(GuestWindow *window, int width, int height,
                                int initial, int old_width, int old_height) {
    int max_w = OSPI_W - 48, max_h = OSPI_H - 78;
    int image_w = width, image_h = height;
    if (image_w > max_w - 16 || image_h > max_h - 44) {
        double sx = (double)(max_w - 16) / image_w;
        double sy = (double)(max_h - 44) / image_h;
        double scale = sx < sy ? sx : sy;
        image_w = (int)(width * scale);
        image_h = (int)(height * scale);
    }
    window->image_w = image_w;
    window->image_h = image_h;
    window->w = image_w + 16;
    window->h = image_h + 44;
    if (initial || width != old_width || height != old_height) {
        window->x = (OSPI_W - window->w) / 2;
        window->y = (OSPI_H - window->h) / 2;
    }
    window->image_x = window->x + 8;
    window->image_y = window->y + 36;
}


void guest_pixel_point(const GuestWindow *window, int width, int height,
                              int x, int y, int *guest_x, int *guest_y) {
    int px = x - window->image_x, py = y - window->image_y;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= window->image_w) px = window->image_w - 1;
    if (py >= window->image_h) py = window->image_h - 1;
    *guest_x = px * width / window->image_w;
    *guest_y = py * height / window->image_h;
}

static void guest_circle(Fb *fb, int x, int y, uint32_t color) {
    static const uint8_t rows[9] = {0x1c,0x3e,0x7f,0x7f,0x7f,0x7f,0x7f,0x3e,0x1c};
    for (int row = 0; row < 9; row++)
        for (int col = 0; col < 7; col++)
            if (rows[row] & (uint8_t)(1u << (6 - col)))
                fb_fill(fb, x + col, y + row, 1, 1, color);
}

void draw_guest_window(uint32_t *frame, const GuestWindow *window,
                              const uint32_t *pixels, int width, int height,
                              uint32_t state, const char *title, const char *message) {
    Fb fb;
    fb_init(&fb, frame, OSPI_W, OSPI_H);
    fb_fill(&fb, window->x + 4, window->y + 4, window->w, window->h, 0xFF26304Au);
    fb_fill(&fb, window->x, window->y, window->w, window->h, C_WHITE);
    fb_frame(&fb, window->x, window->y, window->w, window->h, C_BLACK);
    fb_fill(&fb, window->x + 1, window->y + 1, window->w - 2, 25, C_WHITE);
    fb_hline(&fb, window->x + 1, window->y + 26, window->w - 2, C_BLACK);
    guest_circle(&fb, window->x + 12, window->y + 9,
                 state == RUNTIME_BRIDGE_FAILED ? 0xFFE54A4Au : 0xFFD3D3D3u);
    guest_circle(&fb, window->x + 27, window->y + 9, 0xFFD3D3D3u);
    guest_circle(&fb, window->x + 42, window->y + 9, 0xFFD3D3D3u);
    fb_text(&fb, window->x + 60, window->y + 7, 0xFF555555u, title);
    fb_fill(&fb, window->image_x, window->image_y, window->image_w,
            window->image_h, C_BLACK);
    if (pixels && width > 0 && height > 0) {
        for (int y = 0; y < window->image_h; y++) {
            int sy = y * height / window->image_h;
            for (int x = 0; x < window->image_w; x++) {
                int sx = x * width / window->image_w;
                frame[(size_t)(window->image_y + y) * OSPI_W +
                      (size_t)(window->image_x + x)] =
                    pixels[(size_t)sy * (size_t)width + (size_t)sx];
            }
        }
    } else if (state == RUNTIME_BRIDGE_FAILED && message && message[0]) {
        fb_text(&fb, window->image_x + 12, window->image_y + 18, C_WHITE, message);
    } else {
        fb_text(&fb, window->image_x + 12, window->image_y + 18, C_WHITE, "Starting application...");
    }
}
