#include "runtime_keys.h"

/* Map SDL's host key symbols to classic Macintosh virtual key messages. */
int runtime_key_message(const SDL_Keysym *key, uint32_t *message) {
    static const uint8_t letters[] = {
        0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6
    };
    static const uint8_t digits[] = {29,18,19,20,21,23,22,26,28,25};
    if (!key || !message) return 0;
    SDL_Keycode k = key->sym;
    unsigned code, character;
    if (k >= SDLK_a && k <= SDLK_z) {
        code = letters[k - SDLK_a];
        character = (unsigned)k;
        if (!!(key->mod & KMOD_SHIFT) != !!(key->mod & KMOD_CAPS)) character -= 32;
    } else if (k >= SDLK_0 && k <= SDLK_9) {
        code = digits[k - SDLK_0];
        character = key->mod & KMOD_SHIFT ? (unsigned char)")!@#$%^&*("[k - SDLK_0] : (unsigned)k;
    } else if (k >= SDLK_F1 && k <= SDLK_F12) {
        static const uint8_t function_codes[] = {122,120,99,118,96,97,98,100,101,109,103,111};
        code = function_codes[k - SDLK_F1];
        character = 16;
    } else if (k >= SDLK_F13 && k <= SDLK_F15) {
        static const uint8_t function_codes[] = {105,107,113};
        code = function_codes[k - SDLK_F13];
        character = 16;
    } else {
        switch (k) {
        case SDLK_LEFT: code = 123; character = 28; break;
        case SDLK_RIGHT: code = 124; character = 29; break;
        case SDLK_DOWN: code = 125; character = 31; break;
        case SDLK_UP: code = 126; character = 30; break;
        case SDLK_RETURN: code = 36; character = 13; break;
        case SDLK_KP_ENTER: code = 76; character = 3; break;
        case SDLK_SPACE: code = 49; character = 32; break;
        case SDLK_TAB: code = 48; character = 9; break;
        case SDLK_BACKSPACE: code = 51; character = 8; break;
        case SDLK_DELETE: code = 117; character = 127; break;
        case SDLK_ESCAPE: code = 53; character = 27; break;
        case SDLK_MINUS: code = 27; character = key->mod & KMOD_SHIFT ? '_' : '-'; break;
        case SDLK_EQUALS: code = 24; character = key->mod & KMOD_SHIFT ? '+' : '='; break;
        case SDLK_LEFTBRACKET: code = 33; character = key->mod & KMOD_SHIFT ? '{' : '['; break;
        case SDLK_RIGHTBRACKET: code = 30; character = key->mod & KMOD_SHIFT ? '}' : ']'; break;
        case SDLK_BACKSLASH: code = 42; character = key->mod & KMOD_SHIFT ? '|' : '\\'; break;
        case SDLK_SEMICOLON: code = 41; character = key->mod & KMOD_SHIFT ? ':' : ';'; break;
        case SDLK_QUOTE: code = 39; character = key->mod & KMOD_SHIFT ? '"' : '\''; break;
        case SDLK_COMMA: code = 43; character = key->mod & KMOD_SHIFT ? '<' : ','; break;
        case SDLK_PERIOD: code = 47; character = key->mod & KMOD_SHIFT ? '>' : '.'; break;
        case SDLK_SLASH: code = 44; character = key->mod & KMOD_SHIFT ? '?' : '/'; break;
        case SDLK_BACKQUOTE: code = 50; character = key->mod & KMOD_SHIFT ? '~' : '`'; break;
        default: return 0;
        }
    }
    *message = (code << 8) | character;
    return 1;
}
