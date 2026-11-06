#ifndef RUNTIME_KEYS_H
#define RUNTIME_KEYS_H

#include <SDL.h>
#include <stdint.h>

int runtime_key_message(const SDL_Keysym *key, uint32_t *message);

#endif
