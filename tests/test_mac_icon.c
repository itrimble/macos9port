#include "mac_icon.h"

#include <stdio.h>

#define CHECK(index, expected) do { \
    uint32_t actual = mac_icon_color(index); \
    if (actual != (expected)) { \
        fprintf(stderr, "palette[%u] expected %08x, got %08x\n", \
                (unsigned)(index), (unsigned)(expected), (unsigned)actual); \
        return 1; \
    } \
} while (0)

int main(void) {
    CHECK(0,   0xffffffffu);
    CHECK(1,   0xffffffccu);
    CHECK(6,   0xffffccffu);
    CHECK(215, 0xff000000u);
    CHECK(216, 0xffee0000u);
    CHECK(225, 0xff110000u);
    CHECK(226, 0xff00ee00u);
    CHECK(236, 0xff0000eeu);
    CHECK(246, 0xffeeeeeeu);
    CHECK(255, 0xff111111u);
    puts("Macintosh icon palette tests passed");
    return 0;
}
