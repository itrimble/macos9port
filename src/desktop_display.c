#include "desktop_display.h"
#include "display_modes.h"

#include <stdio.h>
#include <stdlib.h>

static int valid_scale(int scale) { return scale >= 50 && scale <= 200 ? scale : 100; }

/* Window size for the current desktop at `scale` percent, inside the usable
 * area of the display the window is on (display 0 before it exists). */
static void window_size(SDL_Window *win, int scale, int *w, int *h) {
    SDL_Rect area = {0, 0, 0, 0};
    int display = win ? SDL_GetWindowDisplayIndex(win) : 0;
    if (display < 0 || SDL_GetDisplayUsableBounds(display, &area) != 0) area.w = area.h = 0;
    int top = 0;
    if (win && area.h > 0 && SDL_GetWindowBordersSize(win, &top, NULL, NULL, NULL) == 0 && top > 0)
        area.h = area.h > top ? area.h - top : 1;
    display_window_fit(OSPI_W, OSPI_H, scale, area.w, area.h, w, h);
}

static void fit_window(DesktopDisplay *d) {
    int w, h, have_w = 0, have_h = 0;
    window_size(d->win, d->scale, &w, &h);
    SDL_GetWindowSize(d->win, &have_w, &have_h);
    if (w == have_w && h == have_h) return;
    SDL_SetWindowSize(d->win, w, h);
    SDL_SetWindowPosition(d->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static int make_buffers(SDL_Renderer *ren, uint32_t **frame, uint32_t **backdrop, SDL_Texture **tex) {
    size_t pixels = (size_t)OSPI_W * (size_t)OSPI_H;
    *frame = calloc(pixels, sizeof **frame);
    *backdrop = calloc(pixels, sizeof **backdrop);
    *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, OSPI_W, OSPI_H);
    if (*frame && *backdrop && *tex) return 0;
    if (*tex) SDL_DestroyTexture(*tex);
    free(*frame);
    free(*backdrop);
    *frame = *backdrop = NULL;
    *tex = NULL;
    return -1;
}

int desktop_display_open(DesktopDisplay *d, const char *volume) {
    *d = (DesktopDisplay){0};
    d->scale = valid_scale(display_mode_startup(volume));
    int w, h;
    window_size(NULL, d->scale, &w, &h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    d->win = SDL_CreateWindow("OS999", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                              SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!d->win) {
        fprintf(stderr, "OS999: could not open a window: %s\n", SDL_GetError());
        return -1;
    }
    d->ren = SDL_CreateRenderer(d->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!d->ren) d->ren = SDL_CreateRenderer(d->win, -1, SDL_RENDERER_SOFTWARE);
    if (!d->ren || make_buffers(d->ren, &d->frame, &d->backdrop, &d->tex) != 0) {
        fprintf(stderr, "OS999: could not create the screen: %s\n", SDL_GetError());
        desktop_display_close(d);
        return -1;
    }
    d->w = OSPI_W;
    d->h = OSPI_H;
    fit_window(d);   /* now that the title bar height is known */
    int ww = 0, wh = 0, rw = 0, rh = 0;
    SDL_GetWindowSize(d->win, &ww, &wh);
    SDL_GetRendererOutputSize(d->ren, &rw, &rh);
    fprintf(stderr, "OS999 desktop %dx%d at %d%%  window %dx%d  drawable %dx%d\n",
            d->w, d->h, d->scale, ww, wh, rw, rh);
    return 0;
}

void desktop_display_close(DesktopDisplay *d) {
    free(d->backdrop);
    free(d->frame);
    if (d->tex) SDL_DestroyTexture(d->tex);
    if (d->ren) SDL_DestroyRenderer(d->ren);
    if (d->win) SDL_DestroyWindow(d->win);
    *d = (DesktopDisplay){0};
}

int desktop_display_sync(DesktopDisplay *d, Session *s) {
    int scale = s ? valid_scale(s->display_window_scale) : d->scale;
    int resized = OSPI_W != d->w || OSPI_H != d->h;
    if (resized) {
        uint32_t *frame, *backdrop;
        SDL_Texture *tex;
        if (make_buffers(d->ren, &frame, &backdrop, &tex) != 0) {
            fprintf(stderr, "OS999: could not switch to %dx%d: %s\n", OSPI_W, OSPI_H, SDL_GetError());
            (void)display_mode_apply(s, d->w, d->h);
            return 0;
        }
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(tex, !s || s->display_integer_scale ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
        free(d->frame);
        free(d->backdrop);
        SDL_DestroyTexture(d->tex);
        d->frame = frame;
        d->backdrop = backdrop;
        d->tex = tex;
        d->w = OSPI_W;
        d->h = OSPI_H;
    }
    if (resized || scale != d->scale) {
        d->scale = scale;
        fit_window(d);
    }
    return resized;
}

void desktop_display_present(DesktopDisplay *d) {
    SDL_UpdateTexture(d->tex, NULL, d->frame, d->w * (int)sizeof *d->frame);
    SDL_RenderClear(d->ren);
    int rw = d->w, rh = d->h;
    SDL_GetRendererOutputSize(d->ren, &rw, &rh);
    SDL_Rect dst = {0, 0, rw, rh};
    SDL_RenderCopy(d->ren, d->tex, NULL, &dst);
    SDL_RenderPresent(d->ren);
}

void desktop_display_point(SDL_Window *win, int x, int y, int *out_x, int *out_y) {
    int ww = 1, wh = 1;
    SDL_GetWindowSize(win, &ww, &wh);
    display_point(x, y, ww, wh, out_x, out_y);
}
