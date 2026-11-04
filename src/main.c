#include "ospi.h"

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *flag_arg(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static void take_clip(Session *s) {
    char *text = SDL_GetClipboardText();
    session_set_clip(s, text ? text : "");
    SDL_free(text);
}

static int resolve_volume(int argc, char **argv, char *out, size_t n) {
    const char *chosen = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--volume") == 0 && i + 1 < argc) {
            chosen = argv[++i];
        }
    }
    char path[OSPI_PATH];
    if (chosen) {
        snprintf(path, sizeof path, "%s", chosen);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "OS Pi: HOME is not set\n");
            return -1;
        }
#if defined(__APPLE__)
        snprintf(path, sizeof path, "%s/Library/Application Support/OS Pi/Volume", home);
#else
        snprintf(path, sizeof path, "%s/.local/share/ospi/Volume", home);
#endif
    }
    if (volume_mkdir_p(path) != 0 || volume_seed(path) != 0) {
        fprintf(stderr, "OS Pi: could not create the volume at %s\n", path);
        return -1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved) || strlen(resolved) >= n) {
        fprintf(stderr, "OS Pi: could not resolve %s\n", path);
        return -1;
    }
    memcpy(out, resolved, strlen(resolved) + 1);
    return 0;
}

static void logical_point(SDL_Window *win, int x, int y, int *ox, int *oy) {
    int ww = 1;
    int wh = 1;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww < 1) {
        ww = 1;
    }
    if (wh < 1) {
        wh = 1;
    }
    *ox = x * OSPI_W / ww;
    *oy = y * OSPI_H / wh;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--seed") == 0) {
        return volume_seed(argv[2]) == 0 ? 0 : 1;
    }
    char volume[OSPI_PATH];
    if (resolve_volume(argc, argv, volume, sizeof volume) != 0) {
        return 1;
    }
    fprintf(stderr, "OS Pi 0.1  %s\n", volume);
    const char *install_url = flag_arg(argc, argv, "--install");
    if (install_url) {
        char rel[OSPI_PATH];
        char err[240];
        err[0] = 0;
        if (install_garden(volume, install_url, rel, sizeof rel, err, sizeof err) != 0) {
            fprintf(stderr, "OS Pi: %s\n", err[0] ? err : "install failed");
            return 1;
        }
        fprintf(stderr, "OS Pi installed %s\n", rel);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "OS Pi: SDL_Init failed: %s\nInstall SDL2 (brew install sdl2, or apt install libsdl2-dev).\n",
                SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *win = SDL_CreateWindow("OS Pi", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, OSPI_W, OSPI_H,
                                       SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "OS Pi: could not open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, OSPI_W, OSPI_H);
    if (!ren || !tex) {
        fprintf(stderr, "OS Pi: could not create the screen: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    int ww = 0;
    int wh = 0;
    int rw = 0;
    int rh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &rw, &rh);
    fprintf(stderr, "OS Pi window %dx%d  drawable %dx%d\n", ww, wh, rw, rh);
    Session *s = session_create(volume);
    if (!s) {
        fprintf(stderr, "OS Pi: could not open %s\n", volume);
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    uint32_t *frame = calloc((size_t)OSPI_W * OSPI_H, sizeof *frame);
    if (!frame) {
        session_destroy(s);
        return 1;
    }
    int dumped = 0;
    const char *dump = getenv("OSPI_DUMP");
    while (!session_quit(s)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            int x = 0;
            int y = 0;
            if (ev.type == SDL_QUIT) {
                s->quit = 1;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                take_clip(s);
                logical_point(win, ev.button.x, ev.button.y, &x, &y);
                session_down(s, x, y, ev.button.clicks);
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                logical_point(win, ev.button.x, ev.button.y, &x, &y);
                session_up(s, x, y);
            } else if (ev.type == SDL_MOUSEMOTION) {
                logical_point(win, ev.motion.x, ev.motion.y, &x, &y);
                session_move(s, x, y);
            } else if (ev.type == SDL_MOUSEWHEEL) {
                session_wheel(s, ev.wheel.y);
            } else if (ev.type == SDL_KEYDOWN) {
                int key = ev.key.keysym.sym;
                if (key == SDLK_KP_ENTER) {
                    key = '\r';
                }
                int mods = (ev.key.keysym.mod & KMOD_GUI) ? MOD_CMD : 0;
                session_key(s, key, mods);
            }
        }
        session_tick(s, SDL_GetTicks());
        session_paint(s, frame, OSPI_W, OSPI_H);
        if (dump && !dumped) {
            FILE *f = fopen(dump, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", OSPI_W, OSPI_H);
                for (int i = 0; i < OSPI_W * OSPI_H; i++) {
                    unsigned char rgb[3] = {(unsigned char)(frame[i] >> 16), (unsigned char)(frame[i] >> 8),
                                            (unsigned char)frame[i]};
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
            dumped = 1;
        }
        SDL_UpdateTexture(tex, NULL, frame, OSPI_W * (int)sizeof *frame);
        SDL_RenderClear(ren);
        int rw = OSPI_W;
        int rh = OSPI_H;
        SDL_GetRendererOutputSize(ren, &rw, &rh);
        SDL_Rect dst = {0, 0, rw, rh};
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);
    }
    free(frame);
    session_destroy(s);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
