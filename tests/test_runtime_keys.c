#include "runtime_keys.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    SDL_Keysym key = {.sym = SDLK_a};
    uint32_t message = 0;
    assert(runtime_key_message(&key, &message) && message == 0x61);
    key.mod = KMOD_SHIFT;
    assert(runtime_key_message(&key, &message) && message == 0x41);
    key = (SDL_Keysym){.sym = SDLK_ESCAPE};
    assert(runtime_key_message(&key, &message) && message == 0x351b);
    key = (SDL_Keysym){.sym = SDLK_LEFT};
    assert(runtime_key_message(&key, &message) && message == 0x7b1c);
    key = (SDL_Keysym){.sym = SDLK_HOME};
    assert(!runtime_key_message(&key, &message));
    assert(!runtime_key_message(NULL, &message));
    assert(!runtime_key_message(&key, NULL));
    puts("runtime key mapping: ok");
    return 0;
}
