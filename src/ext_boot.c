#include "ext_boot.h"
#include "ext_ui.h"

#include <SDL.h>
#include <stdlib.h>

static void present(SDL_Renderer *ren, SDL_Texture *tex, const uint32_t *frame) {
    int rw = 0, rh = 0;
    SDL_UpdateTexture(tex, NULL, frame, OSPI_W * (int)sizeof *frame);
    SDL_RenderClear(ren);
    SDL_GetRendererOutputSize(ren, &rw, &rh);
    SDL_Rect dst = {0, 0, rw, rh};
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

static void run_overlay(ExtManager *m, SDL_Window *win, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *frame) {
    Fb fb;
    Rect panel = {(OSPI_W - 640) / 2, (OSPI_H - 420) / 2, 640, 420};
    Rect content = {panel.x + 16, panel.y + 28, panel.w - 32, panel.h - 44};
    while (!m->done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { SDL_Quit(); exit(0); }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int ww = 1, wh = 1;
                SDL_GetWindowSize(win, &ww, &wh);
                ext_manager_click(m, content, ev.button.x * OSPI_W / (ww > 0 ? ww : 1),
                                  ev.button.y * OSPI_H / (wh > 0 ? wh : 1));
            } else if (ev.type == SDL_MOUSEWHEEL) {
                ext_manager_wheel(m, -ev.wheel.y);
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) ext_manager_key(m, '\r');
                else if (ev.key.keysym.sym == SDLK_ESCAPE) m->done = 1;
            }
        }
        fb_init(&fb, frame, OSPI_W, OSPI_H);
        fb_fill(&fb, 0, 0, OSPI_W, OSPI_H, C_DESK);
        fb_fill(&fb, panel.x, panel.y, panel.w, panel.h, C_MENU);
        fb_frame(&fb, panel.x, panel.y, panel.w, panel.h, C_BLACK);
        fb_text(&fb, panel.x + 12, panel.y + 6, C_BLACK, "Extensions Manager");
        ext_manager_render(m, &fb, content);
        present(ren, tex, frame);
        SDL_Delay(16);
    }
}

void ext_boot_run(const char *volume, SDL_Window *win, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *frame) {
    static ExtManager m;
    if (ext_manager_open(&m, volume, 1) < 0) return;
    SDL_PumpEvents(); /* refresh held-key state: Space/Shift held while launching */
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    const char *env = getenv("OSPI_EXT_MANAGER");
    ExtBootMode mode = ext_boot_decide(keys[SDL_SCANCODE_SPACE] != 0,
                                       (SDL_GetModState() & KMOD_SHIFT) != 0,
                                       env && env[0] == '1', m.list.crash_pending);
    setenv("OSPI_EXT_ROOT", volume, 1);
    if (mode == EXT_BOOT_MANAGER) {
        run_overlay(&m, win, ren, tex, frame);
        ext_state_end_boot(&m.list, volume); /* reviewing clears a crash-loop marker */
    }
    if (mode == EXT_BOOT_NONE) setenv("OSPI_EXT_MODE", "none", 1);
    else unsetenv("OSPI_EXT_MODE");
}
