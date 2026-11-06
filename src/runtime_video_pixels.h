/* Private presenter helpers; frame conversion lives in runtime_frame.h. */
#include "runtime_frame.h"
static SDL_Rect viewport(int width, int height, int guest_width, int guest_height) {
    SDL_Rect r = {0, 0, 1, 1};
    if (width < 1 || height < 1 || guest_width < 1 || guest_height < 1) return r;
    if ((int64_t)width * guest_height < (int64_t)height * guest_width) {
        r.w = width;
        r.h = (int)((int64_t)width * guest_height / guest_width);
    } else {
        r.h = height;
        r.w = (int)((int64_t)height * guest_width / guest_height);
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}
