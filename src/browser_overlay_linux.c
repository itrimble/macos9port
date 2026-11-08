#include "browser_overlay.h"
#include "runtime_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Linux / Raspberry Pi: CPU frames through one shared-memory surface per
 * window (the same bridge the classic runtime uses), copied into the OS999
 * framebuffer. GPU zero-copy (dmabuf import) is future work. */

enum { SLOTS = 4 };

static RuntimeBridge *g_bridge[SLOTS];
static char g_name[SLOTS][64];
static uint32_t *g_pixels[SLOTS];
static int g_w[SLOTS], g_h[SLOTS];
static uint64_t g_seq[SLOTS], g_frames[SLOTS];

int overlay_listen(char *name, size_t n) {
    snprintf(name, n, "shm");
    return 0;
}

void overlay_window_open(int slot, char *target, size_t n) {
    if (n) target[0] = 0;
    if (slot < 0 || slot >= SLOTS) return;
    if (!g_bridge[slot]) g_bridge[slot] = runtime_bridge_create(g_name[slot], sizeof g_name[slot]);
    if (!g_pixels[slot])
        g_pixels[slot] = calloc((size_t)RUNTIME_BRIDGE_MAX_WIDTH * RUNTIME_BRIDGE_MAX_HEIGHT,
                                sizeof(uint32_t));
    g_w[slot] = g_h[slot] = 0;
    g_seq[slot] = g_frames[slot] = 0;
    if (g_bridge[slot]) snprintf(target, n, "%s", g_name[slot]);
}

void overlay_window_close(int slot) {
    if (slot < 0 || slot >= SLOTS || !g_bridge[slot]) return;
    runtime_bridge_close(g_bridge[slot]);
    runtime_bridge_unlink(g_name[slot]);
    g_bridge[slot] = NULL;
    g_frames[slot] = 0;
}

void overlay_bind(int slot, int id) { (void)slot; (void)id; }

void overlay_place(SDL_Window *win, int lw, int lh, int slot, int visible, int order,
                   OverlayRect content, const OverlayRect *holes, int nholes) {
    (void)win; (void)lw; (void)lh; (void)slot; (void)visible; (void)order;
    (void)content; (void)holes; (void)nholes;  /* drawn into the framebuffer instead */
}

void overlay_draw_cpu(int slot, uint32_t *frame, int fw, int fh, OverlayRect c) {
    if (slot < 0 || slot >= SLOTS || !g_bridge[slot] || !g_pixels[slot]) return;
    int w = 0, h = 0;
    uint64_t seq = g_seq[slot];
    int got = runtime_bridge_read(g_bridge[slot], g_pixels[slot],
                                  (size_t)RUNTIME_BRIDGE_MAX_WIDTH * RUNTIME_BRIDGE_MAX_HEIGHT,
                                  &w, &h, &seq);
    if (got > 0 && w > 0 && h > 0) {  /* a new frame */
        g_frames[slot]++;
        g_seq[slot] = seq;
        g_w[slot] = w;
        g_h[slot] = h;
    }
    if (g_frames[slot] == 0) return;
    int cw = c.w < g_w[slot] ? c.w : g_w[slot], ch = c.h < g_h[slot] ? c.h : g_h[slot];
    for (int y = 0; y < ch; y++) {
        int ty = c.y + y;
        if (ty < 0 || ty >= fh) continue;
        for (int x = 0; x < cw; x++) {
            int tx = c.x + x;
            if (tx >= 0 && tx < fw)
                frame[(size_t)ty * fw + tx] = 0xFF000000u | g_pixels[slot][(size_t)y * g_w[slot] + x];
        }
    }
}

float overlay_scale(SDL_Window *win, int lw) {
    (void)win; (void)lw;
    return 1.0f;
}

uint64_t overlay_frames(int slot) { return slot >= 0 && slot < SLOTS ? g_frames[slot] : 0; }
