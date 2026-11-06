#include "mac_icon.h"

uint32_t mac_icon_color(uint8_t index) {
    static const uint8_t cube[6] = {255, 204, 153, 102, 51, 0};
    static const uint8_t ramp[10] = {238, 221, 187, 170, 136,
                                     119, 85, 68, 34, 17};
    uint8_t r, g, b;
    if (index < 216) {
        r = cube[index / 36];
        g = cube[(index / 6) % 6];
        b = cube[index % 6];
    } else {
        unsigned slot = index - 216;
        uint8_t value = ramp[slot % 10];
        r = (slot < 10 || slot >= 30) ? value : 0;
        g = ((slot >= 10 && slot < 20) || slot >= 30) ? value : 0;
        b = ((slot >= 20 && slot < 30) || slot >= 30) ? value : 0;
    }
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
