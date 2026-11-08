#ifndef OS999_MOBILE_PNG_H
#define OS999_MOBILE_PNG_H

#include <stdint.h>

int mobile_png_write(const char *path, const uint32_t *px, int w, int h, int alpha);
uint32_t *mobile_downscale(const uint32_t *px, int w, int h, int k, int *ow, int *oh);

#endif
